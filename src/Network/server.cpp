#include "Network/server.hpp"
#include <iostream>

Server::Server(
    unsigned short port
):
socket(ioContext),
port(port){

}

void Server::HandleMessage(){
    std::array<char,1024> buffer{};

    boost::asio::ip::udp::endpoint clientEndpoint;

    const std::size_t bytesReceived = socket.receive_from(
        boost::asio::buffer(buffer),
        clientEndpoint
    );

    const std::string message(
        buffer.data(),
        bytesReceived
    );

    std::cout<<"Message Received from client : "<<message<<"\n";

    const std::string response = "Hello from Server";
    socket.send_to(
        boost::asio::buffer(buffer),
        clientEndpoint
    );
}

void Server::Run(){
    socket.open(
        boost::asio::ip::udp::v4()
    );

    socket.bind(
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(),
            port
        )
    );

    std::cout<<"Server Started on UDP Port : "<<port<<"\n";

    HandleMessage(); //right now this is only going to handle a single message !
}