#include "src/tcp_server.h"
#include "../App/src/tcp_client.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

std::vector<uint8_t> receiveFrame(SOCKET socket) {
    std::vector<uint8_t> response;
    while (response.size() < mp2310::HEADER_218_SIZE) {
        uint8_t buffer[64];
        const int count = recv(socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
        assert(count > 0);
        response.insert(response.end(), buffer, buffer + count);
    }
    const size_t frameSize = response[6] | (static_cast<size_t>(response[7]) << 8);
    while (response.size() < frameSize) {
        uint8_t buffer[64];
        const int count = recv(socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
        assert(count > 0);
        response.insert(response.end(), buffer, buffer + count);
    }
    response.resize(frameSize);
    return response;
}

} // namespace

int main() {
    using namespace mp2310;

    constexpr uint16_t port = 26002;
    sim::TcpServer server;
    server.setRegister(0, 0x1234);
    assert(server.start(port));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify that the simulator reassembles a command split across TCP packets.
    SOCKET rawSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(rawSocket != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    assert(connect(rawSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    const auto fragmented = FrameBuilder::buildReadCommand(0x37, 0, 10);
    assert(send(rawSocket, reinterpret_cast<const char*>(fragmented.data()), 7, 0) == 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(send(rawSocket, reinterpret_cast<const char*>(fragmented.data() + 7), 15, 0) == 15);
    const auto fragmentedResponse = receiveFrame(rawSocket);
    assert(fragmentedResponse.size() == 40);
    assert(fragmentedResponse[18] == 10 && fragmentedResponse[19] == 0);
    closesocket(rawSocket);

    // Verify the production client, response validator, and register extraction together.
    TcpClient client;
    assert(client.connect("127.0.0.1", port));
    const auto request = FrameBuilder::buildReadCommand(0x44, 0, 10);
    std::vector<uint8_t> response;
    assert(client.sendAndWait(request, response, 1000));
    const auto registers = Util::extractRegisters(response);
    assert(registers.size() == 10 && registers[0] == 0x1234);

    // MW00000..MW00009 are plain holding registers in the supplied example.
    // Writing MW00001 must not implicitly toggle a status bit in MW00000.
    auto write0 = FrameBuilder::buildWriteSingleCommand(0x45, 0, 100);
    assert(client.sendAndWait(write0, response, 1000));
    auto write1 = FrameBuilder::buildWriteSingleCommand(0x46, 1, 1);
    assert(client.sendAndWait(write1, response, 1000));
    const auto verifyRequest = FrameBuilder::buildReadCommand(0x47, 0, 2);
    assert(client.sendAndWait(verifyRequest, response, 1000));
    const auto verified = Util::extractRegisters(response);
    assert(verified.size() == 2);
    assert(verified[0] == 100);
    assert(verified[1] == 1);

    client.disconnect();

    server.stop();
    std::cout << "tcp_integration_tests passed\n";
    return 0;
}
