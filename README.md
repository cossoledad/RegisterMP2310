# MP2310 Extended MEMOBUS 通信示例

这是一个纯 Windows 的现代 C++23 示例项目。它使用 Winsock、Dear ImGui 和
OpenGL2，实现所给 `Ethernet通信说明.txt` 中的核心流程：

- TCP 连接控制器；
- 每 100 ms 发送一次扩展 MEMOBUS `SFC=0x09` 指令；
- 读取并显示 `MW00000` 到 `MW00009`；
- 无真机时，通过本地模拟器验证通信流程。

## 项目结构

- `App/`：Win32 + Dear ImGui 客户端。
- `Simulator/`：MP2310 TCP 模拟器。
- `common/`：扩展 MEMOBUS 帧构建、验证和解析。
- `Ethernet通信说明.txt`：本项目当前采用的协议依据。
- `Third-party/imgui/`：Dear ImGui 源码。

## 构建

环境要求：

- Windows；
- CMake 3.20 或更高版本；
- 支持 C++23 的 MinGW-w64 GCC 15；
- PowerShell 和 Git（仅获取第三方依赖时需要）。

```powershell
# 仅当 Third-party/imgui 不存在时执行
.\scripts\fetch_thirdparty.ps1

cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j
ctest --test-dir build --output-on-failure
```

生成文件位于 `build/bin/`：

- `mp2310_simulator.exe`
- `mp2310_app.exe`

## 本地演示

1. 启动 `build\bin\mp2310_simulator.exe`。
2. 启动 `build\bin\mp2310_app.exe`。
3. 使用默认目标 `127.0.0.1:16002`，点击 **TCP Open**。
4. 点击 **Start (100ms cycle)**。
5. 在模拟器中执行 `set 0 1234`，观察 App 中 `MW00000` 更新。

模拟器命令可通过 `help` 查看；地址和值按十六进制输入。
`MW00000..MW00009` 在模拟器中是相互独立的普通保持寄存器，不附加状态字或控制字语义。

## 协议符合性边界

已按所给说明片段实现并测试：

- 扩展 MEMOBUS 218 报头；
- TCP 传输；
- CPU1、`MFC=0x20`、`SFC=0x09`；
- `MW00000..MW00009` 的 100 ms 周期读取；
- 读取响应中数量位于 `[18..19]`、数据始于 `[20]`；
- TCP 拆包、粘包和部分发送处理；
- PC 本地 IP/端口绑定；`0.0.0.0:0` 表示自动选择。

目前不能声称已通过真实 MP2310 兼容性认证，因为尚未使用真机和完整安川手册验证。
以下功能也超出所给说明片段或尚未完成：

- UDP 通信；
- 写寄存器 `SFC=0x0B/0x0C` 的真机格式、错误响应和权限验证；
- 控制器异常响应、超时重连及完整错误码展示；
- 真机联调、抓包对照和长时间稳定性测试。

写寄存器界面和模拟器实现仅用于实验，不应直接用于生产设备。
