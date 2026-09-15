#pragma once

#include<boost/asio.hpp>
#include<string>

#include "Network/udpSocket.hpp"

class Client{
    private:
        boost::asio::io_context io_context;

        UdpSocket socket;

        boost::asio::ip::udp::endpoint serverEndpoint;

        void SendMessage(
            const std::string& message
        );
        std::string ReceiveMessage();
    
    public:
        Client(
            const std::string& serverAddress,
            unsigned short serverPort
        );

        void Run();
};