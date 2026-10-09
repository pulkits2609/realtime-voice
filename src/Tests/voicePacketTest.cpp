#include <cstdint>
#include <iostream>
#include <vector>
#include "Network/packet.hpp"
#include "Common/debugLog.hpp"

int main()
{
    constexpr std::uint32_t expectedClientId = 0x12345678u;
    constexpr std::uint32_t expectedSequenceNumber = 12345;

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

    voicePacket.SetClientId(expectedClientId);
    voicePacket.SetSequenceNumber(expectedSequenceNumber);

    const std::vector<std::uint8_t> serialized =
        voicePacket.Serialize();

    if(serialized.size() != 13 + opusData.size()){
        std::cerr<<"Wrong voice packet header size\n";
        return 1;
    }

    // Fixed wire bytes also catch matching mistakes in
    // serialization and deserialization, such as swapped fields.
    std::vector<std::uint8_t> expectedWireData = {
        0x02,                   // Voice packet
        0x06, 0x00, 0x00, 0x00, // Payload length, little endian
        0x78, 0x56, 0x34, 0x12, // Client ID, little endian
        0x39, 0x30, 0x00, 0x00  // Sequence 12345, little endian
    };

    expectedWireData.insert(
        expectedWireData.end(),
        opusData.begin(),
        opusData.end()
    );

    if(serialized != expectedWireData){
        std::cerr<<"Voice packet wire format mismatch\n";
        return 1;
    }

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

    if(receivedPacket.GetClientId() != expectedClientId){
        std::cerr<<"Client ID mismatch\n";
        return 1;
    }

    if(receivedPacket.GetSequenceNumber() != expectedSequenceNumber){
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

    // Every incomplete prefix must be rejected, including
    // partial headers and partial Opus payloads.
    for(std::size_t length = 0; length < serialized.size(); length++){
        const std::vector<std::uint8_t> truncated(
            serialized.begin(),
            serialized.begin() + length
        );

        Packet truncatedPacket;

        if(truncatedPacket.Deserialize(truncated)){
            std::cerr
                <<"Accepted truncated voice packet of "
                <<length
                <<" bytes\n";

            return 1;
        }
    }

    std::cout<<"Voice packet test passed\n";

    DEBUG_LOG(
        "Payload size: "
        << receivedPacket.GetPayload().size()
        << " bytes\n");

    return 0;
}
