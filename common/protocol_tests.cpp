#include "protocol.h"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace mp2310;

    const auto request = FrameBuilder::buildReadCommand(0x2A, 0, 10);
    assert(request.size() == 22);
    assert(request[0] == 0x11 && request[1] == 0x2A);
    assert(request[6] == 22 && request[7] == 0);
    assert(request[12] == 8 && request[13] == 0);
    assert(request[14] == 0x20 && request[15] == 0x09);
    assert(request[16] == 0x10);
    assert(request[18] == 0 && request[19] == 0);
    assert(request[20] == 10 && request[21] == 0);

    // SFC=0x09 response layout from the supplied MP2310 example:
    // count at [18..19], register values starting at [20].
    std::vector<uint8_t> response(40, 0);
    response[0] = 0x19;
    response[1] = 0x2A;
    response[6] = 40;
    response[12] = 26;
    response[14] = 0x20;
    response[15] = 0x09;
    response[16] = 0x10;
    response[18] = 10;
    for (uint16_t i = 0; i < 10; ++i) {
        response[20 + i * 2] = static_cast<uint8_t>(i);
    }

    assert(ResponseValidator::validate(static_cast<int>(response.size()), request, response) == 0);
    const auto registers = Util::extractRegisters(response);
    assert(registers.size() == 10);
    for (uint16_t i = 0; i < 10; ++i) assert(registers[i] == i);

    response[18] = 9;
    assert(ResponseValidator::validate(static_cast<int>(response.size()), request, response) == -9);

    std::cout << "protocol_tests passed\n";
    return 0;
}
