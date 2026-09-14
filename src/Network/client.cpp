#include "Network/client.hpp"
#include <iostream>

Client::Client(
    const std::string& serverAddress,
    unsigned short serverPort
):
socket(ioContext),
serverEndpoint(boost::asio::ip::make_address(serverAddress),serverPort){

}

void Client::SendMessage(const std::string& message){
    socket.send_to(
        boost::asio::buffer(message),
        serverEndpoint
    );
}

std::string Client::ReceiveMessage(){
    std::array<char, 1024> buffer{};
    boost::asio::ip::udp::endpoint sender;

    const std::size_t bytesReceived = socket.receive_from(
        boost::asio::buffer(buffer),sender
    );

    return std::string(buffer.data(), bytesReceived);
}

void Client::Run(){
    socket.open(
        boost::asio::ip::udp::v4()
    );

    std::cout<<"Client Started\n";
    std::cout<<"Sending Message to Server\n";

    SendMessage("Hello To Server Ji");
    std::cout<<"Waiting For Server Response : \n";

    const std::string response = ReceiveMessage();

    std::cout<<"Server Message : "<<response<<"\n";
}

