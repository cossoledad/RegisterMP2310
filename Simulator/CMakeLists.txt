# Simulator - MP2310 设备模拟器

set(SIM_SOURCES
    main.cpp
    src/tcp_server.cpp
)

add_executable(mp2310_simulator
    ${SIM_SOURCES}
)

target_include_directories(mp2310_simulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(mp2310_simulator PRIVATE
    memobus_protocol
    ws2_32
)
