#include "tcp_server.h"
#include <chrono>
#include <thread>

namespace mp2310 {
namespace sim {

static class WinsockInit {
public:
    WinsockInit() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~WinsockInit() { WSACleanup(); }
} s_winsock;

TcpServer::TcpServer() : m_port(16002) {
    // Default registers (MW00000 ~ MW00009)
    m_registers[0x0000] = 0x0000;
    m_registers[0x0001] = 0x0000;
    m_registers[0x0002] = 0x0001;
    m_registers[0x0003] = 0x0000;
    m_registers[0x0004] = 0x0000;
    m_registers[0x0005] = 0x0000;
    m_registers[0x0006] = 0x0000;
    m_registers[0x0007] = 0x0000;
    m_registers[0x0008] = 0x0000;
    m_registers[0x0009] = 0x0000;
    m_registers[0x0100] = 0x0000;
    m_registers[0x0101] = 0x0000;
    m_registers[0x0102] = 0x0000;
    m_registers[0x0103] = 0x0000;
    m_registers[0x0104] = 0x0000;
    m_registers[0x0105] = 0x0000;
    m_registers[0x0106] = 0x0000;
    m_registers[0x0107] = 0x0000;
    m_registers[0x0108] = 100;
    m_registers[0x0109] = 100;
}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start(uint16_t port) {
    if (m_running) stop();
    m_port = port;

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        std::cerr << "[Simulator] Failed to create socket" << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[Simulator] Failed to bind port " << port << std::endl;
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }
    if (::listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[Simulator] Failed to listen" << std::endl;
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    m_running = true;
    m_acceptThread = std::thread(&TcpServer::acceptThread, this);
    std::cout << "[Simulator] MP2310 Simulator started, listening on port " << port << std::endl;
    return true;
}

void TcpServer::stop() {
    m_running = false;
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    if (m_acceptThread.joinable()) m_acceptThread.join();
    std::lock_guard<std::mutex> lock(m_threadMutex);
    for (auto& t : m_clientThreads) {
        if (t.joinable()) t.join();
    }
    m_clientThreads.clear();
}

void TcpServer::acceptThread() {
    while (m_running) {
        SOCKET clientSock = ::accept(m_listenSocket, nullptr, nullptr);
        if (clientSock == INVALID_SOCKET) {
            if (m_running) std::cerr << "[Simulator] Accept failed" << std::endl;
            break;
        }
        m_clientCount++;
        std::cout << "[Simulator] New client (total: " << m_clientCount << ")" << std::endl;
        std::lock_guard<std::mutex> lock(m_threadMutex);
        m_clientThreads.emplace_back(&TcpServer::clientThread, this, clientSock);
        m_clientThreads.erase(
            std::remove_if(m_clientThreads.begin(), m_clientThreads.end(),
                [](const std::thread& t) { return !t.joinable(); }),
            m_clientThreads.end());
    }
}

void TcpServer::clientThread(SOCKET clientSock) {
    std::vector<uint8_t> buffer(DATA_SIZE);
    std::vector<uint8_t> stream;
    u_long mode = 1;
    ioctlsocket(clientSock, FIONBIO, &mode);
    while (m_running) {
        int bytesRead = ::recv(clientSock, reinterpret_cast<char*>(buffer.data()),
                               DATA_SIZE, 0);
        if (bytesRead <= 0) {
            if (bytesRead == 0) {
                std::cout << "[Simulator] Client disconnected" << std::endl;
            } else {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                std::cerr << "[Simulator] Recv error: " << err << std::endl;
            }
            break;
        }
        stream.insert(stream.end(), buffer.begin(), buffer.begin() + bytesRead);
        while (stream.size() >= HEADER_218_SIZE) {
            const size_t frameSize = stream[6] | (static_cast<size_t>(stream[7]) << 8);
            if (frameSize < TOTAL_HEADER_SIZE || frameSize > DATA_SIZE) {
                std::cerr << "[Simulator] Invalid frame length: " << frameSize << std::endl;
                stream.clear();
                break;
            }
            if (stream.size() < frameSize) break;

            std::vector<uint8_t> request(stream.begin(), stream.begin() + frameSize);
            stream.erase(stream.begin(), stream.begin() + frameSize);
            std::cout << "[Simulator] RX: " << Util::bytesToHex(request) << std::endl;
            if (m_responseDelayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(m_responseDelayMs));
            auto response = handleFrame(request);
            size_t sentTotal = 0;
            while (sentTotal < response.size()) {
                const int sent = ::send(clientSock,
                    reinterpret_cast<const char*>(response.data() + sentTotal),
                    static_cast<int>(response.size() - sentTotal), 0);
                if (sent <= 0) break;
                sentTotal += static_cast<size_t>(sent);
            }
            if (!response.empty()) {
                std::cout << "[Simulator] TX: " << Util::bytesToHex(response) << std::endl;
            }
        }
        // Simulate motion
        {
            std::lock_guard<std::mutex> lock(m_regMutex);
            uint16_t ctrl = m_registers[0x0001];
            if (ctrl & 0x01) {
                int32_t targetPos = (m_registers[0x0100] << 16) | m_registers[0x0101];
                int32_t currentPos = (m_registers[0x0102] << 16) | m_registers[0x0103];
                if (currentPos < targetPos) {
                    currentPos += 10;
                    if (currentPos > targetPos) currentPos = targetPos;
                } else if (currentPos > targetPos) {
                    currentPos -= 10;
                    if (currentPos < targetPos) currentPos = targetPos;
                }
                m_registers[0x0102] = (currentPos >> 16) & 0xFFFF;
                m_registers[0x0103] = currentPos & 0xFFFF;
                m_registers[0x0106] = m_registers[0x0104];
                m_registers[0x0107] = m_registers[0x0105];
                m_registers[0x0000] |= 0x0001;
            } else {
                m_registers[0x0000] &= ~0x0001;
                m_registers[0x0106] = 0;
                m_registers[0x0107] = 0;
            }
        }
    }
    closesocket(clientSock);
    m_clientCount--;
    std::cout << "[Simulator] Client released (total: " << m_clientCount << ")" << std::endl;
}

// ============================================================
// Extended MEMOBUS Frame Handler
// ============================================================
std::vector<uint8_t> TcpServer::handleFrame(const std::vector<uint8_t>& request) {
    if (request.size() < TOTAL_HEADER_SIZE) {
        std::cerr << "[Simulator] Frame too short: " << request.size() << " bytes" << std::endl;
        return {};
    }
    if (request[0] != static_cast<uint8_t>(DataType::Command)) {
        std::cerr << "[Simulator] Not a command: type=0x"
                  << std::hex << static_cast<int>(request[0]) << std::dec << std::endl;
        return {};
    }
    const uint16_t declaredTotal = request[6] | (request[7] << 8);
    const uint16_t declaredMemobus = request[12] | (request[13] << 8);
    if (declaredTotal != request.size() || declaredMemobus + 14 != request.size() ||
        request[14] != MFC_VALUE) {
        std::cerr << "[Simulator] Invalid MEMOBUS frame header" << std::endl;
        return {};
    }
    uint8_t sfc = request[15];
    switch (sfc) {
    case static_cast<uint8_t>(SFC::ReadHoldingRegisters):
        return handleReadRegisters(request);
    case static_cast<uint8_t>(SFC::WriteSingleRegister):
        return handleWriteSingle(request);
    case static_cast<uint8_t>(SFC::WriteMultipleRegisters):
        return handleWriteMultiple(request);
    default:
        std::cerr << "[Simulator] Unknown SFC: 0x" << std::hex
                  << static_cast<int>(sfc) << std::dec << std::endl;
        return {};
    }
}

static std::vector<uint8_t> buildResponse(const std::vector<uint8_t>& req,
                                           const std::vector<uint8_t>& extraData) {
    std::vector<uint8_t> resp = req;
    resp[0] = static_cast<uint8_t>(DataType::Response);  // 0x19
    uint16_t extraBytes = static_cast<uint16_t>(extraData.size());
    uint16_t mDataLen = MEMOBUS_FIXED_SIZE + extraBytes;
    uint16_t totalLen = HEADER_218_SIZE + MEMOBUS_LENGTH_SIZE + mDataLen;
    resp[6] = static_cast<uint8_t>(totalLen & 0xFF);
    resp[7] = static_cast<uint8_t>((totalLen >> 8) & 0xFF);
    resp[12] = static_cast<uint8_t>(mDataLen & 0xFF);
    resp[13] = static_cast<uint8_t>((mDataLen >> 8) & 0xFF);
    resp.insert(resp.end(), extraData.begin(), extraData.end());
    resp.resize(totalLen);
    return resp;
}

std::vector<uint8_t> TcpServer::handleReadRegisters(const std::vector<uint8_t>& req) {
    // LE format: Adr=[18-19], DataNum=[20-21]
    uint16_t startAddr = req[18] | (req[19] << 8);
    uint16_t count = req[20] | (req[21] << 8);
    if (count == 0 || count > 125) return {};
    // 手册中的 SFC=0x09 响应省略请求中的引用地址:
    // [18-19] 为数量，[20...] 为数据，因此总长度为 20 + count * 2。
    const uint16_t totalLen = READ_RESPONSE_DATA_OFFSET + count * 2;
    const uint16_t memobusLen = totalLen - HEADER_218_SIZE - MEMOBUS_LENGTH_SIZE;
    std::vector<uint8_t> response(totalLen, 0);
    std::copy_n(req.begin(), 18, response.begin());
    response[0] = static_cast<uint8_t>(DataType::Response);
    response[6] = static_cast<uint8_t>(totalLen & 0xFF);
    response[7] = static_cast<uint8_t>((totalLen >> 8) & 0xFF);
    response[12] = static_cast<uint8_t>(memobusLen & 0xFF);
    response[13] = static_cast<uint8_t>((memobusLen >> 8) & 0xFF);
    response[18] = static_cast<uint8_t>(count & 0xFF);
    response[19] = static_cast<uint8_t>((count >> 8) & 0xFF);

    std::lock_guard<std::mutex> lock(m_regMutex);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t addr = startAddr + i;
        uint16_t value = m_registers.count(addr) ? m_registers[addr] : 0;
        response[READ_RESPONSE_DATA_OFFSET + i * 2] = static_cast<uint8_t>(value & 0xFF);
        response[READ_RESPONSE_DATA_OFFSET + i * 2 + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }
    return response;
}

std::vector<uint8_t> TcpServer::handleWriteSingle(const std::vector<uint8_t>& req) {
    // SFC=0x0B: Adr[18-19], Count[20-21]=1, Data[22-23]
    if (req.size() < 24 || req[20] != 1 || req[21] != 0) return {};
    uint16_t addr = req[18] | (req[19] << 8);
    uint16_t value = req[22] | (req[23] << 8);
    {
        std::lock_guard<std::mutex> lock(m_regMutex);
        m_registers[addr] = value;
    }
    std::cout << "[Simulator] Write MW" << std::dec << addr
              << " = 0x" << std::hex << value << std::dec << std::endl;
    std::vector<uint8_t> extra = { req[20], req[21] };  // echo count
    return buildResponse(req, extra);
}

std::vector<uint8_t> TcpServer::handleWriteMultiple(const std::vector<uint8_t>& req) {
    // SFC=0x0C: Adr[18-19], Count[20-21], Data[22..]
    uint16_t startAddr = req[18] | (req[19] << 8);
    uint16_t count = req[20] | (req[21] << 8);
    if (count == 0 || count > 123 ||
        req.size() != TOTAL_HEADER_SIZE + static_cast<size_t>(count) * 2) return {};
    {
        std::lock_guard<std::mutex> lock(m_regMutex);
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t offset = TOTAL_HEADER_SIZE + i * 2;
            if (offset + 1 >= req.size()) break;
            uint16_t value = req[offset] | (req[offset + 1] << 8);
            m_registers[startAddr + i] = value;
        }
    }
    std::cout << "[Simulator] Write MW" << std::dec << startAddr
              << " - MW" << (startAddr + count - 1) << std::endl;
    std::vector<uint8_t> extra = { req[20], req[21] };  // echo count
    return buildResponse(req, extra);
}

void TcpServer::setRegister(uint16_t addr, uint16_t value) {
    std::lock_guard<std::mutex> lock(m_regMutex);
    m_registers[addr] = value;
}

void TcpServer::setRegister32(uint16_t addr, int32_t value) {
    std::lock_guard<std::mutex> lock(m_regMutex);
    m_registers[addr]     = (value >> 16) & 0xFFFF;
    m_registers[addr + 1] = value & 0xFFFF;
}

uint16_t TcpServer::getRegister(uint16_t addr) const {
    std::lock_guard<std::mutex> lock(m_regMutex);
    auto it = m_registers.find(addr);
    return it != m_registers.end() ? it->second : 0;
}

int32_t TcpServer::getRegister32(uint16_t addr) const {
    std::lock_guard<std::mutex> lock(m_regMutex);
    uint16_t hi = m_registers.count(addr) ? m_registers.at(addr) : 0;
    uint16_t lo = m_registers.count(addr + 1) ? m_registers.at(addr + 1) : 0;
    return (static_cast<int32_t>(hi) << 16) | lo;
}

void TcpServer::setSimulateAlarm(bool alarm) {
    m_simulateAlarm = alarm;
    std::lock_guard<std::mutex> lock(m_regMutex);
    m_registers[0x0003] = alarm ? 1 : 0;
}

} // namespace sim
} // namespace mp2310
