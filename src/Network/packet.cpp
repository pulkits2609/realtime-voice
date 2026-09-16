#include "Network/packet.hpp"

Packet::Packet():type(PacketType::Text){

}

Packet::Packet(PacketType type, const std::string& message):
type(type),
message(message){
    
}

PacketType Packet::GetType() const{
    return type;
}

const std::string& Packet::GetMessage() const{
    return message;
}

//serialization
//we will be converting the packet object into bytes
//because UDP doesnt understand C++ Objects, only bytes

//Byte 0 : Packet Type
//Bytes 1-4 Message Length
//Bytes 5+ Message

std::vector<std::uint8_t> Packet::Serialize() const{
    //this returns a vector of type uint8_t
    const std::uint32_t messageLength = static_cast<std::uint32_t>(message.size());

    
    std::vector<std::uint8_t> data;
    //first block is reserved for the packet type
    //then comes the messageLength, then the actual message
    data.reserve(
        1+sizeof(messageLength)+
        message.size()
    );

    data.push_back(
        static_cast<std::uint8_t>(type)
    );

    //now when pushing the messageLength
    //we cannot just push it directly because our messageLength is in form of uint32_t
    //we need to break it into 4 parts

    //0xFF means : Keep only the lowest 8 bits
    data.push_back(
        static_cast<std::uint8_t>(
            messageLength & 0xFF //this gives the last 8 bits
        )
    );
    data.push_back(
        static_cast<std::uint8_t>(
            (messageLength >> 8) & 0xFF // >> 8 shifts the last 8 bits moving them out, so now we have next 8 bits in last position, subsequently this is done for 16 and 32, so when pushing all of them, we have stored out uint32_t as 4 blocks of uint8_t
            //this is also called as little endien encoding XD
        )
    );
    data.push_back(
        static_cast<std::uint8_t>(
            (messageLength >> 16) & 0xFF
        )
    );
    data.push_back(
        static_cast<std::uint8_t>(
            (messageLength >> 24) & 0xFF
        )
    );

    //now the actual message
    for(std::size_t i=0;i<message.size();i++){
        data.push_back(
            static_cast<std::uint8_t>(message[i])
        );
    }
    
    return data;
}

bool Packet::Deserialize(
    const std::vector<std::uint8_t>& data
){
    if(data.size() < 5){
        return false;
        //1st byte is type, next 4 bytes are the length, if the packet lacks this minimum requirement then immediately discard
    }

    type = static_cast<PacketType>(
        data[0]
    );

    const std::uint32_t messageLen = static_cast<std::uint32_t>(data[1])
    |
    (static_cast<std::uint32_t>(data[2])<<8)
    |
    (static_cast<std::uint32_t>(data[3])<<16)
    |
    (static_cast<std::uint32_t>(data[4])<<24);

    // the bitwise operator | is being used here to combine the bits
    //for eg:
    //   00001111
    // | 11110000
    //   -----------
    //   11111111

    if(data.size() < 5+messageLen){
        return false;
    }

    message.clear();
    message.reserve(messageLen);

    for(std::uint32_t i=0; i<messageLen; i++){
        message.push_back(static_cast<char>(data[5+i]));
    }

    return true;
}

