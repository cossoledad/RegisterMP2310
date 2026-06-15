#ifndef MP2310_PROTOCOL_H
#define MP2310_PROTOCOL_H

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <iomanip>

#include <winsock2.h>

// ============================================================
// MP2310 扩展MEMOBUS 协议定义
// ============================================================
// 根据手册: 扩展MEMOBUS协议使用以下帧结构:
//
// 218报头 (12字节) + Length (2字节) + MEMOBUS数据部 (可变)
//
// 218报头:
//   [0]   数据类别: 0x11=指令, 0x19=响应
//   [1]   序列号 (每次发送递增)
//   [2]   发送目标通道编号 (PLC侧, 固定0x00)
//   [3]   发送源通道编号 (PC侧, 固定0x00)
//   [4-5] 预约 (0x00)
//   [6-7] DATAi: 总数据数 (218报头起始至MEMOBUS数据末尾) L/H
//   [8-11] 预约 (0x00)
//
// MEMOBUS数据部:
//   [12-13] MDATAi: MEMOBUS数据长度 (MFC至数据末尾) L/H
//   [14]   MFC: 固定0x20
//   [15]   SFC: 子功能码
//   [16]   CPU编号 (高4位)
//   [17]   Spare (0x00)
//   [18-19] Adr: 引用编号 (起始地址) L/H
//   [20-21] DataNum: 寄存器数 L/H
//   [22+]  寄存器数据 (仅在写入指令或响应中包含)
// ============================================================

namespace mp2310 {

// ========== 协议常量 ==========
constexpr int HEADER_218_SIZE = 12;     // 218报头长度
constexpr int MEMOBUS_LENGTH_SIZE = 2;  // MEMOBUS数据长度字段
constexpr int MEMOBUS_DATA_OFFSET = HEADER_218_SIZE + MEMOBUS_LENGTH_SIZE;  // =14
constexpr int MEMOBUS_FIXED_SIZE = 8;   // MEMOBUS数据固定部分 (MFC~DataNum)
constexpr int TOTAL_HEADER_SIZE = MEMOBUS_DATA_OFFSET + MEMOBUS_FIXED_SIZE; // =22
constexpr int DATA_SIZE = 4096;         // 最大缓冲区

// ========== 数据类别 ==========
enum class DataType : uint8_t {
    Command  = 0x11,  // 扩展MEMOBUS指令
    Response = 0x19,  // 扩展MEMOBUS响应
};

// ========== 子功能码 (SFC) ==========
enum class SFC : uint8_t {
    ReadHoldingRegisters    = 0x09,  // 读取保持寄存器 (MW)
    WriteSingleRegister     = 0x0B,  // 写入单寄存器 (MW)
    WriteMultipleRegisters  = 0x0C,  // 写入多寄存器 (MW)
};

// ========== MFC (固定值) ==========
constexpr uint8_t MFC_VALUE = 0x20;

// ========== MP2310 寄存器地址 (MW) ==========
namespace RegisterAddress {
    // MW00000 ~ MW00009 (手册示例使用)
    constexpr uint16_t MW00000 = 0x0000;  // 状态字
    constexpr uint16_t MW00001 = 0x0001;  // 控制字
    constexpr uint16_t MW00002 = 0x0002;  // 运行模式
    constexpr uint16_t MW00003 = 0x0003;  // 报警代码
    constexpr uint16_t MW00004 = 0x0004;  // 用户数据1
    constexpr uint16_t MW00005 = 0x0005;  // 用户数据2
    constexpr uint16_t MW00006 = 0x0006;  // 用户数据3
    constexpr uint16_t MW00007 = 0x0007;  // 用户数据4
    constexpr uint16_t MW00008 = 0x0008;  // 用户数据5
    constexpr uint16_t MW00009 = 0x0009;  // 用户数据6

    // 位置相关 (扩展定义)
    constexpr uint16_t TARGET_POSITION   = 0x0100;  // 目标位置 (32位)
    constexpr uint16_t CURRENT_POSITION  = 0x0102;  // 当前位置 (32位)
    constexpr uint16_t TARGET_SPEED      = 0x0104;  // 目标速度 (32位)
    constexpr uint16_t CURRENT_SPEED     = 0x0106;  // 当前速度 (32位)
    constexpr uint16_t ACCEL_TIME        = 0x0108;  // 加速时间
    constexpr uint16_t DECEL_TIME        = 0x0109;  // 减速时间

    // M 寄存器范围
    constexpr uint16_t MW_BASE           = 0x0000;
    constexpr uint16_t MW_END            = 0x1FFF;
}

// ========== 扩展MEMOBUS 帧 ==========
#pragma pack(push, 1)
struct ExtendedMemobusFrame {
    // ---- 218报头 (12字节) ----
    uint8_t  dataType;       // [0]  0x11=指令, 0x19=响应
    uint8_t  serialNo;       // [1]  序列号
    uint8_t  dstChannel;     // [2]  发送目标通道编号
    uint8_t  srcChannel;     // [3]  发送源通道编号
    uint8_t  reserved1;      // [4]  预约
    uint8_t  reserved2;      // [5]  预约
    uint16_t totalLength;    // [6-7] 总数据数 (218报头至MEMOBUS末尾) LE格式
    uint8_t  reserved3;      // [8]  预约
    uint8_t  reserved4;      // [9]  预约
    uint8_t  reserved5;      // [10] 预约
    uint8_t  reserved6;      // [11] 预约

    // ---- MEMOBUS数据部 ----
    uint16_t memobusLength;  // [12-13] MEMOBUS数据长度 (MFC至末尾) LE格式
    uint8_t  mfc;            // [14] MFC (固定0x20)
    uint8_t  sfc;            // [15] 子功能码
    uint8_t  cpuNum;         // [16] CPU编号 (高4位)
    uint8_t  spare;          // [17] Spare
    uint16_t refAddr;        // [18-19] 引用编号 (起始地址) LE格式
    uint16_t regCount;       // [20-21] 寄存器数 LE格式
    // [22+] 寄存器数据 (可变)

    ExtendedMemobusFrame() {
        memset(this, 0, sizeof(*this));
        dataType = static_cast<uint8_t>(DataType::Command);
        mfc = MFC_VALUE;
    }

    // 设置总数据数 (调用此函数后 data 部分需已填充)
    void updateLength(uint16_t dataBytes = 0) {
        // 总数据数 = 218报头(12) + MEMOBUS长度(2) + MEMOBUS数据(8 + dataBytes)
        memobusLength = MEMOBUS_FIXED_SIZE + dataBytes;
        totalLength = HEADER_218_SIZE + MEMOBUS_LENGTH_SIZE + memobusLength;
    }

    // 从字节数组解析 (直接内存映射)
    static ExtendedMemobusFrame fromBytes(const uint8_t* data, size_t len) {
        ExtendedMemobusFrame frame;
        if (len < TOTAL_HEADER_SIZE) return frame;
        memcpy(&frame, data, sizeof(ExtendedMemobusFrame));
        // 注意: totalLength, memobusLength, refAddr, regCount 是LE格式
        return frame;
    }

    // 获取寄存器数据指针
    const uint8_t* regData() const {
        return reinterpret_cast<const uint8_t*>(this) + TOTAL_HEADER_SIZE;
    }
    uint8_t* regData() {
        return reinterpret_cast<uint8_t*>(this) + TOTAL_HEADER_SIZE;
    }

    // 获取总帧大小
    size_t frameSize() const {
        return totalLength; // totalLength已经是LE格式的总长度
    }
};
#pragma pack(pop)

// ========== 帧构建器 ==========
class FrameBuilder {
public:
    // 构建读取保持寄存器指令 (SFC=0x09)
    // DATAi = 14 + 8 = 22 (固定)
    static std::vector<uint8_t> buildReadCommand(uint8_t serial, uint16_t addr, uint16_t count) {
        std::vector<uint8_t> buf(DATA_SIZE, 0);
        // 218报头
        buf[0] = static_cast<uint8_t>(DataType::Command);  // 0x11
        buf[1] = serial;                                    // 序列号
        // buf[2..5] = 0
        uint16_t totalLen = 14 + 8;  // 读取指令固定22字节
        buf[6] = static_cast<uint8_t>(totalLen & 0xFF);        // L
        buf[7] = static_cast<uint8_t>((totalLen >> 8) & 0xFF); // H
        // buf[8..11] = 0

        // MEMOBUS数据部
        uint16_t mDataLen = 8;  // 读取指令固定
        buf[12] = static_cast<uint8_t>(mDataLen & 0xFF);
        buf[13] = static_cast<uint8_t>((mDataLen >> 8) & 0xFF);
        buf[14] = MFC_VALUE;     // MFC = 0x20
        buf[15] = static_cast<uint8_t>(SFC::ReadHoldingRegisters);  // SFC = 0x09
        buf[16] = 0x10;          // CPU编号 = 1 (高4位)
        buf[17] = 0x00;          // Spare
        buf[18] = static_cast<uint8_t>(addr & 0xFF);
        buf[19] = static_cast<uint8_t>((addr >> 8) & 0xFF);
        buf[20] = static_cast<uint8_t>(count & 0xFF);
        buf[21] = static_cast<uint8_t>((count >> 8) & 0xFF);

        buf.resize(totalLen);
        return buf;
    }

    // 构建写入单寄存器指令 (SFC=0x0B)
    static std::vector<uint8_t> buildWriteSingleCommand(uint8_t serial, uint16_t addr, uint16_t value) {
        std::vector<uint8_t> buf(DATA_SIZE, 0);
        uint16_t totalLen = 14 + 10;  // 写入单寄存器: 8 + 2 = 10字节MEMOBUS数据
        buf[0] = static_cast<uint8_t>(DataType::Command);
        buf[1] = serial;
        buf[6] = static_cast<uint8_t>(totalLen & 0xFF);
        buf[7] = static_cast<uint8_t>((totalLen >> 8) & 0xFF);

        uint16_t mDataLen = 10;
        buf[12] = static_cast<uint8_t>(mDataLen & 0xFF);
        buf[13] = static_cast<uint8_t>((mDataLen >> 8) & 0xFF);
        buf[14] = MFC_VALUE;
        buf[15] = static_cast<uint8_t>(SFC::WriteSingleRegister);  // SFC = 0x0B
        buf[16] = 0x10;
        buf[17] = 0x00;
        buf[18] = static_cast<uint8_t>(addr & 0xFF);
        buf[19] = static_cast<uint8_t>((addr >> 8) & 0xFF);
        buf[20] = 0x01;  // 数量=1
        buf[21] = 0x00;
        buf[22] = static_cast<uint8_t>(value & 0xFF);
        buf[23] = static_cast<uint8_t>((value >> 8) & 0xFF);

        buf.resize(totalLen);
        return buf;
    }

    // 构建写入多寄存器指令 (SFC=0x0C)
    static std::vector<uint8_t> buildWriteMultiCommand(uint8_t serial, uint16_t addr,
                                                        uint16_t count, const std::vector<uint16_t>& values) {
        std::vector<uint8_t> buf(DATA_SIZE, 0);
        uint16_t dataBytes = count * 2;
        uint16_t mDataLen = MEMOBUS_FIXED_SIZE + dataBytes;
        uint16_t totalLen = HEADER_218_SIZE + MEMOBUS_LENGTH_SIZE + mDataLen;

        buf[0] = static_cast<uint8_t>(DataType::Command);
        buf[1] = serial;
        buf[6] = static_cast<uint8_t>(totalLen & 0xFF);
        buf[7] = static_cast<uint8_t>((totalLen >> 8) & 0xFF);

        buf[12] = static_cast<uint8_t>(mDataLen & 0xFF);
        buf[13] = static_cast<uint8_t>((mDataLen >> 8) & 0xFF);
        buf[14] = MFC_VALUE;
        buf[15] = static_cast<uint8_t>(SFC::WriteMultipleRegisters);  // SFC = 0x0C
        buf[16] = 0x10;
        buf[17] = 0x00;
        buf[18] = static_cast<uint8_t>(addr & 0xFF);
        buf[19] = static_cast<uint8_t>((addr >> 8) & 0xFF);
        buf[20] = static_cast<uint8_t>(count & 0xFF);
        buf[21] = static_cast<uint8_t>((count >> 8) & 0xFF);

        for (uint16_t i = 0; i < count && i < values.size(); ++i) {
            buf[22 + i * 2]     = static_cast<uint8_t>(values[i] & 0xFF);
            buf[22 + i * 2 + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
        }

        buf.resize(totalLen);
        return buf;
    }
};

// ========== 响应验证 ==========
class ResponseValidator {
public:
    // 验证响应数据，返回0=成功，负值=错误码
    static int validate(int rlen, const std::vector<uint8_t>& sbuf, const std::vector<uint8_t>& rbuf) {
        if (rlen < TOTAL_HEADER_SIZE) return -3;

        // 检查数据包类型
        if (rbuf[0] != static_cast<uint8_t>(DataType::Response)) return -4;  // 非响应

        // 检查序列号
        if (sbuf[1] != rbuf[1]) return -5;  // 序列号不一致

        // 检查SFC
        if (rbuf[15] != sbuf[15]) return -8;  // SFC不一致

        // 检查MFC
        if (rbuf[14] != MFC_VALUE) return -7;

        // 检查寄存器数 (仅对读取响应)
        uint8_t sfc = sbuf[15];
        if (sfc == static_cast<uint8_t>(SFC::ReadHoldingRegisters)) {
            uint16_t reqCount = sbuf[20] | (sbuf[21] << 8);
            uint16_t rspCount = rbuf[20] | (rbuf[21] << 8);  // [20-21] 是数量字段
            if (rspCount != reqCount) return -9;

            // 检查总长度: 20 + DataNum*2
            int expectedLen = 20 + reqCount * 2;
            if (rlen != expectedLen) return -3;
        }

        return 0;  // OK
    }
};

// ========== 工具函数 ==========
namespace Util {
    inline std::string bytesToHex(const uint8_t* data, size_t len) {
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0');
        for (size_t i = 0; i < len; ++i) {
            oss << std::setw(2) << static_cast<int>(data[i]) << " ";
        }
        return oss.str();
    }

    inline std::string bytesToHex(const std::vector<uint8_t>& data) {
        return bytesToHex(data.data(), data.size());
    }

    // 从响应中提取寄存器值
    inline std::vector<uint16_t> extractRegisters(const std::vector<uint8_t>& rbuf) {
        std::vector<uint16_t> regs;
        if (rbuf.size() < TOTAL_HEADER_SIZE + 2) return regs;
        // 注意: MEMOBUS帧中 [18-19]=地址, [20-21]=寄存器数量 (LE格式)
        uint16_t count = rbuf[20] | (rbuf[21] << 8);
        uint16_t addr  = rbuf[18] | (rbuf[19] << 8);  // 起始地址
        // 只有读响应(SFC=0x09)才包含寄存器数据
        // 数据从 TOTAL_HEADER_SIZE(=22) 开始, 每寄存器2字节 LE格式
        for (uint16_t i = 0; i < count && (TOTAL_HEADER_SIZE + i * 2 + 1) < rbuf.size(); ++i) {
            uint16_t val = rbuf[TOTAL_HEADER_SIZE + i * 2] |
                          (rbuf[TOTAL_HEADER_SIZE + i * 2 + 1] << 8);
            regs.push_back(val);
        }
        return regs;
    }

    // CRC-16 (Modbus) 计算 (用于串口模式)
    uint16_t crc16(const uint8_t* data, size_t len);
}

} // namespace mp2310

#endif // MP2310_PROTOCOL_H
