#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class PacketType : std::uint8_t{
    Text = 1,
    Voice = 2
};

class Packet{
    private:    
        PacketType type;
        std::string message;
        std::vector<std::uint8_t> payload;
        std::uint32_t sequenceNumber = 0;
        std::uint32_t clientId = 0;
    
    public:
        Packet();
        Packet(
            PacketType type,
            const std::string& message
        ); //this constructor only accepts binary data

        Packet(
            PacketType type,
            const std::vector<std::uint8_t> &payload
        );
        //helper function and constructor to take audio sample payload
        const std::vector<std::uint8_t>& GetPayload() const;


        PacketType GetType() const;
        
        const std::string& GetTextMessage() const; //GetMessage() clashes with Windows API
        
        std::vector<std::uint8_t> Serialize() const;
        
        bool Deserialize(
            const std::vector<std::uint8_t>& data
        );

        void SetSequenceNumber(
            std::uint32_t sequenceNumber
        );

        std::uint32_t GetSequenceNumber() const;

        void SetClientId(
            std::uint32_t clientId
        );

        std::uint32_t GetClientId() const;
};

