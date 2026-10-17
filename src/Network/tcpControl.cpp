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

    constexpr std::size_t maxFrameSize = MaxControlBytes;
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
    const std::string& clientName,
    const std::atomic<bool>& sessionRunning
){
    Disconnect();

    try{
        if(clientName.empty() || clientName.size() > maxNameSize){
            throw std::runtime_error(
                "Client name must contain 1 to 64 bytes"
            );
        }

        using Tcp = boost::asio::ip::tcp;

        Tcp::resolver resolver(io_context);

        bool completed = false;
        bool cancelled = false;

        boost::system::error_code connectionError;

        io_context.restart();

        resolver.async_resolve(
            Tcp::v4(),
            serverAddress,
            std::to_string(serverPort),
            [&](const boost::system::error_code& error,
                Tcp::resolver::results_type endpoints){

                if(error || cancelled || !sessionRunning){
                    connectionError = error
                        ? error
                        : boost::asio::error::operation_aborted;

                    completed = true;
                    return;
                }

                boost::asio::async_connect(
                    socket,
                    endpoints,
                    [&](const boost::system::error_code& error,
                        const Tcp::endpoint&){

                        connectionError = error;
                        completed = true;
                    }
                );
            }
        );

        const auto deadline = Clock::now() + connectTimeout;

        while(
            !completed &&
            sessionRunning &&
            Clock::now() < deadline
        ){
            io_context.run_for(pollInterval);
        }

        if(!completed || !sessionRunning){
            cancelled = true;
            resolver.cancel();

            boost::system::error_code ignored;
            socket.cancel(ignored);

            // Finish callbacks before their local references expire.
            while(!completed){
                io_context.restart();
                io_context.run_for(pollInterval);
            }

            throw std::runtime_error(
                sessionRunning
                    ? "Server lookup / connection timed out"
                    : "Connection stopped"
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

        const IsAlive alive = [this, &sessionRunning](){
            io_context.poll();

            return running.load() && sessionRunning.load();
        };

        WriteControlFrame(socket, joinMessage, alive);

        const auto response = ReadControlFrame(socket, alive);

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
        if(sessionRunning){
            std::cerr
                <<"TCP connection failed: "
                <<error.what()
                <<"\n";
        }

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

boost::asio::ip::address TcpControlClient::GetServerAddress() const{
    return socket.remote_endpoint().address();
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

void TcpControlServer::HandleClient(std::shared_ptr<boost::asio::ip::tcp::socket> socket){
    auto client = std::make_shared<ClientConnection>();
    client->socket = socket;
    std::thread writer;
    bool registered = false;
    Json hello = Json::object();
    const IsAlive alive = [this, client](){ return running.load() && client->active.load(); };

    try{
        socket->non_blocking(true);
        auto first = ReadControlFrame(*socket, alive);
        try{ hello = Json::parse(first); }
        catch(const Json::exception&){
            throw ProtocolError("incompatible_version", "Use length-prefixed JSON protocol version 2");
        }
        ValidateRequest(hello);
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            if(nextClientId == 0) throw ProtocolError("ids_exhausted", "Session IDs exhausted");
            client->clientId = nextClientId++;
            clients.emplace(client->clientId, client);
        }
        const auto data = opened(client->clientId, hello);
        registered = true;
        // Write hello directly before starting the writer. Events queued during
        // registration cannot overtake this first response.
        const auto acknowledgement = Success(hello, data).dump();
        WriteControlFrame(*socket, {acknowledgement.begin(), acknowledgement.end()}, alive);

        writer = std::thread([client, alive](){
            auto nextPing = Clock::now();
            std::uint32_t nonce = 0;
            try{
                while(alive()){
                    std::vector<std::uint8_t> payload;
                    if(client->outgoing.Pop(payload)){
                        WriteControlFrame(*client->socket, payload, alive);
                    }
                    else if(client->closing){
                        client->active = false;
                        break;
                    }
                    else{
                        std::this_thread::sleep_for(pollInterval);
                    }
                    if(!client->closing && Clock::now() >= nextPing && client->pendingPing == 0){
                        if(++nonce == 0) ++nonce;
                        client->pendingPing = nonce;
                        const auto ping = Event("ping", {{"nonce", nonce}}).dump();
                        WriteControlFrame(*client->socket, {ping.begin(), ping.end()}, alive);
                        nextPing = Clock::now() + heartbeatInterval;
                    }
                }
            }
            catch(const std::exception& error){
                if(alive()) std::cerr << "TCP send failed: " << error.what() << '\n';
                client->active = false;
            }
        });

        auto lastPong = Clock::now();
        const Tick heartbeat = [&](){
            if(Clock::now() - lastPong >= receiveTimeout){
                throw ProtocolError("heartbeat_timeout", "No valid heartbeat response for ten seconds");
            }
        };
        while(alive()){
            Json request = Json::object();
            try{
                auto frame = ReadControlFrame(*socket, alive, heartbeat);
                request = Json::parse(frame);
                if(request.is_object() && request.value("op", "") == "pong"){
                    if(request.value("v", 0) != VoiceProtocolVersion ||
                       Number(request, "nonce") != client->pendingPing.load()){
                        throw ProtocolError("invalid_heartbeat", "The pong nonce does not match the outstanding ping");
                    }
                    client->pendingPing = 0;
                    lastPong = Clock::now();
                    continue;
                }
                ValidateRequest(request);
                if(client->closing) continue;
                Send(client->clientId, Success(request, requested(client->clientId, request)));
            }
            catch(const ProtocolError& error){
                if(error.code == "heartbeat_timeout") throw;
                Send(client->clientId, Failure(request, error.code, error.what()));
            }
            catch(const Json::exception& error){
                Send(client->clientId, Failure(request, "invalid_request", error.what()));
            }
        }
    }
    catch(const std::exception& error){
        if(!writer.joinable() && alive()){
            try{
                const auto protocol = dynamic_cast<const ProtocolError*>(&error);
                const auto failure = Failure(hello, protocol ? protocol->code : "connection_error", error.what()).dump();
                WriteControlFrame(*socket, {failure.begin(), failure.end()}, alive);
            }
            catch(const std::exception&){}
        }
        if(alive()) std::cerr << "TCP session ended: " << error.what() << '\n';
    }

    client->active = false;
    RemoveClient(client);
    // No transport mutex is held while entering routing state.
    if(registered && removed) removed(client->clientId);
    if(writer.joinable()) writer.join();
    boost::system::error_code ignored;
    socket->close(ignored);
}

void TcpControlServer::RemoveClient(const std::shared_ptr<ClientConnection>& client){
    std::lock_guard<std::mutex> lock(clientsMutex);
    clients.erase(client->clientId);
}

void TcpControlServer::Stop(){
    running = false;
    if(acceptThread.joinable()) acceptThread.join();
    boost::system::error_code ignored;
    acceptor.close(ignored);
    for(auto& handler : handlers){
        try{ handler.get(); }
        catch(const std::exception& error){ std::cerr << "TCP cleanup: " << error.what() << '\n'; }
    }
    handlers.clear();
}

void TcpControlServer::SetHandlers(
    std::function<Json(std::uint32_t, const Json&)> open,
    std::function<Json(std::uint32_t, const Json&)> request,
    std::function<void(std::uint32_t)> remove){
    opened = std::move(open);
    requested = std::move(request);
    removed = std::move(remove);
}

void TcpControlServer::Send(std::uint32_t id, const Json& message){
    const auto text = message.dump();
    std::lock_guard<std::mutex> lock(clientsMutex);
    auto found = clients.find(id);
    if(found == clients.end()) return;
    if(text.size() > MaxControlBytes ||
       !found->second->outgoing.TryPush({text.begin(), text.end()})){
        found->second->active = false;
    }
}

void TcpControlServer::Close(std::uint32_t id){
    std::lock_guard<std::mutex> lock(clientsMutex);
    auto found = clients.find(id);
    if(found != clients.end()) found->second->closing = true;
}
