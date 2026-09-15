#include "Network/server.hpp"
#include <iostream>

Server::Server(
    unsigned short port
):
socket(io_context),
port(port){

}

//using the UdpSocket Class, we have abstracted away the network specific logic
void Server::HandleMessage(){
    boost::asio::ip::udp::endpoint clientEndpoint;

    const std::string message = socket.ReceiveFrom(
        clientEndpoint
    );

    std::cout<<"Client Message : "<<message<<"\n";

    const std::string response = "Hello from server";

    socket.SendTo(
        response, clientEndpoint
    );
}

void Server::Run(){
    socket.Open();

    socket.Bind(port);
    std::cout<<"Server Started on UDP Port : "<<port<<"\n";

    HandleMessage();
}