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

    if(packet.GetType() == PacketType::Voice){
        //if this is a new client, add it to the list

        if(!IsClientAlreadyConnected(clientEndpoint)){
            clients.push_back(clientEndpoint);
            
            std::cout<<"New Client Connected : "<<clientEndpoint.address().to_string()<<":"<<clientEndpoint.port()<<"\n";
        }

        std::cout<<"Received Voice Packet : "<<packet.GetPayload().size()<<" bytes\n";
        //voice doesnt require any decoding because server doesnt care, it just acts as a mediator for transmission

        //send this voice packet to every other client
        RelayVoicePacket(
            data,clientEndpoint
        );

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

    while(true){
        HandleMessage(); //now the server continuously receives packets
    }
}

//check if the clients is aready present in the client list

bool Server::IsClientAlreadyConnected(
    const boost::asio::ip::udp::endpoint& clientEndpoint
){
    return std::find(
        clients.begin(),
        clients.end(),
        clientEndpoint
    ) != clients.end();
}

//this function sends the exact same voice packet to every client ecxept the sender

void Server::RelayVoicePacket(
    const std::vector<std::uint8_t> &data,
    const boost::asio::ip::udp::endpoint& sender
){
    int clientsReached = 0;

    for(int i=0; i<clients.size(); i++){
        //we dont send packet back to the person who sent it 
        if(clients[i] == sender){
            continue;
        }

        socket.SendTo(
            data,clients[i]
        );

        clientsReached++;
        
    }

    std::cout<<"Relayed Voice Packet to : "<<clientsReached<<" clients\n";
}
