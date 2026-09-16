#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class PacketType : std::uint8_t{
    Text = 1
    //this can be scaled later
};

class Packet{
    private:    
        PacketType type;
        std::string message;
    
    public:
        Packet();
        Packet(
            PacketType type,
            const std::string& message
        );

        PacketType GetType() const;
        
        const std::string& GetMessage() const;
        
        std::vector<std::uint8_t> Serialize() const;
        
        bool Deserialize(
            const std::vector<std::uint8_t>& data
        );
};

