#include "spi_mock.h"
#include <iostream>

class SpiMock : public SpiInterface {
public:
    void send(const std::vector<uint8_t>& data) override {
        std::cout << "[SPI MOCK] Sent " << data.size() << " bytes\n";
    }
};
