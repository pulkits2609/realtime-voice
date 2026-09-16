#include "Network/client.hpp"
#include <iostream>
#include "Network/packet.hpp"

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
    // socket.SendTo(message, serverEndpoint);
    Packet packet(
        PacketType::Text,
        message
    );
    const std::vector<std::uint8_t> data= packet.Serialize();

    socket.SendTo(
        data,
        serverEndpoint
    );
}

std::string Client::ReceiveMessage(){
    // return socket.ReceiveFrom(serverEndpoint);

    const std::vector<std::uint8_t> data = socket.ReceiveFrom(
        serverEndpoint
    );

    Packet packet;
    if(!packet.Deserialize(data)){
        return "Invalid Packet";
    }

    return packet.GetMessage();
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

