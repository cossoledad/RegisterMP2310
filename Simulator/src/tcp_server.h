#ifndef MP2310_TCP_SERVER_H
#define MP2310_TCP_SERVER_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <memory>
#include <iostream>
#include <sstream>
#include <algorithm>
#include "../../common/protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace mp2310 {
namespace sim {

class TcpServer {
public:
    TcpServer();
    ~TcpServer();

    bool start(uint16_t port = 16002);
    void stop();
    bool isRunning() const { return m_running; }

    void setRegister(uint16_t addr, uint16_t value);
    void setRegister32(uint16_t addr, int32_t value);
    uint16_t getRegister(uint16_t addr) const;
    int32_t getRegister32(uint16_t addr) const;

    int getClientCount() const { return m_clientCount; }
    void setSimulateAlarm(bool alarm) { m_simulateAlarm = alarm; }
    void setResponseDelay(int ms) { m_responseDelayMs = ms; }

private:
    // 处理请求帧 (完整218帧)
    std::vector<uint8_t> handleFrame(const std::vector<uint8_t>& request);

    // SFC处理
    std::vector<uint8_t> handleReadRegisters(const std::vector<uint8_t>& req);
    std::vector<uint8_t> handleWriteSingle(const std::vector<uint8_t>& req);
    std::vector<uint8_t> handleWriteMultiple(const std::vector<uint8_t>& req);

    void acceptThread();
    void clientThread(SOCKET clientSock);

    uint16_t m_port;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_clientCount{0};
    std::atomic<bool> m_simulateAlarm{false};
    std::atomic<int> m_responseDelayMs{0};

    SOCKET m_listenSocket{INVALID_SOCKET};

    mutable std::mutex m_regMutex;
    std::map<uint16_t, uint16_t> m_registers;

    std::thread m_acceptThread;
    std::vector<std::thread> m_clientThreads;
    mutable std::mutex m_threadMutex;
};

} // namespace sim
} // namespace mp2310

#endif // MP2310_TCP_SERVER_H
