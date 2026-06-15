#include "tcp_client.h"
#include <iostream>
#include <algorithm>

namespace mp2310 {

static class WinsockInit {
public:
    WinsockInit() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~WinsockInit() { WSACleanup(); }
} s_winsock;

TcpClient::TcpClient()
    : m_socket(INVALID_SOCKET)
    , m_port(16002), m_serial(0)
{
}

TcpClient::~TcpClient() {
    disconnect();
}

bool TcpClient::connect(const std::string& host, uint16_t port,
                        const std::string& localHost, uint16_t localPort) {
    if (m_connected) disconnect();

    m_host = host;
    m_port = port;
    m_serial = 0;

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        if (m_onError) m_onError("Failed to create socket");
        return false;
    }

    sockaddr_in localAddress{};
    localAddress.sin_family = AF_INET;
    localAddress.sin_port = htons(localPort);
    if (inet_pton(AF_INET, localHost.c_str(), &localAddress.sin_addr) != 1) {
        if (m_onError) m_onError("Invalid local IPv4 address: " + localHost);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }
    if (::bind(m_socket, reinterpret_cast<sockaddr*>(&localAddress), sizeof(localAddress)) ==
        SOCKET_ERROR) {
        if (m_onError) m_onError("Failed to bind local address " + localHost + ":" +
                                 std::to_string(localPort));
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        if (m_onError) m_onError("Invalid IPv4 address: " + host);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    int result = ::connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR) {
        if (m_onError) m_onError("Connection failed: " + host + ":" + std::to_string(port));
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    m_connected = true;
    if (m_onConnect) m_onConnect(true);

    m_recvThread = std::thread(&TcpClient::receiveThread, this);
    return true;
}

void TcpClient::disconnect() {
    m_connected = false;
    if (m_socket != INVALID_SOCKET) {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    if (m_recvThread.joinable()) m_recvThread.join();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& req : m_pendingRequests) req.completed = true;
    }
    m_pendingCv.notify_all();
}

bool TcpClient::sendFrame(const std::vector<uint8_t>& frame) {
    if (!m_connected || frame.empty()) return false;

    // TCP 的 send 允许只发送一部分数据，必须循环到完整帧全部发出。
    std::lock_guard<std::mutex> sendLock(m_sendMutex);
    size_t offset = 0;
    while (offset < frame.size()) {
        int sent = ::send(m_socket, reinterpret_cast<const char*>(frame.data() + offset),
                          static_cast<int>(frame.size() - offset), 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            if (m_onError) m_onError("Send failed: " + std::to_string(WSAGetLastError()));
            m_errorCount++;
            return false;
        }
        offset += static_cast<size_t>(sent);
    }
    m_sentCount++;
    return true;
}

bool TcpClient::sendReadCommand(uint16_t addr, uint16_t count) {
    uint8_t serial = m_serial.fetch_add(1);
    auto frame = FrameBuilder::buildReadCommand(serial, addr, count);
    return sendFrame(frame);
}

bool TcpClient::sendWriteSingleCommand(uint16_t addr, uint16_t value) {
    uint8_t serial = m_serial.fetch_add(1);
    auto frame = FrameBuilder::buildWriteSingleCommand(serial, addr, value);
    return sendFrame(frame);
}

bool TcpClient::sendWriteMultiCommand(uint16_t addr, const std::vector<uint16_t>& values) {
    uint8_t serial = m_serial.fetch_add(1);
    auto frame = FrameBuilder::buildWriteMultiCommand(serial, addr, 
                     static_cast<uint16_t>(values.size()), values);
    return sendFrame(frame);
}

bool TcpClient::sendAndWait(const std::vector<uint8_t>& frame, 
                             std::vector<uint8_t>& response, int timeoutMs) {
    if (!m_connected || frame.size() < TOTAL_HEADER_SIZE) return false;

    uint8_t serial = frame[1];  // 序列号在字节[1]

    // 注册等待请求
    PendingRequest pending;
    pending.serial = serial;
    pending.request = frame;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingRequests.push_back(pending);
    }

    if (!sendFrame(frame)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingRequests.erase(
            std::remove_if(m_pendingRequests.begin(), m_pendingRequests.end(),
                [serial](const PendingRequest& r) { return r.serial == serial; }),
            m_pendingRequests.end());
        return false;
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    const bool ready = m_pendingCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        return !m_connected || std::any_of(m_pendingRequests.begin(), m_pendingRequests.end(),
            [serial](const PendingRequest& r) { return r.serial == serial && r.completed; });
    });
    auto it = std::find_if(m_pendingRequests.begin(), m_pendingRequests.end(),
        [serial](const PendingRequest& r) { return r.serial == serial; });
    if (ready && it != m_pendingRequests.end() && it->completed && !it->response.empty()) {
        response = std::move(it->response);
        m_pendingRequests.erase(it);
        return true;
    }
    if (it != m_pendingRequests.end()) m_pendingRequests.erase(it);
    return false;
}

void TcpClient::receiveThread() {
    std::vector<uint8_t> buffer(DATA_SIZE);
    std::vector<uint8_t> stream;

    while (m_connected) {
        int bytesRead = ::recv(m_socket, reinterpret_cast<char*>(buffer.data()),
                               DATA_SIZE, 0);
        if (bytesRead <= 0) {
            if (bytesRead == 0) {
                if (m_onError) m_onError("Connection closed");
            } else {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    if (m_onError) m_onError("Recv error");
                    m_errorCount++;
                }
            }
            if (bytesRead < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
        stream.insert(stream.end(), buffer.begin(), buffer.begin() + bytesRead);

        // TCP 是字节流，一次 recv 可能包含半帧或多帧。218 报头的 [6-7]
        // 给出完整帧长度，可据此拆包。
        while (stream.size() >= HEADER_218_SIZE) {
            const size_t frameSize = stream[6] | (static_cast<size_t>(stream[7]) << 8);
            if (frameSize < READ_RESPONSE_DATA_OFFSET || frameSize > DATA_SIZE) {
                if (m_onError) m_onError("Invalid response frame length");
                m_errorCount++;
                stream.clear();
                break;
            }
            if (stream.size() < frameSize) break;
            std::vector<uint8_t> received(stream.begin(), stream.begin() + frameSize);
            stream.erase(stream.begin(), stream.begin() + frameSize);
            m_recvCount++;

            if (m_onData) m_onData(received);

            uint8_t rspSerial = received[1];
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& req : m_pendingRequests) {
                if (req.serial == rspSerial && !req.completed) {
                    if (ResponseValidator::validate(static_cast<int>(received.size()),
                                                    req.request, received) == 0) {
                        req.response = received;
                        req.completed = true;
                        m_pendingCv.notify_all();
                    } else {
                        m_errorCount++;
                    }
                }
            }
        }
    }

    m_connected = false;
    m_pendingCv.notify_all();
    if (m_onConnect) m_onConnect(false);
}

} // namespace mp2310
