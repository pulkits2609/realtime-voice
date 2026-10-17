#pragma once

#include "Network/protocol.hpp"
#include "Network/routedPacket.hpp"
#include <boost/asio.hpp>
#include <functional>
#include <map>
#include <mutex>
#include <chrono>

// This is routing state, not authentication. All mutations use the same mutex.
class VoiceGroups{
    public:
        using Send = std::function<void(std::uint32_t, const Json&)>;
        using Close = std::function<void(std::uint32_t)>;
        using Relay = std::function<void(const RoutedPacket&, const boost::asio::ip::udp::endpoint&)>;

        VoiceGroups(Send send, Close close);
        Json Open(std::uint32_t sessionId, const Json& hello);
        Json Request(std::uint32_t sessionId, const Json& request);
        Json Operator(const Json& request);
        void Remove(std::uint32_t sessionId, bool heartbeatExpired = false);
        void Tick();
        void Route(RoutedPacket packet, const boost::asio::ip::udp::endpoint& endpoint, const Relay& relay);
        std::string Instance() const;
        std::uint64_t InstanceNumber() const;

    private:
        struct Session{
            std::string name;
            bool manager = false;
            Json intent = Json::object();
            std::uint32_t group = 0, team = 0, transmit = 0;
            std::uint32_t sendRevision = 1, receiveRevision = 1;
            bool microphone = true, all = true, teamMuted = true;
            bool udpRegistered = false;
            boost::asio::ip::udp::endpoint endpoint;
        };

        struct Group{
            std::uint32_t id = 0, owner = 0, all = 0;
            std::string name, type, phase = "waiting";
            std::vector<std::uint32_t> teams;
        };

        std::map<std::uint32_t, Session> sessions;
        std::map<std::uint32_t, Group> groups;
        // Logical ownership outlives a TCP session during the recovery window.
        std::uint32_t manager = 0, managerId = 0, nextManager = 1;
        std::uint32_t nextGroup = 1, nextChannel = 1;
        using Clock = std::chrono::steady_clock;
        Clock::time_point recoveryDeadline = Clock::time_point::max();
        std::uint64_t instance;
        std::string instanceText;
        mutable std::mutex mutex;
        Send send;
        Close close;

        Json Execute(std::uint32_t caller, const Json& request, bool local);
        Group& FindGroup(std::uint32_t id);
        void RequireOwner(const Group& group, std::uint32_t caller, bool local);
        void Join(std::uint32_t id, Group& group, std::uint32_t team, std::uint32_t channel);
        void Delete(std::uint32_t id, const std::string& reason);
        void Changed(std::uint32_t group);
        Json Status(std::uint32_t id) const;
        Json Details(const Group& group) const;
        void ExpireManager(Clock::time_point now);
        void DeleteOwnedGroups(const std::string& reason);
        friend struct ManagerRecoveryTest;
};
