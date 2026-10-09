#include "Network/server.hpp"
#include <iostream>
#include "Network/packet.hpp"
#include "Common/debugLog.hpp"
#include <csignal>
#include <chrono>
#include <thread>

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

        if(packet.GetPayload().empty()){
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
    running = true;
    io_context.restart();

    boost::asio::signal_set signals(
        io_context,
        SIGINT,
        SIGTERM
    );

    signals.async_wait(
        [this](const boost::system::error_code& error, int){
            if(!error){
                running = false;
            }
        }
    );

    try{
        socket.Open();
        socket.Bind(port);
        socket.SetNonBlocking(true);

        controlServer.Start();

        while(running){
            io_context.poll();

            if(!running){
                break;
            }

            try{
                HandleMessage();
            }
            catch(const boost::system::system_error& error){
                const auto code = error.code();

                if(
                    code == boost::asio::error::would_block ||
                    code == boost::asio::error::try_again
                ){
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(2)
                    );

                    continue;
                }

                if(
                    code == boost::asio::error::message_size ||
                    code == boost::asio::error::connection_reset
                ){
                    continue;
                }

                throw;
            }
        }
    }
    catch(...){
        running = false;

        controlServer.Stop();
        socket.Close();

        throw;
    }

    controlServer.Stop();
    socket.Close();
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
        std::lock_guard<std::mutex> lock(clientsMutex);

        bool senderActive = false;

        for(const auto& client : clients){
            if(
                client.clientId == senderClientId &&
                client.udpRegistered
            ){
                senderActive = true;
                break;
            }
        }

        // The TCP handler may have removed the sender
        // after its UDP packet was initially validated.
        if(!senderActive){
            return;
        }

        for(const auto& client : clients){
            if(
                client.clientId != senderClientId &&
                client.udpRegistered
            ){
                recipients.push_back(client.endpoint);
            }
        }
    }

    for(const auto& endpoint : recipients){
        try{
            socket.SendTo(data, endpoint);
        }
        catch(const boost::system::system_error& error){
            DEBUG_LOG(
                "UDP relay failed: "
                <<error.what()
                <<"\n"
            );
        }
    }
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
    std::lock_guard<std::mutex> lock(clientsMutex);

    for(auto& client : clients){
        if(client.clientId != clientId){
            continue;
        }

        if(client.udpRegistered){
            // Keep this session bound to its original endpoint.
            return client.endpoint == endpoint;
        }

        client.endpoint = endpoint;
        client.udpRegistered = true;

        return true;
    }

    return false;
}