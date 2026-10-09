#include "Network/tcpControl.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace{

    using Clock = std::chrono::steady_clock;
    using IsAlive = std::function<bool()>;
    using Tick = std::function<void()>;

    constexpr std::size_t maxFrameSize = 4096;
    constexpr std::size_t maxNameSize = 64;

    const auto pollInterval = std::chrono::milliseconds(5);
    const auto heartbeatInterval = std::chrono::seconds(2);
    const auto receiveTimeout = std::chrono::seconds(10);
    const auto sendTimeout = std::chrono::seconds(3);
    const auto connectTimeout = std::chrono::seconds(5);

    bool WouldBlock(
        const boost::system::error_code& error
    ){
        return error == boost::asio::error::would_block ||
               error == boost::asio::error::try_again;
    }

    void WriteUint32(
        std::vector<std::uint8_t>& data,
        std::uint32_t value
    ){
        data.push_back(
            static_cast<std::uint8_t>((value >> 24) & 0xFF)
        );
        data.push_back(
            static_cast<std::uint8_t>((value >> 16) & 0xFF)
        );
        data.push_back(
            static_cast<std::uint8_t>((value >> 8) & 0xFF)
        );
        data.push_back(
            static_cast<std::uint8_t>(value & 0xFF)
        );
    }

    std::uint32_t ReadUint32(
        const std::vector<std::uint8_t>& data,
        std::size_t offset
    ){
        return
            (static_cast<std::uint32_t>(data[offset]) << 24) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
            static_cast<std::uint32_t>(data[offset + 3]);
    }

    std::vector<std::uint8_t> CreateClientMessage(
        ControlMessageType type,
        std::uint32_t clientId,
        const std::string& name = ""
    ){
        std::vector<std::uint8_t> payload;

        payload.push_back(
            static_cast<std::uint8_t>(type)
        );

        WriteUint32(payload, clientId);

        payload.insert(
            payload.end(),
            name.begin(),
            name.end()
        );

        return payload;
    }

    void ReadExact(
        boost::asio::ip::tcp::socket& socket,
        std::uint8_t* output,
        std::size_t count,
        const IsAlive& alive,
        Clock::time_point deadline,
        const Tick& tick
    ){
        std::size_t received = 0;

        while(received < count){
            if(!alive()){
                throw std::runtime_error("TCP session stopped");
            }

            if(Clock::now() >= deadline){
                throw std::runtime_error("TCP receive timed out");
            }

            if(tick){
                tick();
            }

            boost::system::error_code error;

            const std::size_t bytes = socket.read_some(
                boost::asio::buffer(
                    output + received,
                    count - received
                ),
                error
            );

            if(WouldBlock(error)){
                std::this_thread::sleep_for(pollInterval);
                continue;
            }

            if(error){
                throw boost::system::system_error(error);
            }

            if(bytes == 0){
                throw std::runtime_error(
                    "TCP peer closed the connection"
                );
            }

            received += bytes;
        }
    }

    std::vector<std::uint8_t> ReadControlFrame(
        boost::asio::ip::tcp::socket& socket,
        const IsAlive& alive,
        const Tick& tick = {}
    ){
        const auto deadline = Clock::now() + receiveTimeout;

        std::array<std::uint8_t,4> header{};

        ReadExact(
            socket,
            header.data(),
            header.size(),
            alive,
            deadline,
            tick
        );

        const std::uint32_t size =
            (static_cast<std::uint32_t>(header[0]) << 24) |
            (static_cast<std::uint32_t>(header[1]) << 16) |
            (static_cast<std::uint32_t>(header[2]) << 8) |
            static_cast<std::uint32_t>(header[3]);

        if(size == 0 || size > maxFrameSize){
            throw std::runtime_error(
                "Invalid TCP control frame size"
            );
        }

        std::vector<std::uint8_t> payload(size);

        ReadExact(
            socket,
            payload.data(),
            payload.size(),
            alive,
            deadline,
            tick
        );

        return payload;
    }

    void WriteControlFrame(
        boost::asio::ip::tcp::socket& socket,
        const std::vector<std::uint8_t>& payload,
        const IsAlive& alive
    ){
        if(payload.empty() || payload.size() > maxFrameSize){
            throw std::runtime_error(
                "Invalid outgoing TCP control frame size"
            );
        }

        std::vector<std::uint8_t> frame;

        WriteUint32(
            frame,
            static_cast<std::uint32_t>(payload.size())
        );

        frame.insert(
            frame.end(),
            payload.begin(),
            payload.end()
        );

        const auto deadline = Clock::now() + sendTimeout;
        std::size_t sent = 0;

        while(sent < frame.size()){
            if(!alive()){
                throw std::runtime_error("TCP session stopped");
            }

            if(Clock::now() >= deadline){
                throw std::runtime_error("TCP send timed out");
            }

            boost::system::error_code error;

            const std::size_t bytes = socket.write_some(
                boost::asio::buffer(
                    frame.data() + sent,
                    frame.size() - sent
                ),
                error
            );

            if(WouldBlock(error)){
                std::this_thread::sleep_for(pollInterval);
                continue;
            }

            if(error){
                throw boost::system::system_error(error);
            }

            if(bytes == 0){
                throw std::runtime_error(
                    "TCP send made no progress"
                );
            }

            sent += bytes;
        }
    }
}

// Client implementation

TcpControlClient::TcpControlClient(
    boost::asio::io_context& io_context
):
io_context(io_context),
socket(io_context){

}

TcpControlClient::~TcpControlClient(){
    Disconnect();
}

void TcpControlClient::SendFrame(
    const std::vector<std::uint8_t>& payload
){
    WriteControlFrame(
        socket,
        payload,
        [this](){
            return running.load();
        }
    );
}

std::vector<std::uint8_t> TcpControlClient::ReceiveFrame(){
    return ReadControlFrame(
        socket,
        [this](){
            return running.load();
        }
    );
}

bool TcpControlClient::Connect(
    const std::string& serverAddress,
    unsigned short serverPort,
    const std::string& clientName
){
    Disconnect();

    try{
        if(clientName.empty() || clientName.size() > maxNameSize){
            throw std::runtime_error(
                "Client name must contain 1 to 64 bytes"
            );
        }

        boost::asio::ip::tcp::resolver resolver(io_context);

        const auto endpoints = resolver.resolve(
            boost::asio::ip::tcp::v4(),
            serverAddress,
            std::to_string(serverPort)
        );

        bool completed = false;
        boost::system::error_code connectionError;

        io_context.restart();

        boost::asio::async_connect(
            socket,
            endpoints,
            [&](
                const boost::system::error_code& error,
                const boost::asio::ip::tcp::endpoint&
            ){
                connectionError = error;
                completed = true;
            }
        );

        io_context.run_for(connectTimeout);

        if(!completed){
            boost::system::error_code ignored;
            socket.cancel(ignored);

            // Finish the cancelled operation before its
            // callback's local references go out of scope.
            io_context.restart();
            io_context.run();

            throw std::runtime_error(
                "TCP connection timed out"
            );
        }

        if(connectionError){
            throw boost::system::system_error(connectionError);
        }

        socket.non_blocking(true);
        running = true;

        std::vector<std::uint8_t> joinMessage{
            static_cast<std::uint8_t>(
                ControlMessageType::Join
            )
        };

        joinMessage.insert(
            joinMessage.end(),
            clientName.begin(),
            clientName.end()
        );

        SendFrame(joinMessage);

        const auto response = ReceiveFrame();

        if(
            response.size() != 5 ||
            response[0] != static_cast<std::uint8_t>(
                ControlMessageType::JoinAccepted
            )
        ){
            throw std::runtime_error(
                "Expected JoinAccepted as the first server frame"
            );
        }

        clientId = ReadUint32(response, 1);

        if(clientId == 0){
            throw std::runtime_error(
                "Server assigned an invalid client ID"
            );
        }

        return true;
    }
    catch(const std::exception& error){
        std::cerr
            <<"TCP connection failed: "
            <<error.what()
            <<"\n";

        Disconnect();
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
    if(!running || receiveThread.joinable()){
        throw std::runtime_error(
            "TCP receiver cannot be started"
        );
    }

    receiveThread = std::thread(
        [this, callback](){
            auto nextPing = Clock::now();

            const IsAlive alive = [this](){
                return running.load();
            };

            const Tick heartbeat = [this, &nextPing](){
                if(Clock::now() >= nextPing){
                    SendFrame(
                        std::vector<std::uint8_t>{
                            static_cast<std::uint8_t>(
                                ControlMessageType::Ping
                            )
                        }
                    );

                    nextPing = Clock::now() + heartbeatInterval;
                }
            };

            try{
                while(running){
                    const auto data = ReadControlFrame(
                        socket,
                        alive,
                        heartbeat
                    );

                    const auto type =
                        static_cast<ControlMessageType>(data[0]);

                    if(type == ControlMessageType::Pong){
                        if(data.size() != 1){
                            throw std::runtime_error(
                                "Invalid Pong frame"
                            );
                        }

                        continue;
                    }

                    if(type == ControlMessageType::ClientConnected){
                        if(
                            data.size() < 6 ||
                            data.size() > 5 + maxNameSize
                        ){
                            throw std::runtime_error(
                                "Invalid ClientConnected frame"
                            );
                        }

                        const auto remoteId = ReadUint32(data, 1);

                        if(remoteId == 0){
                            throw std::runtime_error(
                                "Invalid remote client ID"
                            );
                        }

                        callback(
                            type,
                            remoteId,
                            std::string(
                                data.begin() + 5,
                                data.end()
                            )
                        );
                    }
                    else if(
                        type == ControlMessageType::ClientDisconnected
                    ){
                        if(data.size() != 5){
                            throw std::runtime_error(
                                "Invalid ClientDisconnected frame"
                            );
                        }

                        const auto remoteId = ReadUint32(data, 1);

                        if(remoteId == 0){
                            throw std::runtime_error(
                                "Invalid remote client ID"
                            );
                        }

                        callback(type, remoteId, "");
                    }
                    else{
                        throw std::runtime_error(
                            "Unexpected server control frame"
                        );
                    }
                }
            }
            catch(const std::exception& error){
                if(running.exchange(false)){
                    std::cerr
                        <<"TCP control connection lost: "
                        <<error.what()
                        <<"\n";

                    callback(
                        ControlMessageType::ServerDisconnected,
                        0,
                        ""
                    );
                }
            }
        }
    );
}

void TcpControlClient::Disconnect(){
    running = false;

    if(receiveThread.joinable()){
        receiveThread.join();
    }

    boost::system::error_code ignored;
    socket.close(ignored);

    clientId = 0;
}

std::uint32_t TcpControlClient::GetClientId() const{
    return clientId;
}

// Server implementation

TcpControlServer::TcpControlServer(
    boost::asio::io_context& io_context,
    unsigned short port
):
io_context(io_context),
acceptor(
    io_context,
    boost::asio::ip::tcp::endpoint(
        boost::asio::ip::tcp::v4(),
        port
    )
){

}

TcpControlServer::~TcpControlServer(){
    Stop();
}

void TcpControlServer::Start(){
    if(running.exchange(true)){
        return;
    }

    acceptor.non_blocking(true);

    acceptThread = std::thread(
        &TcpControlServer::AcceptLoop,
        this
    );

    std::cout<<"TCP Control Server Started\n";
}

void TcpControlServer::AcceptLoop(){
    while(running){
        // Reap finished handlers instead of retaining
        // one thread record for every historical connection.
        for(auto iterator = handlers.begin();
            iterator != handlers.end();){

            if(
                iterator->wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready
            ){
                try{
                    iterator->get();
                }
                catch(const std::exception& error){
                    std::cerr
                        <<"TCP handler failed: "
                        <<error.what()
                        <<"\n";
                }

                iterator = handlers.erase(iterator);
            }
            else{
                ++iterator;
            }
        }

        auto socket = std::make_shared<
            boost::asio::ip::tcp::socket
        >(io_context);

        boost::system::error_code error;
        acceptor.accept(*socket, error);

        if(WouldBlock(error)){
            std::this_thread::sleep_for(pollInterval);
            continue;
        }

        if(error){
            if(running){
                std::cerr
                    <<"TCP accept failed: "
                    <<error.message()
                    <<"\n";
            }

            std::this_thread::sleep_for(pollInterval);
            continue;
        }

        try{
            handlers.push_back(
                std::async(
                    std::launch::async,
                    [this, socket](){
                        HandleClient(socket);
                    }
                )
            );
        }
        catch(const std::exception& error){
            std::cerr
                <<"Unable to start TCP handler: "
                <<error.what()
                <<"\n";
        }
    }
}

void TcpControlServer::HandleClient(
    std::shared_ptr<
        boost::asio::ip::tcp::socket
    > socket
){
    auto client = std::make_shared<ClientConnection>();
    client->socket = socket;

    std::thread writer;

    const IsAlive alive = [this, client](){
        return running.load() && client->active.load();
    };

    try{
        socket->non_blocking(true);

        const auto joinData = ReadControlFrame(
            *socket,
            alive
        );

        if(
            joinData.size() < 2 ||
            joinData.size() > 1 + maxNameSize ||
            joinData[0] != static_cast<std::uint8_t>(
                ControlMessageType::Join
            )
        ){
            throw std::runtime_error("Invalid Join frame");
        }

        client->clientName.assign(
            joinData.begin() + 1,
            joinData.end()
        );

        {
            std::lock_guard<std::mutex> lock(clientsMutex);

            // Zero means the uint32 counter has exhausted
            // its IDs. Do not wrap and reuse an old session ID.
            if(nextClientId == 0){
                throw std::runtime_error(
                    "Client ID space exhausted"
                );
            }

            client->clientId = nextClientId++;

            // The acknowledgement is always the first
            // outgoing frame for this connection.
            client->outgoing.Push(
                CreateClientMessage(
                    ControlMessageType::JoinAccepted,
                    client->clientId
                )
            );

            // Existing membership becomes this client's roster.
            for(const auto& [id, existing] : clients){
                client->outgoing.Push(
                    CreateClientMessage(
                        ControlMessageType::ClientConnected,
                        id,
                        existing->clientName
                    )
                );
            }

            clients.emplace(client->clientId, client);

            // Register the UDP identity before the writer
            // can deliver JoinAccepted.
            if(clientConnectedCallback){
                clientConnectedCallback(
                    client->clientId,
                    client->clientName
                );
            }

            const auto connectedMessage = CreateClientMessage(
                ControlMessageType::ClientConnected,
                client->clientId,
                client->clientName
            );

            // Queue membership changes under the same lock
            // as registration. This preserves their order.
            for(const auto& [id, recipient] : clients){
                if(id != client->clientId){
                    recipient->outgoing.Push(connectedMessage);
                }
            }

            std::cout
                <<"New Client Connected : "
                <<client->clientName
                <<" | ID : "
                <<client->clientId
                <<"\n";
        }

        // This is the only thread that writes to this socket.
        writer = std::thread(
            [client, alive](){
                try{
                    while(alive()){
                        std::vector<std::uint8_t> payload;

                        if(!client->outgoing.Pop(payload)){
                            std::this_thread::sleep_for(
                                pollInterval
                            );
                            continue;
                        }

                        WriteControlFrame(
                            *client->socket,
                            payload,
                            alive
                        );
                    }
                }
                catch(const std::exception& error){
                    if(alive()){
                        std::cerr
                            <<"TCP send failed for Client "
                            <<client->clientId
                            <<": "
                            <<error.what()
                            <<"\n";
                    }

                    client->active = false;
                }
            }
        );

        while(alive()){
            const auto data = ReadControlFrame(
                *socket,
                alive
            );

            if(
                data.size() != 1 ||
                data[0] != static_cast<std::uint8_t>(
                    ControlMessageType::Ping
                )
            ){
                throw std::runtime_error(
                    "Expected a client heartbeat"
                );
            }

            client->outgoing.Push(
                std::vector<std::uint8_t>{
                    static_cast<std::uint8_t>(
                        ControlMessageType::Pong
                    )
                }
            );
        }
    }
    catch(const std::exception& error){
        if(alive()){
            std::cerr
                <<"TCP session ended for Client "
                <<client->clientId
                <<": "
                <<error.what()
                <<"\n";
        }
    }

    client->active = false;
    RemoveClient(client);

    if(writer.joinable()){
        writer.join();
    }

    // Both socket users have finished before it is closed.
    boost::system::error_code ignored;
    socket->close(ignored);
}

void TcpControlServer::RemoveClient(
    const std::shared_ptr<ClientConnection>& client
){
    std::lock_guard<std::mutex> lock(clientsMutex);

    const auto iterator = clients.find(client->clientId);

    if(
        iterator == clients.end() ||
        iterator->second != client
    ){
        return;
    }

    clients.erase(iterator);

    if(clientDisconnectedCallback){
        clientDisconnectedCallback(client->clientId);
    }

    const auto disconnectedMessage = CreateClientMessage(
        ControlMessageType::ClientDisconnected,
        client->clientId
    );

    for(const auto& [id, recipient] : clients){
        recipient->outgoing.Push(disconnectedMessage);
    }

    std::cout
        <<"Client Disconnected | ID : "
        <<client->clientId
        <<"\n";
}

void TcpControlServer::Stop(){
    running = false;

    if(acceptThread.joinable()){
        acceptThread.join();
    }

    boost::system::error_code ignored;
    acceptor.close(ignored);

    // All readers/writers observe running == false.
    for(auto& handler : handlers){
        try{
            handler.get();
        }
        catch(const std::exception& error){
            std::cerr
                <<"TCP handler cleanup failed: "
                <<error.what()
                <<"\n";
        }
    }

    handlers.clear();
}

void TcpControlServer::SetClientConnectedCallback(
    ClientConnectedCallback callback
){
    clientConnectedCallback = std::move(callback);
}

void TcpControlServer::SetClientDisconnectedCallback(
    ClientDisconnectedCallback callback
){
    clientDisconnectedCallback = std::move(callback);
}