#include "Network/tcpControl.hpp"

#include "Common/debugLog.hpp"

#include <iostream>
#include <vector>

namespace{

    //3 helper functions

    //takes uint32_t number and puts its 4 bytes into a byte vector
    void WriteUint32(
        std::vector<std::uint8_t>& data,
        std::uint32_t value
    ){
        data.push_back(
            static_cast<std::uint8_t>(
                (value >> 24) & 0xFF
            )
        );
        data.push_back(
            static_cast<std::uint8_t>(
                (value >> 16) & 0xFF
            )
        );
        data.push_back(
            static_cast<std::uint8_t>(
                (value >> 8) & 0xFF
            )
        );
        data.push_back(
            static_cast<std::uint8_t>(
                value & 0xFF
            )
        );
    }

    //does opposite by taking 4 bytes from vector and rebuilds uint32_t
    std::uint32_t ReadUint32(
        const std::vector<std::uint8_t> &data,
        std::size_t offset
    ){
        return
        (static_cast<std::uint32_t>(data[offset]) << 24) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
        static_cast<std::uint32_t>(data[offset + 3]);
    }

    //this creates a packet [message type][client name bytes]
    //[Join][P][u][l][k][i][t]
    std::vector<std::uint8_t> CreateJoinMessage(
        const std::string& clientName
    ){
        std::vector<std::uint8_t> payload;

        payload.push_back(
            static_cast<std::uint8_t>(
                ControlMessageType::Join
            )
        );

        payload.insert(
            payload.end(),
            clientName.begin(),
            clientName.end()
        );

        return payload;
    }
}

TcpControlClient::TcpControlClient(
    boost::asio::io_context& io_context
):socket(io_context){
    
}

void TcpControlClient::SendFrame(
    const std::vector<std::uint8_t>& payload
){
    std::vector<std::uint8_t> frame;

    WriteUint32(
        frame,
        static_cast<std::uint32_t>(
            payload.size()
        )
    );

    frame.insert(
        frame.end(),
        payload.begin(),
        payload.end()
    );

    boost::asio::write(
        socket,
        boost::asio::buffer(frame)
    );
}

std::vector<std::uint8_t> TcpControlClient::ReceiveFrame(){
    std::array<std::uint8_t, 4> sizeBuffer{};

    boost::asio::read(
        socket,
        boost::asio::buffer(sizeBuffer)
    );

    const std::uint32_t size = 
        (static_cast<std::uint32_t>(sizeBuffer[0]) << 24) |
        (static_cast<std::uint32_t>(sizeBuffer[1]) << 16) |
        (static_cast<std::uint32_t>(sizeBuffer[2]) << 8) |
        static_cast<std::uint32_t>(sizeBuffer[3]);
    
    if(size == 0 || size > 4096){
        throw std::runtime_error(
            "Invalid TCP Control Frame Size"
        );
    }

    std::vector<std::uint8_t> payload(
        size
    );

    boost::asio::read(
        socket,
        boost::asio::buffer(payload)
    );

    return payload;
}

bool TcpControlClient::Connect(
    const std::string& serverAddress,
    unsigned short serverPort,
    const std::string& clientName
){
    try{
        boost::asio::ip::tcp::resolver resolver(
            socket.get_executor()
        );

        const auto endpoints = resolver.resolve(
            serverAddress,
            std::to_string(serverPort)
        );

        boost::asio::connect(
            socket,
            endpoints
        );

        SendFrame(
            CreateJoinMessage(
                clientName
            )
        );

        const std::vector<std::uint8_t> response = ReceiveFrame();

        if(response.size() != 5 || response[0] != static_cast<std::uint8_t>(
            ControlMessageType::JoinAccepted
        )){
            return false;
        }

        clientId = ReadUint32(
            response,
            1
        );

        return true;
    }

    catch(const std::exception& e){
        std::cerr<<"TCP Connection Failed : "<<e.what()<<"\n";

        return false;
    }
}

void TcpControlClient::StartReceive(
    std::function<void(
        ControlMessageType,
        std::uint32_t,
        const std::string&
    )> callback
){
    std::thread(
        [this,callback](){
            try{
                while(true){
                    const std::vector<std::uint8_t> data = ReceiveFrame();

                    if(data.empty()){
                        continue;
                    }

                    const auto type = static_cast<ControlMessageType>(
                        data[0]
                    );

                    if(type == ControlMessageType::ClientConnected){
                        if(data.size() < 5){
                            continue;
                        }

                        const std::uint32_t remoteClientId = ReadUint32(
                            data,
                            1
                        );

                        const std::string name(
                            data.begin() + 5,
                            data.end()
                        );

                        callback(
                            type,
                            remoteClientId,
                            name
                        );
                    }
                    else if(
                        type == ControlMessageType::ClientDisconnected
                    ){
                        if(data.size() < 5){
                            continue;
                        }

                        const std::uint32_t remoteClientId = ReadUint32(
                            data,
                            1
                        );

                        callback(
                            type,
                            remoteClientId,
                            ""
                        );
                    }
                }
            }
            catch(const std::exception& e){
                std::cerr<<"TCP Control Conection Lost : "<<e.what()<<"\n";
            }
        }
    ).detach();
}

std::uint32_t TcpControlClient::GetClientId() const{
    return clientId;
}

// Server Implementation

TcpControlServer::TcpControlServer(
    boost::asio::io_context& io_context,
    unsigned short port
): io_context(io_context),
acceptor(
    io_context,
    boost::asio::ip::tcp::endpoint(
        boost::asio::ip::tcp::v4(),
        port
    )
){

}

std::vector<std::uint8_t> TcpControlServer::ReceiveFrame(
    boost::asio::ip::tcp::socket& socket
){
    std::array<std::uint8_t, 4> sizeBuffer{};

    boost::asio::read(
        socket,
        boost::asio::buffer(sizeBuffer)
    );

    const std::uint32_t size = 
        (static_cast<std::uint32_t>(sizeBuffer[0]) << 24) |
        (static_cast<std::uint32_t>(sizeBuffer[1]) << 16) |
        (static_cast<std::uint32_t>(sizeBuffer[2]) << 8) |
        static_cast<std::uint32_t>(sizeBuffer[3]);

    if(size == 0 || size > 4096){
        throw std::runtime_error(
            "Invalid TCP Control Frame Size"
        );
    }

    std::vector<std::uint8_t> payload(
        size
    );

    boost::asio::read(
        socket,
        boost::asio::buffer(payload)
    );

    return payload;
}

void TcpControlServer::SendFrame(
    boost::asio::ip::tcp::socket& socket,
    const std::vector<std::uint8_t>& payload
){
    std::vector<std::uint8_t> frame;

    WriteUint32(
        frame,
        static_cast<std::uint32_t>(
            payload.size()
        )
    );

    frame.insert(
        frame.end(),
        payload.begin(),
        payload.end()
    );

    boost::asio::write(
        socket,
        boost::asio::buffer(frame)
    );
}

void TcpControlServer::AcceptLoop(){
    while(running){
        auto socket = std::make_shared<
        boost::asio::ip::tcp::socket
        >(
            io_context
        );

        try{
            acceptor.accept(
                *socket
            );
            std::thread(
                &TcpControlServer::HandleClient,
                this,
                socket
            ).detach();
        }
        catch(const std::exception& e){
            if(running){
                std::cerr<<"TCP Accept failed : "<<e.what()<<"\n";
            }
        }
    }
}

void TcpControlServer::HandleClient(
    std::shared_ptr<
        boost::asio::ip::tcp::socket
    > socket
){

    std::uint32_t clientId = 0;

    try{

        const std::vector<std::uint8_t> joinData =
            ReceiveFrame(
                *socket
            );

        if(
            joinData.empty() ||
            joinData[0] !=
                static_cast<std::uint8_t>(
                    ControlMessageType::Join
                )
        ){

            return;
        }

        const std::string clientName(
            joinData.begin() + 1,
            joinData.end()
        );

        clientId =
            nextClientId++;

        {
            std::lock_guard<std::mutex> lock(
                clientsMutex
            );

            clients.emplace(
                clientId,
                ClientConnection{
                    clientId,
                    clientName,
                    socket
                }
            );
        }

        //tell Server that a new identity
        //has been created
        if(clientConnectedCallback){

            clientConnectedCallback(
                clientId,
                clientName
            );
        }

        std::cout
            <<"New Client Connected : "
            <<clientName
            <<" | ID : "
            <<clientId
            <<"\n";

        //tell client its assigned ID
        std::vector<std::uint8_t> acceptedMessage;

        acceptedMessage.push_back(
            static_cast<std::uint8_t>(
                ControlMessageType::JoinAccepted
            )
        );

        WriteUint32(
            acceptedMessage,
            clientId
        );

        SendFrame(
            *socket,
            acceptedMessage
        );

        //tell all existing clients about
        //the newly connected client
        std::vector<std::uint8_t> connectedMessage;

        connectedMessage.push_back(
            static_cast<std::uint8_t>(
                ControlMessageType::ClientConnected
            )
        );

        WriteUint32(
            connectedMessage,
            clientId
        );

        connectedMessage.insert(
            connectedMessage.end(),
            clientName.begin(),
            clientName.end()
        );

        Broadcast(
            connectedMessage,
            clientId
        );

        //keep TCP connection alive
        //until the client disconnects
        while(true){

            ReceiveFrame(
                *socket
            );
        }
    }

    catch(const std::exception&){

        if(clientId == 0){
            return;
        }

        {
            std::lock_guard<std::mutex> lock(
                clientsMutex
            );

            clients.erase(
                clientId
            );
        }

        //tell Server to remove all state
        //belonging to this client
        if(clientDisconnectedCallback){

            clientDisconnectedCallback(
                clientId
            );
        }

        std::cout
            <<"Client Disconnected | ID : "
            <<clientId
            <<"\n";

        //tell the remaining clients
        std::vector<std::uint8_t> disconnectedMessage;

        disconnectedMessage.push_back(
            static_cast<std::uint8_t>(
                ControlMessageType::ClientDisconnected
            )
        );

        WriteUint32(
            disconnectedMessage,
            clientId
        );

        Broadcast(
            disconnectedMessage,
            clientId
        );
    }
}

void TcpControlServer::Broadcast(
    const std::vector<std::uint8_t>& payload,
    std::uint32_t excludedClientId
){

    std::lock_guard<std::mutex> lock(
        clientsMutex
    );

    for(auto& [
        clientId,
        client
    ] : clients){

        if(clientId == excludedClientId){
            continue;
        }

        try{

            SendFrame(
                *client.socket,
                payload
            );
        }
        catch(const std::exception& e){

            DEBUG_LOG(
                "TCP Broadcast Failed for Client : "
                <<clientId
                <<" : "
                <<e.what()
                <<"\n"
            );
        }
    }
}

void TcpControlServer::Start(){
    running = true;

    acceptThread = std::thread(
        &TcpControlServer::AcceptLoop,
        this
    );

    std::cout<<"TCP Control Server Started\n";
}

void TcpControlServer::SetClientConnectedCallback(
    ClientConnectedCallback callback
){

    clientConnectedCallback =
        std::move(callback);
}

void TcpControlServer::SetClientDisconnectedCallback(
    ClientDisconnectedCallback callback
){

    clientDisconnectedCallback =
        std::move(callback);
}