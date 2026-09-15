#pragma once

#include <boost/asio.hpp>
#include <string>

#include "Network/udpSocket.hpp"

class Server{
    private:
        void HandleMessage();

        boost::asio::io_context io_context;
        UdpSocket socket;
        unsigned short port;

    public:
        explicit Server(unsigned short port);
        void Run();
};