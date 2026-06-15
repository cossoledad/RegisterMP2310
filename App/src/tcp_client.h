#ifndef MP2310_TCP_CLIENT_H
#define MP2310_TCP_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>
#include "../../common/protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace mp2310 {

// TCP 客户端回调
using OnDataCallback = std::function<void(const std::vector<uint8_t>&)>;
using OnErrorCallback = std::function<void(const std::string&)>;
using OnConnectCallback = std::function<void(bool)>;

class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    // 连接到 MP2310 设备
    bool connect(const std::string& host, uint16_t port = 16002);
    
    // 断开连接
    void disconnect();
    
    // 是否已连接
    bool isConnected() const { return m_connected; }

    // 发送原始帧 (直接发送字节数据)
    bool sendFrame(const std::vector<uint8_t>& frame);
    
    // 便捷方法: 发送读取命令 (自动管理序列号)
    bool sendReadCommand(uint16_t addr, uint16_t count);
    
    // 便捷方法: 发送写入单寄存器命令
    bool sendWriteSingleCommand(uint16_t addr, uint16_t value);
    
    // 便捷方法: 发送写入多寄存器命令
    bool sendWriteMultiCommand(uint16_t addr, const std::vector<uint16_t>& values);

    // 同步请求-响应 (超时毫秒), 返回完整响应帧
    bool sendAndWait(const std::vector<uint8_t>& frame, std::vector<uint8_t>& response, int timeoutMs = 1000);
    
    // 获取当前序列号
    uint8_t getSerial() const { return m_serial; }

    // 设置回调
    void setOnData(OnDataCallback cb) { m_onData = cb; }
    void setOnError(OnErrorCallback cb) { m_onError = cb; }
    void setOnConnect(OnConnectCallback cb) { m_onConnect = cb; }

    // 获取统计信息
    uint32_t getSentCount() const { return m_sentCount; }
    uint32_t getReceivedCount() const { return m_recvCount; }
    uint32_t getErrorCount() const { return m_errorCount; }

private:
    void receiveThread();

    std::string m_host;
    uint16_t m_port;

    SOCKET m_socket;

    std::atomic<bool> m_connected{false};
    std::atomic<uint8_t> m_serial{0};    // 序列号 (0x00~0xFF 循环)
    std::atomic<uint32_t> m_sentCount{0};
    std::atomic<uint32_t> m_recvCount{0};
    std::atomic<uint32_t> m_errorCount{0};

    std::thread m_recvThread;
    mutable std::mutex m_mutex;

    OnDataCallback m_onData;
    OnErrorCallback m_onError;
    OnConnectCallback m_onConnect;

    // 同步请求-响应支持
    struct PendingRequest {
        uint8_t serial;
        std::vector<uint8_t> response;
        bool completed{false};
    };
    std::vector<PendingRequest> m_pendingRequests;
};

} // namespace mp2310

#endif // MP2310_TCP_CLIENT_H
