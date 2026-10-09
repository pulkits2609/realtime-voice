#include "Network/server.hpp"
#include <iostream>
#include "Network/packet.hpp"
#include "Common/debugLog.hpp"

Server::Server(
    unsigned short port
):
socket(io_context),
port(port),
controlServer(
    io_context,
    port
){
    controlServer.SetClientConnectedCallback(
        [this](
            std::uint32_t clientId,
            const std::string& clientName
        ){

            HandleClientConnected(
                clientId,
                clientName
            );
        }
    );

    controlServer.SetClientDisconnectedCallback(
        [this](
            std::uint32_t clientId
        ){

            HandleClientDisconnected(
                clientId
            );
        }
    );
}

void Server::HandleMessage(){

    boost::asio::ip::udp::endpoint clientEndpoint;

    //server now receives bytes
    const std::vector<std::uint8_t> data =
        socket.ReceiveFrom(
            clientEndpoint
        );

    Packet packet;

    if(!packet.Deserialize(data)){

        DEBUG_LOG(
            "Received Invalid Packet\n"
        );

        return;
    }

    if(packet.GetType() == PacketType::Voice){

        const std::uint32_t clientId =
            packet.GetClientId();

        //associate this UDP endpoint with
        //the client identity received over TCP
        if(!RegisterUdpClient(
            clientId,
            clientEndpoint
        )){

            DEBUG_LOG(
                "Received Voice Packet from Unknown Client ID : "
                <<clientId
                <<"\n"
            );

            return;
        }

        DEBUG_LOG(
            "Received Voice Packet from Client "
            <<clientId
            <<" : "
            <<packet.GetPayload().size()
            <<" bytes\n"
        );

        RelayVoicePacket(
            data,
            clientId
        );

        return;
    }

    if(packet.GetType() == PacketType::Text){

        DEBUG_LOG(
            "Client Message : "
            <<packet.GetTextMessage()
            <<"\n"
        );
    }
}

void Server::Run(){
    socket.Open();

    socket.Bind(port);

    controlServer.Start();

    DEBUG_LOG("Server Started on UDP Port : "<<port<<"\n");

    while(true){
        HandleMessage(); //now the server continuously receives packets
    }
}

//this function sends the exact same voice packet to every client ecxept the sender

void Server::RelayVoicePacket(
    const std::vector<std::uint8_t>& data,
    std::uint32_t senderClientId
){

    std::vector<
        boost::asio::ip::udp::endpoint
    > recipients;

    {
        std::lock_guard<std::mutex> lock(
            clientsMutex
        );

        for(const auto& client : clients){

            if(
                client.clientId ==
                senderClientId
            ){
                continue;
            }

            if(!client.udpRegistered){
                continue;
            }

            recipients.push_back(
                client.endpoint
            );
        }
    }

    //send outside the mutex
    for(const auto& endpoint : recipients){

        socket.SendTo(
            data,
            endpoint
        );
    }

    DEBUG_LOG(
        "Relayed Voice Packet to : "
        <<recipients.size()
        <<" clients\n"
    );
}

void Server::HandleClientConnected(
    std::uint32_t clientId,
    const std::string& clientName
){

    std::lock_guard<std::mutex> lock(
        clientsMutex
    );

    clients.push_back(
        ClientInfo{
            clientId,
            clientName,
            boost::asio::ip::udp::endpoint(),
            false
        }
    );

    DEBUG_LOG(
        "Registered Client : "
        <<clientName
        <<" | ID : "
        <<clientId
        <<"\n"
    );
}

void Server::HandleClientDisconnected(
    std::uint32_t clientId
){
    std::lock_guard<std::mutex> lock(
        clientsMutex
    );

    for(std::size_t i = 0; i < clients.size(); i++){
        if(clients[i].clientId == clientId){
            DEBUG_LOG(
                "Removed Client : "
                <<clientId
                <<"\n"
            );

            clients.erase(
                clients.begin() + i
            );

            return;
        }
    }
}

bool Server::RegisterUdpClient(
    std::uint32_t clientId,
    const boost::asio::ip::udp::endpoint& endpoint
){

    std::lock_guard<std::mutex> lock(
        clientsMutex
    );

    for(auto& client : clients){

        if(client.clientId == clientId){

            client.endpoint = endpoint;
            client.udpRegistered = true;
            return true;
        }
    }
    return false;
}