#pragma once

#include "Network/packet.hpp"

// Protocol v2 wraps the existing voice packet with routing context.
// IDs and revisions are public identifiers, not authentication credentials.
class RoutedPacket : public Packet{
    private:
        std::uint32_t group = 0, channel = 0, senderRevision = 0, recipientRevision = 0;
        std::uint64_t instance = 0;

    public:
        using Packet::Packet;
        static constexpr std::size_t HeaderSize = 41;
        void SetContext(std::uint64_t server, std::uint32_t groupId, std::uint32_t channelId,
                        std::uint32_t sendRevision, std::uint32_t receiveRevision = 0){
            instance = server; group = groupId; channel = channelId;
            senderRevision = sendRevision; recipientRevision = receiveRevision;
        }
        std::uint64_t GetInstance() const { return instance; }
        std::uint32_t GetGroupId() const { return group; }
        std::uint32_t GetChannelId() const { return channel; }
        std::uint32_t GetSendRevision() const { return senderRevision; }
        std::uint32_t GetReceiveRevision() const { return recipientRevision; }

        std::vector<std::uint8_t> Serialize() const{
            const auto voice = Packet::Serialize();
            if(GetType() != PacketType::Voice || voice.size() > 996) return {};
            std::vector<std::uint8_t> data(28, 0);
            data[0] = 'R'; data[1] = 'V'; data[2] = 2;
            const auto put = [&](unsigned offset, std::uint64_t value, unsigned count){
                for(unsigned i = 0; i < count; i++){
                    data[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
                }
            };
            put(4, group, 4); put(8, channel, 4);
            put(12, senderRevision, 4); put(16, recipientRevision, 4);
            put(20, instance, 8);
            data.insert(data.end(), voice.begin(), voice.end());
            return data;
        }

        bool Deserialize(const std::vector<std::uint8_t>& data){
            if(data.size() < HeaderSize || data.size() > 1024 ||
               data[0] != 'R' || data[1] != 'V' || data[2] != 2 || data[3] != 0){
                return false;
            }
            const auto get = [&](unsigned offset, unsigned count){
                std::uint64_t value = 0;
                for(unsigned i = 0; i < count; i++){
                    value |= static_cast<std::uint64_t>(data[offset + i]) << (8 * i);
                }
                return value;
            };
            const std::vector<std::uint8_t> voice(data.begin() + 28, data.end());
            if(!Packet::Deserialize(voice) || GetType() != PacketType::Voice ||
               Packet::Serialize().size() != voice.size()) return false;
            group = static_cast<std::uint32_t>(get(4, 4));
            channel = static_cast<std::uint32_t>(get(8, 4));
            senderRevision = static_cast<std::uint32_t>(get(12, 4));
            recipientRevision = static_cast<std::uint32_t>(get(16, 4));
            instance = get(20, 8);
            return true;
        }
};

