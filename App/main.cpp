// MP2310 Communication Program
// Extended MEMOBUS protocol over TCP
// Reference: VC++ sample from MP2310 Ethernet Communication Manual

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <memory>
#include <ctime>
#include <thread>
#include <chrono>
#include <algorithm>

#include "../Third-party/imgui/imgui.h"
#include "../Third-party/imgui/backends/imgui_impl_win32.h"
#include "../Third-party/imgui/backends/imgui_impl_opengl2.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "src/tcp_client.h"

// ============================================================
// Application State
// ============================================================
struct AppState {
    // Connection settings
    char pcIp[64] = "127.0.0.1";
    int  pcPort = 16002;
    char mpIp[64] = "127.0.0.1";
    int  mpPort = 16002;

    bool connected = false;
    bool autoReadRunning = false;

    // Register monitor: MW00000 ~ MW00009 (10 registers)
    static constexpr int REG_COUNT = 10;
    uint16_t registers[REG_COUNT] = {};

    // Editable register values (for writing)
    int writeAddr = 0;
    int writeValue = 0;

    // Stats
    uint32_t sentCount = 0;
    uint32_t recvCount = 0;
    uint32_t errorCount = 0;

    // Client
    std::unique_ptr<mp2310::TcpClient> client;

    // Auto-read timer thread
    std::thread timerThread;
    bool timerRunning = false;
};

static AppState g_appState;
static HGLRC g_glRC = nullptr;
static HDC g_hDC = nullptr;

// ============================================================
// Callbacks
// ============================================================
static void onClientData(const std::vector<uint8_t>& data) {
    if (data.size() >= mp2310::TOTAL_HEADER_SIZE &&
        data[0] == static_cast<uint8_t>(mp2310::DataType::Response)) {
        // 仅当是读响应(SFC=0x09)时才更新寄存器显示
        // 写响应(SFC=0x0B/0x0C)不包含可显示的寄存器数据
        if (data[15] == static_cast<uint8_t>(mp2310::SFC::ReadHoldingRegisters)) {
            auto regs = mp2310::Util::extractRegisters(data);
            if (!regs.empty()) {
                for (size_t i = 0; i < regs.size() && i < AppState::REG_COUNT; ++i) {
                    g_appState.registers[i] = regs[i];
                }
            }
        }
    }
}

static void onClientError(const std::string& err) {
    OutputDebugStringA(err.c_str());
    OutputDebugStringA("\n");
}

static void onClientConnect(bool connected) {
    g_appState.connected = connected;
    if (!connected) {
        // 连接断开时自动停止定时器，重置状态
        g_appState.autoReadRunning = false;
        g_appState.timerRunning = false;
        // 清零寄存器显示
        memset(g_appState.registers, 0, sizeof(g_appState.registers));
    }
}

// ============================================================
// Auto-read timer (100ms cycle, matching the document example)
// ============================================================
static void autoReadThread() {
    while (g_appState.timerRunning && g_appState.connected) {
        if (g_appState.client) {
            g_appState.client->sendReadCommand(0x0000, AppState::REG_COUNT);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static void startAutoRead() {
    if (g_appState.autoReadRunning) return;
    g_appState.autoReadRunning = true;
    g_appState.timerRunning = true;
    g_appState.timerThread = std::thread(autoReadThread);
}

static void stopAutoRead() {
    g_appState.autoReadRunning = false;
    g_appState.timerRunning = false;
    if (g_appState.timerThread.joinable()) {
        g_appState.timerThread.join();
    }
}

// ============================================================
// ImGui UI
// ============================================================
static void ShowConnectionPanel() {
    ImGui::Begin("Connection Settings");

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "PC Side");
    ImGui::InputText("PC IP", g_appState.pcIp, sizeof(g_appState.pcIp));
    ImGui::InputInt("PC Port", &g_appState.pcPort);

    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "MP2310 (Controller) Side");
    ImGui::InputText("MP IP", g_appState.mpIp, sizeof(g_appState.mpIp));
    ImGui::InputInt("MP Port", &g_appState.mpPort);

    ImGui::Separator();

    if (!g_appState.connected) {
        if (ImGui::Button("TCP/UDP Open", ImVec2(140, 0))) {
            g_appState.client = std::make_unique<mp2310::TcpClient>();
            g_appState.client->setOnData(onClientData);
            g_appState.client->setOnError(onClientError);
            g_appState.client->setOnConnect(onClientConnect);

            if (g_appState.client->connect(g_appState.mpIp,
                                            static_cast<uint16_t>(g_appState.mpPort))) {
                g_appState.connected = true;
            } else {
                g_appState.client.reset();
            }
        }
    } else {
        if (ImGui::Button("Close", ImVec2(140, 0))) {
            stopAutoRead();
            if (g_appState.client) g_appState.client->disconnect();
            g_appState.connected = false;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0,1,0,1), "Connected");
    }

    ImGui::SameLine();
    ImGui::Text("TX:%d RX:%d ERR:%d",
               g_appState.sentCount, g_appState.recvCount, g_appState.errorCount);

    ImGui::End();
}

static void ShowRegisterMonitor() {
    ImGui::Begin("Register Monitor (MW00000 ~ MW00009)");

    // Start/Stop buttons (matching the document's Start/Stop)
    if (g_appState.connected) {
        if (!g_appState.autoReadRunning) {
            if (ImGui::Button("Start (100ms cycle)", ImVec2(180, 0))) {
                startAutoRead();
            }
        } else {
            if (ImGui::Button("Stop", ImVec2(180, 0))) {
                stopAutoRead();
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(g_appState.autoReadRunning ? ImVec4(0,1,0,1) : ImVec4(1,1,0,1),
                           g_appState.autoReadRunning ? "Reading..." : "Stopped");
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Start (100ms cycle)", ImVec2(180, 0));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("Connect first");
    }

    ImGui::Separator();

    // Table
    const float COL_WIDTH = 140.0f;
    ImGui::Columns(3, "RegMonitor", false);
    ImGui::Text("Address"); ImGui::NextColumn();
    ImGui::Text("Value (DEC)"); ImGui::NextColumn();
    ImGui::Text("Value (HEX)"); ImGui::NextColumn();
    ImGui::Separator();

    for (int i = 0; i < AppState::REG_COUNT; ++i) {
        char addrStr[16];
        snprintf(addrStr, sizeof(addrStr), "MW%05d", i);
        ImGui::Text("%s", addrStr); ImGui::NextColumn();
        ImGui::Text("%5d", g_appState.registers[i]); ImGui::NextColumn();
        ImGui::Text("0x%04X", g_appState.registers[i]); ImGui::NextColumn();
    }

    ImGui::Columns(1);
    ImGui::Separator();

    // Write section
    ImGui::Text("Write Register:");
    ImGui::PushItemWidth(120);
    ImGui::InputInt("Address (MW)", &g_appState.writeAddr);
    ImGui::SameLine();
    ImGui::InputInt("Value", &g_appState.writeValue);
    ImGui::PopItemWidth();

    if (!g_appState.connected) ImGui::BeginDisabled();
    if (ImGui::Button("Write", ImVec2(100, 0))) {
        uint16_t addr = static_cast<uint16_t>(std::clamp(g_appState.writeAddr, 0, 65535));
        uint16_t value = static_cast<uint16_t>(std::clamp(g_appState.writeValue, 0, 65535));
        if (g_appState.client) {
            g_appState.client->sendWriteSingleCommand(addr, value);
        }
    }
    if (!g_appState.connected) ImGui::EndDisabled();

    ImGui::End();
}

static void ShowStatusBar() {
    ImGui::BeginMainMenuBar();
    ImGui::Text("MP2310 Communication - Extended MEMOBUS Protocol");
    if (g_appState.connected) {
        ImGui::Text(" | Connected to %s:%d", g_appState.mpIp, g_appState.mpPort);
    } else {
        ImGui::TextColored(ImVec4(1,0,0,1), " | Not connected");
    }
    ImGui::EndMainMenuBar();
}

// ============================================================
// Win32 Message Handler
// ============================================================
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_glRC && wParam != SIZE_MINIMIZED) {
            RECT rect; GetClientRect(hWnd, &rect);
            glViewport(0, 0, rect.right - rect.left, rect.bottom - rect.top);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// WinMain
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const char* CLASS_NAME = "MP2310AppWindow";
    WNDCLASS wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClass(&wc)) {
        MessageBox(nullptr, "Window registration failed", "Error", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowEx(
        0, CLASS_NAME, "MP2310 Communication - Extended MEMOBUS Protocol",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) {
        MessageBox(nullptr, "Window creation failed", "Error", MB_ICONERROR);
        return 1;
    }

    // OpenGL init
    g_hDC = GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pixelFormat = ChoosePixelFormat(g_hDC, &pfd);
    SetPixelFormat(g_hDC, pixelFormat, &pfd);
    g_glRC = wglCreateContext(g_hDC);
    wglMakeCurrent(g_hDC, g_glRC);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplOpenGL2_Init();
    io.Fonts->Build();

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Main loop
    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        if (g_appState.client) {
            g_appState.sentCount = g_appState.client->getSentCount();
            g_appState.recvCount = g_appState.client->getReceivedCount();
            g_appState.errorCount = g_appState.client->getErrorCount();
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ShowStatusBar();
        ShowConnectionPanel();
        ShowRegisterMonitor();

        ImGui::Render();
        glViewport(0, 0,
                   static_cast<int>(io.DisplaySize.x),
                   static_cast<int>(io.DisplaySize.y));
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_hDC);
        Sleep(10);
    }

    // Cleanup
    stopAutoRead();
    if (g_appState.client) g_appState.client->disconnect();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_glRC);
    ReleaseDC(hWnd, g_hDC);
    DestroyWindow(hWnd);
    return 0;
}
