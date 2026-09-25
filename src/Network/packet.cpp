#include "Network/packet.hpp"

Packet::Packet():type(PacketType::Text){

}

Packet::Packet(PacketType type, const std::string& message):
type(type),
message(message){
    
}

Packet::Packet(PacketType type, const std::vector<std::uint8_t>& payload):
type(type),payload(payload){

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
    
    //the current serialize only takes string and puts bytes into packet
    //now we want Text Packet -> use Message
    //voice packet -> use Payload

    //therefore we create the payload first
    std::vector<std::uint8_t> payload;

    if(type == PacketType::Text){
        payload.assign(
            message.begin(),
            message.end()
        );
    }
    else if(type == PacketType::Voice){
        payload = this->payload;
    }
    
    //this returns a vector of type uint8_t
    const std::uint32_t messageLength =
    static_cast<std::uint32_t>(payload.size());

    
    std::vector<std::uint8_t> data(
        5 + messageLength
    );

    data[0] = static_cast<std::uint8_t>(type);

    data[1] = messageLength & 0xFF;
    data[2] = (messageLength >> 8) & 0xFF;
    data[3] = (messageLength >> 16) & 0xFF;
    data[4] = (messageLength >> 24) & 0xFF;

    std::copy(
        payload.begin(),
        payload.end(),
        data.begin() + 5
    );
    
    return data;
}

bool Packet::Deserialize(
    const std::vector<std::uint8_t>& data
){
    if(data.size() < 5){
        return false;
        //1st byte is type, next 4 bytes are the length
    }

    type = static_cast<PacketType>(
        data[0]
    );

    const std::uint32_t messageLen =
        static_cast<std::uint32_t>(data[1])
        |
        (static_cast<std::uint32_t>(data[2]) << 8)
        |
        (static_cast<std::uint32_t>(data[3]) << 16)
        |
        (static_cast<std::uint32_t>(data[4]) << 24);

    if(data.size() < 5 + messageLen){
        return false;
    }

    //Recover the raw payload bytes
    payload.assign(
        data.begin() + 5,
        data.begin() + 5 + messageLen
    );

    //Only Text packets need to be converted into a string
    if(type == PacketType::Text){
        message.assign(
            payload.begin(),
            payload.end()
        );
    }
    else if(type == PacketType::Voice){
        message.clear();
    }

    return true;
}


const std::vector<std::uint8_t>& Packet::GetPayload() const{
    return payload;
}
