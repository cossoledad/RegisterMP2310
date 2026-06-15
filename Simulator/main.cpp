// MP2310 模拟器 - 主入口
// 通过 TCP 模拟 MP2310 设备，响应扩展 MEMOBUS 协议请求

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

// 避免 winsock.h 与 winsock2.h 冲突
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "src/tcp_server.h"

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    std::cout << "\n[Simulator] Shutting down..." << std::endl;
    g_running = false;
}

void printBanner() {
    std::cout << R"(
+==========================================+
|        MP2310  Device Simulator           |
|    Extended MEMOBUS Protocol over TCP     |
|                                           |
|    Default listening port: 16002          |
|    Default target address: 127.0.0.1      |
+==========================================+
)" << std::endl;
}

void printHelp() {
    std::cout << "Commands:" << std::endl;
    std::cout << "  status         - Show device status" << std::endl;
    std::cout << "  reg <addr>     - Read register [addr: hex]" << std::endl;
    std::cout << "  set <addr> <v> - Set register [addr: hex, value: hex]" << std::endl;
    std::cout << "  alarm <0|1>    - Simulate alarm" << std::endl;
    std::cout << "  delay <ms>     - Set response delay (ms)" << std::endl;
    std::cout << "  clients        - Show client connections" << std::endl;
    std::cout << "  help           - Show help" << std::endl;
    std::cout << "  quit           - Exit" << std::endl;
}

int main(int argc, char* argv[]) {
    // 设置控制台为 UTF-8 编码以正确显示中文
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    signal(SIGINT, signalHandler);

    int port = 16002;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    printBanner();

    mp2310::sim::TcpServer server;
    if (!server.start(static_cast<uint16_t>(port))) {
        std::cerr << "[Simulator] 启动失败!" << std::endl;
        std::cerr << "提示: 端口 " << port << " 可能被占用，请使用不同端口启动:" << std::endl;
        std::cerr << "  " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    std::cout << "Type 'help' for command list" << std::endl;
    std::cout << std::endl;

    // 状态显示线程
    std::thread statusThread([&]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (server.getClientCount() > 0) {
                std::cout << "[Simulator] Clients: " << server.getClientCount() 
                          << " | Status: 0x" << std::hex << server.getRegister(0x0000)
                          << std::dec << std::endl;
            }
        }
    });

    // 命令循环
    std::string cmd;
    while (g_running) {
        std::cout << "> ";
        std::getline(std::cin, cmd);

        if (!g_running) break;

        if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            break;
        } else if (cmd == "help" || cmd == "h") {
            printHelp();
        } else if (cmd == "status" || cmd == "st") {
            std::cout << "=== MP2310 Simulator Status ===" << std::endl;
            std::cout << "Running:    " << (server.isRunning() ? "Yes" : "No") << std::endl;
            std::cout << "Port:       " << port << std::endl;
            std::cout << "Clients:    " << server.getClientCount() << std::endl;
            std::cout << "Status:     0x" << std::hex << server.getRegister(0x0000) << std::dec << std::endl;
            std::cout << "Control:    0x" << std::hex << server.getRegister(0x0001) << std::dec << std::endl;
            std::cout << "Position:   " << server.getRegister32(0x0102) << std::endl;
            std::cout << "Target:     " << server.getRegister32(0x0100) << std::endl;
            std::cout << "Speed:      " << server.getRegister32(0x0106) << std::endl;
        } else if (cmd.rfind("reg ", 0) == 0) {
            uint16_t addr = std::stoul(cmd.substr(4), nullptr, 16);
            uint16_t val = server.getRegister(addr);
            std::cout << "Reg[0x" << std::hex << addr << "] = 0x" << val << std::dec << std::endl;
        } else if (cmd.rfind("set ", 0) == 0) {
            auto space2 = cmd.find(' ', 4);
            if (space2 != std::string::npos) {
                uint16_t addr = std::stoul(cmd.substr(4, space2 - 4), nullptr, 16);
                uint16_t val = std::stoul(cmd.substr(space2 + 1), nullptr, 16);
                server.setRegister(addr, val);
                std::cout << "Set: Reg[0x" << std::hex << addr << "] = 0x" << val << std::dec << std::endl;
            }
        } else if (cmd.rfind("alarm ", 0) == 0) {
            bool on = cmd[6] == '1';
            server.setSimulateAlarm(on);
            std::cout << "Alarm simulation: " << (on ? "ON" : "OFF") << std::endl;
        } else if (cmd.rfind("delay ", 0) == 0) {
            int ms = std::stoi(cmd.substr(6));
            server.setResponseDelay(ms);
            std::cout << "Response delay: " << ms << "ms" << std::endl;
        } else if (cmd == "clients") {
            std::cout << "Active clients: " << server.getClientCount() << std::endl;
        } else if (!cmd.empty()) {
            std::cout << "Unknown command: " << cmd << std::endl;
            printHelp();
        }
    }

    g_running = false;
    server.stop();

    if (statusThread.joinable()) {
        statusThread.join();
    }

    std::cout << "[Simulator] Exited" << std::endl;
    return 0;
}
