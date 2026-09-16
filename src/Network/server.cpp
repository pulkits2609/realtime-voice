#include "Network/server.hpp"
#include <iostream>
#include "Network/packet.hpp"

Server::Server(
    unsigned short port
):
socket(io_context),
port(port){

}

//using the UdpSocket Class, we have abstracted away the network specific logic
void Server::HandleMessage(){
    boost::asio::ip::udp::endpoint clientEndpoint;

    //server now receives bytes
    const std::vector<std::uint8_t> data = socket.ReceiveFrom(
        clientEndpoint
    );

    Packet packet;
    if(!packet.Deserialize(data)){
        std::cout<<"Received Invalid Packet\n";
        return;
    }

    if(packet.GetType() == PacketType::Text){
        std::cout<<"Client Message : "<<packet.GetMessage()<<"\n";
    }

    Packet response(
        PacketType::Text,
        "Hello from Server"
    );

    const std::vector<std::uint8_t> responseData = response.Serialize();

    socket.SendTo(
        responseData,
        clientEndpoint
    );
}

void Server::Run(){
    socket.Open();

    socket.Bind(port);
    std::cout<<"Server Started on UDP Port : "<<port<<"\n";

    HandleMessage();
}