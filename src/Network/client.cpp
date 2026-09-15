#include "Network/client.hpp"
#include <iostream>

Client::Client(
    const std::string& serverAddress,
    unsigned short serverPort
):
socket(io_context),
serverEndpoint(
    boost::asio::ip::make_address(serverAddress),
    serverPort
){

}

void Client::SendMessage(const std::string& message){
    socket.SendTo(message, serverEndpoint);
}

std::string Client::ReceiveMessage(){
    return socket.ReceiveFrom(serverEndpoint);
}

void Client::Run(){
    socket.Open();
   
    std::cout<<"Client Connected\n";
    std::cout<<"Sending Message to server\n";
    SendMessage(
        "Hello From Client"
    );
    std::cout<<"Waiting for server response : \n";
    const std::string response = ReceiveMessage();

    std::cout<<"Server Response : \n"<<response<<"\n";
}

