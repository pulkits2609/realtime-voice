#pragma once

#include <boost/asio.hpp>
#include <string>

#include "Network/udpSocket.hpp"

class Server{
    private:
        void HandleMessage();

        bool IsClientAlreadyConnected(
            const boost::asio::ip::udp::endpoint& clientEndpoint
        );
        
        void RelayVoicePacket(
            const std::vector<std::uint8_t>& data,
            const boost::asio::ip::udp::endpoint& sender
        );

        boost::asio::io_context io_context;
        UdpSocket socket;
        unsigned short port;

        std::vector<boost::asio::ip::udp::endpoint> clients;

    public:
        explicit Server(unsigned short port);
        void Run();
};