#pragma once
#include <vector>
#include <cstdint>

class SpiInterface {
public:
    virtual ~SpiInterface() = default;
    virtual void send(const std::vector<uint8_t>& data) = 0;
};
