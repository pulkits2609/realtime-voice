#pragma once

#include<boost/asio.hpp>
#include<string>

class Client{
    private:
        boost::asio::io_context ioContext;
        boost::asio::ip::udp::socket socket;
        boost::asio::ip::udp::endpoint serverEndpoint;

        void SendMessage(const std::string& message);
        std::string ReceiveMessage();

    public:
        Client(
            const std::string &serverAddress,
            unsigned short serverPort
        );

        void Run();

};

//client initializes with server address and port, it stores the created io context and socket
//and the serverEndpoint to send the message at, inside its class privately