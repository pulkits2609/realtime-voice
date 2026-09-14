#pragma once

#include <boost/asio.hpp>

class Server{
    private:
        boost::asio::io_context ioContext;
        boost::asio::ip::udp::socket socket;
        unsigned short port;

        void HandleMessage(); //handle the messages being sent by the client
    
    public:
        explicit Server(
            unsigned short port
        );
        void Run();

};