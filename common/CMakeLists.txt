# common - 共享的MEMOBUS协议库
add_library(memobus_protocol STATIC
    protocol.h
    protocol.cpp
)

target_include_directories(memobus_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Winsock dependency
if(WIN32)
    target_link_libraries(memobus_protocol PUBLIC ws2_32)
endif()
