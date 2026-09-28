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

    //Text Packet :
    //Byte 0 : Packet Type
    //Byte 1-4 : Message Length
    //Bytes 5+ : Message

    //Voice packet :
    //Byte 0 : Packet type
    //Bytes 1-4 : Payload Length
    //Bytes 5-8 : Sequence Number for ordering
    //Bytes 9+ : Opus Payload

    const std::size_t headerSize = type == PacketType::Voice ? 9 : 5;

    std::vector<std::uint8_t> data(
        headerSize + messageLength
    );

    data[0] = static_cast<std::uint8_t>(type);
    data[1] = messageLength & 0xFF;
    data[2] = (messageLength >> 8) & 0xFF;
    data[3] = (messageLength >> 16) & 0xFF;
    data[4] = (messageLength >> 24) & 0xFF;

    if(type == PacketType::Voice){
        data[5] = sequenceNumber & 0xFF;
        data[6] = (sequenceNumber >> 8) & 0xFF;
        data[7] = (sequenceNumber >> 16) & 0xFF;
        data[8] = (sequenceNumber >> 24) & 0xFF;
    
        std::copy(
            payload.begin(),
            payload.end(),
            data.begin()+9
        );
    }
    else if(type == PacketType::Text){
        std::copy(
            payload.begin(),
            payload.end(),
            data.begin()+5
        );
    }

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

    //now Voice Packets have additional 4 byte sequence Number
    const std::size_t headerSize = type == PacketType::Voice ? 9 : 5;

    if(data.size() < headerSize + messageLen){
        return false;
    }

    if(type == PacketType::Voice){
        sequenceNumber = 
            static_cast<std::uint32_t>(data[5]) |
            (static_cast<std::uint32_t>(data[6]) << 8) |
            (static_cast<std::uint32_t>(data[7]) << 16) |
            (static_cast<std::uint32_t>(data[8]) << 24);
        
        //recovering the raw Opus payload bytes
        payload.assign(
            data.begin() + 9,
            data.begin() + 9 + messageLen
        );

        message.clear();
    }
    else if(type == PacketType::Text){
        //recovering raw payload bytes
        payload.assign(
            data.begin() + 5,
            data.begin() + 5 + messageLen
        );

        message.assign(
            payload.begin(),
            payload.end()
        );
    }

    return true;
}


const std::vector<std::uint8_t>& Packet::GetPayload() const{
    return payload;
}

void Packet::SetSequenceNumber(
    std::uint32_t sequenceNumber
){
    this->sequenceNumber = sequenceNumber;
}

std::uint32_t Packet::GetSequenceNumber() const{
    return sequenceNumber;
}