#include <iostream>
#include <vector>
#include "Network/packet.hpp"

int main()
{
    std::vector<std::uint8_t> opusData =
    {
        0x00,
        0x01,
        0xFF,
        0x7A,
        0x00,
        0x42
    };

    Packet voicePacket(
        PacketType::Voice,
        opusData
    );

    const std::vector<std::uint8_t> serialized =
        voicePacket.Serialize();

    Packet receivedPacket;

    if(!receivedPacket.Deserialize(serialized))
    {
        std::cerr
            << "Voice packet deserialization failed\n";

        return 1;
    }

    if(receivedPacket.GetType() != PacketType::Voice)
    {
        std::cerr
            << "Wrong packet type\n";

        return 1;
    }

    if(receivedPacket.GetPayload() != opusData)
    {
        std::cerr
            << "Payload mismatch\n";

        return 1;
    }

    std::cout
        << "Voice packet test passed\n";

    std::cout
        << "Payload size: "
        << receivedPacket.GetPayload().size()
        << " bytes\n";

    return 0;
}