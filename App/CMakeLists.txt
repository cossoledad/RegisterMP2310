# App - MP2310 通信主程序 (Dear ImGui GUI)

# 收集 imgui 源文件
set(IMGUI_DIR ${THIRD_PARTY_DIR}/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp
)

# 收集 App 源文件
set(APP_SOURCES
    main.cpp
    src/tcp_client.cpp
)

# 创建可执行文件
add_executable(mp2310_app WIN32
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
)

# 包含目录
target_include_directories(mp2310_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/common
)

# 链接库
target_link_libraries(mp2310_app PRIVATE
    memobus_protocol
    ws2_32
    opengl32
    gdi32
    dwmapi
)
