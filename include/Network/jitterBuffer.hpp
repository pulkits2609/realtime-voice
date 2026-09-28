#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class JitterBuffer{
    private:
        std::map<
            std::uint32_t,
            std::vector<std::uint8_t>
        > packets;
        
        std::size_t maxPackets;

    public:
        explicit JitterBuffer(
            std::size_t maxPackets
        );

        bool Push(
            std::uint32_t sequenceNumber,
            const std::vector<std::uint8_t> &payload
        );

        bool Pop(
            const std::uint32_t sequenceNumber,
            std::vector<std::uint8_t>& payload
        );

        std::size_t Size() const;
};