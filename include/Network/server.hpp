#pragma once

#include <boost/asio.hpp>
#include <string>

#include "Network/udpSocket.hpp"

#include "Network/tcpControl.hpp"
#include <cstdint>
#include <mutex>
#include <vector>

class Server{
    private:
        void HandleMessage();
        
        void RelayVoicePacket(
            const std::vector<std::uint8_t>& data,
            std::uint32_t senderClientId
        );

        boost::asio::io_context io_context;
        UdpSocket socket;
        unsigned short port;

        struct ClientInfo{
            std::uint32_t clientId;
            std::string clientName;
            boost::asio::ip::udp::endpoint endpoint;
            bool udpRegistered = false;
        };

        std::vector<ClientInfo> clients;

        std::mutex clientsMutex;

        TcpControlServer controlServer;

        void HandleClientConnected(
            std::uint32_t clientId,
            const std::string& clientName
        );

        void HandleClientDisconnected(
            std::uint32_t clientId
        );

        bool RegisterUdpClient(
            std::uint32_t clientId,
            const boost::asio::ip::udp::endpoint& endpoint
        );

    public:
        explicit Server(unsigned short port);
        void Run();
};