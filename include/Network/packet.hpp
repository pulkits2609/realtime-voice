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
        
        const std::string& GetMessage() const;
        
        std::vector<std::uint8_t> Serialize() const;
        
        bool Deserialize(
            const std::vector<std::uint8_t>& data
        );
};

