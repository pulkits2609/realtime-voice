#include "Network/jitterBuffer.hpp"

JitterBuffer::JitterBuffer(
    std::size_t maxPackets
): maxPackets(maxPackets){

}

bool JitterBuffer::Push(
    std::uint32_t sequenceNumber,
    const std::vector<std::uint8_t>& payload
){
    if(packets.size() >= maxPackets){
        return false;
    }
    if(packets.find(sequenceNumber) != packets.end()){
        return false;
    }

    packets[sequenceNumber] = payload;

    return true;
}

bool JitterBuffer::Pop(
    std::uint32_t sequenceNumber,
    std::vector<std::uint8_t>& payload
){
    auto packet = packets.find(
        sequenceNumber
    );

    if(packet == packets.end()){
        return false;
    }

    payload = packet->second;
    packets.erase(packet);

    return true;
}

std::size_t JitterBuffer::Size() const{
    return packets.size();
}

//instead of decoding every sequence Immediately :
//we now do : 
//5 arrives : store 5
//3 arrives : store 3
//4 arrives : store 4

//then we ask from jitterBuffer.Pop(expectedSequenceNumber, payload)
//which gives received 3 4 5, when originally 5 3 4 got delivered

void JitterBuffer::Clear(){
    packets.clear();
}

void JitterBuffer::DiscardBefore(
    std::uint32_t sequenceNumber
){
    for(auto packet = packets.begin(); packet != packets.end();){
        if(packet->first - sequenceNumber >= 0x80000000u){
            packet = packets.erase(packet);
        }
        else{
            ++packet;
        }
    }
}

bool JitterBuffer::GetNextSequence(
    std::uint32_t expectedSequenceNumber,
    std::uint32_t& sequenceNumber
) const{
    bool found = false;
    std::uint32_t nearestDistance = 0x80000000u;

    // Map order alone is incorrect when sequence numbers wrap.
    for(const auto& packet : packets){
        const std::uint32_t distance =
            packet.first - expectedSequenceNumber;

        if(distance < nearestDistance){
            nearestDistance = distance;
            sequenceNumber = packet.first;
            found = true;
        }
    }

    return found;
}