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

bool TcpClient::connect(const std::string& host, uint16_t port) {
    if (m_connected) disconnect();

    m_host = host;
    m_port = port;
    m_serial = 0;

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        if (m_onError) m_onError("Failed to create socket");
        return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

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
        m_pendingRequests.clear();
    }
}

bool TcpClient::sendFrame(const std::vector<uint8_t>& frame) {
    if (!m_connected) return false;

    int sent = ::send(m_socket, reinterpret_cast<const char*>(frame.data()),
                      static_cast<int>(frame.size()), 0);
    if (sent == SOCKET_ERROR) {
        if (m_onError) m_onError("Send failed");
        m_errorCount++;
        return false;
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
    if (!m_connected) return false;

    uint8_t serial = frame[1];  // 序列号在字节[1]

    // 注册等待请求
    PendingRequest pending;
    pending.serial = serial;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingRequests.push_back(pending);
    }

    if (!sendFrame(frame)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingRequests.pop_back();
        return false;
    }

    // 等待响应
    auto startTime = std::chrono::steady_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = std::find_if(m_pendingRequests.begin(), m_pendingRequests.end(),
                [serial](const PendingRequest& r) { return r.serial == serial && r.completed; });
            if (it != m_pendingRequests.end()) {
                response = it->response;
                m_pendingRequests.erase(it);
                return true;
            }
        }
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeoutMs) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingRequests.erase(
                std::remove_if(m_pendingRequests.begin(), m_pendingRequests.end(),
                    [serial](const PendingRequest& r) { return r.serial == serial; }),
                m_pendingRequests.end());
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void TcpClient::receiveThread() {
    std::vector<uint8_t> buffer(DATA_SIZE);

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
        m_recvCount++;

        // 复制收到的数据
        std::vector<uint8_t> received(buffer.begin(), buffer.begin() + bytesRead);

        // 验证响应
        int rc = 0;
        {
            // 我们需要从待匹配中获取对应的发送帧用于验证
            // 这里简化处理: 仅调用回调，不做完整验证
        }

        // 触发数据回调
        if (m_onData) {
            m_onData(received);
        }

        // 匹配等待队列 (通过序列号)
        if (received.size() > 1) {
            uint8_t rspSerial = received[1];
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& req : m_pendingRequests) {
                if (req.serial == rspSerial && !req.completed) {
                    req.response = received;
                    req.completed = true;
                }
            }
        }
    }

    m_connected = false;
    if (m_onConnect) m_onConnect(false);
}

} // namespace mp2310
