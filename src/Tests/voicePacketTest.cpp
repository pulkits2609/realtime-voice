#include <iostream>
#include <vector>
#include "Network/packet.hpp"
#include "Common/debugLog.hpp"

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

    voicePacket.SetSequenceNumber(12345);

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

    if(receivedPacket.GetSequenceNumber() != 12345){
        std::cerr<<"Sequence Number mismatch\n";

        return 1;
    }

    DEBUG_LOG("Sequence Number : "<<receivedPacket.GetSequenceNumber()<<"\n");

    if(receivedPacket.GetPayload() != opusData)
    {
        std::cerr
            << "Payload mismatch\n";

        return 1;
    }

    DEBUG_LOG("Voice packet test passed\n");

    DEBUG_LOG(
        "Payload size: "
        << receivedPacket.GetPayload().size()
        << " bytes\n");

    return 0;
}