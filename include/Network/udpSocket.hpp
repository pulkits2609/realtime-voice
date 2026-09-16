#pragma once

//the main goal of creating this header is
//to reduce and remove the networking logic
//inside client and server
//its good for scaling the project, because later
//when voice gets introduced its gonna be messy

#include <cstddef>
//provides with the fundamental types and macros
//essential for low level programming, mem mnanage
//and pointer arithmetic... ( sounds geeky but it is what it is)

#include <boost/asio.hpp>
#include <vector> //for supporting packet class function calls

class UdpSocket{
    private:
        boost::asio::ip::udp::socket socket;

    public:
        explicit UdpSocket(
            boost::asio::io_context& io_context
        );

        void Open();
        
        void Bind(
            unsigned short port
        );

        void SendTo(
            // const std::string& message,
            const std::vector<std::uint8_t>& data,
            const boost::asio::ip::udp::endpoint& endpoint
        );

        std::vector<std::uint8_t> ReceiveFrom(
            boost::asio::ip::udp::endpoint& sender
        );
};