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

const std::string& Packet::GetTextMessage() const{
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

    const std::size_t headerSize = type == PacketType::Voice ? 13 : 5;

    std::vector<std::uint8_t> data(
        headerSize + messageLength
    );

    data[0] = static_cast<std::uint8_t>(type);
    data[1] = messageLength & 0xFF;
    data[2] = (messageLength >> 8) & 0xFF;
    data[3] = (messageLength >> 16) & 0xFF;
    data[4] = (messageLength >> 24) & 0xFF;

    if(type == PacketType::Voice){

        data[5] = clientId & 0xFF;
        data[6] = (clientId >> 8) & 0xFF;
        data[7] = (clientId >> 16) & 0xFF;
        data[8] = (clientId >> 24) & 0xFF;
        data[9] = sequenceNumber & 0xFF;
        data[10] = (sequenceNumber >> 8) & 0xFF;
        data[11] = (sequenceNumber >> 16) & 0xFF;
        data[12] = (sequenceNumber >> 24) & 0xFF;

        std::copy(
            payload.begin(),
            payload.end(),
            data.begin() + 13
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
    }

    type = static_cast<PacketType>(
            data[0]
        );

    const std::uint32_t messageLength =
        static_cast<std::uint32_t>(data[1]) |
        (static_cast<std::uint32_t>(data[2]) << 8) |
        (static_cast<std::uint32_t>(data[3]) << 16) |
        (static_cast<std::uint32_t>(data[4]) << 24);

    const std::size_t headerSize = type == PacketType::Voice ? 13 : 5;

    if(data.size() < headerSize + messageLength){
        return false;
    }

    if(type == PacketType::Voice){
        clientId =
            static_cast<std::uint32_t>(data[5]) |
            (static_cast<std::uint32_t>(data[6]) << 8) |
            (static_cast<std::uint32_t>(data[7]) << 16) |
            (static_cast<std::uint32_t>(data[8]) << 24);

        sequenceNumber =
            static_cast<std::uint32_t>(data[9]) |
            (static_cast<std::uint32_t>(data[10]) << 8) |
            (static_cast<std::uint32_t>(data[11]) << 16) |
            (static_cast<std::uint32_t>(data[12]) << 24);

        payload.assign(
            data.begin() + 13,
            data.begin() + 13 + messageLength
        );

        message.clear();
    }
    else{

        payload.assign(
            data.begin() + 5,
            data.begin() + 5 + messageLength
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

void Packet::SetClientId(
    std::uint32_t clientId
){
    this->clientId = clientId;
}

std::uint32_t Packet::GetClientId() const{
    return clientId;
}