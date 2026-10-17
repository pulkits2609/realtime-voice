#include "Network/server.hpp"
#include "Common/consoleInput.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <sstream>

Server::Server(unsigned short port):
socket(io_context),
port(port),
groups(
    [this](auto id, const Json& message){ controlServer.Send(id, message); },
    [this](auto id){ controlServer.Close(id); }
),
controlServer(io_context, port){
    controlServer.SetHandlers(
        [this](auto id, const Json& hello){ return groups.Open(id, hello); },
        [this](auto id, const Json& request){ return groups.Request(id, request); },
        [this](auto id, bool heartbeatExpired){ groups.Remove(id, heartbeatExpired); }
    );
}

void Server::HandleMessage(){
    boost::asio::ip::udp::endpoint endpoint;
    auto data = socket.ReceiveFrom(endpoint);
    RoutedPacket packet;
    if(!packet.Deserialize(data)) return;
    groups.Route(std::move(packet), endpoint, [this](const RoutedPacket& routed, const auto& target){
        try{ socket.SendTo(routed.Serialize(), target); }
        catch(const boost::system::system_error& error){
            std::cerr << "UDP relay failed: " << error.what() << '\n';
        }
    });
}

void Server::Run(){
    running = true;
    io_context.restart();
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([this](const boost::system::error_code& error, int){
        if(!error) running = false;
    });
    std::thread console;
    try{
        socket.Open();
        socket.Bind(port);
        socket.SetNonBlocking(true);
        controlServer.Start();
        // Flush only after UDP is bound and TCP is listening/accepting.
        std::cout << Event("server_ready", {{"port", port}, {"tcp", true}, {"udp", true}}).dump() << std::endl;
        console = std::thread(&Server::ConsoleLoop, this);
        while(running){
            io_context.poll();
            groups.Tick();
            if(!running) break;
            try{ HandleMessage(); }
            catch(const boost::system::system_error& error){
                const auto code = error.code();
                if(code == boost::asio::error::would_block || code == boost::asio::error::try_again){
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                if(code == boost::asio::error::message_size || code == boost::asio::error::connection_reset) continue;
                throw;
            }
        }
    }
    catch(...){
        running = false;
        if(console.joinable()) console.join();
        controlServer.Stop();
        socket.Close();
        throw;
    }
    running = false;
    if(console.joinable()) console.join();
    controlServer.Stop();
    socket.Close();
}

void Server::ConsoleLoop(){
    ConsoleInput input;
    std::string line, pendingType;
    int step = 0;
    std::cout << "Commands: create | create game <2-4> [name] | create lobby [name]\n"
              << "list [after_id] | show <group> | delete <group> | phase <group> waiting|playing\n"
              << "team <group> <session> <team> | shutdown\n";
    while(running){
        const auto result = input.Poll(line);
        if(result == ConsoleInput::Closed) return; // Headless service continues after terminal EOF.
        if(result == ConsoleInput::None){
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if(result == ConsoleInput::TooLong){
            std::cerr << "Command exceeds 8192 bytes\n"; continue;
        }
        try{
            if(step == 1){
                if(line != "game" && line != "lobby") throw std::runtime_error("Choose game or lobby");
                pendingType = line;
                if(line == "game"){ step = 2; std::cout << "Teams (2-4): " << std::flush; continue; }
                line = "create lobby"; step = 0;
            }
            else if(step == 2){ line = "create game " + line; step = 0; }
            std::istringstream words(line);
            std::string command;
            words >> command;
            if(command.empty()) continue;
            if(command == "shutdown" || command == "quit"){ running = false; break; }
            Json request = {{"v", 2}, {"id", "operator"}};
            if(command == "create"){
                std::string type;
                if(!(words >> type)){ step = 1; std::cout << "Type (game/lobby): " << std::flush; continue; }
                request.update({{"op", "create_group"}, {"type", type}});
                if(type == "game"){
                    int teams = 0;
                    if(!(words >> teams)) throw std::runtime_error("Usage: create game <2-4> [name]");
                    request["teams"] = teams;
                }
                std::string name;
                std::getline(words >> std::ws, name);
                if(!name.empty()) request["name"] = name;
            }
            else if(command == "list"){
                request["op"] = "list_groups";
                std::uint32_t after = 0;
                if(words >> after) request["after_id"] = after;
            }
            else{
                unsigned group = 0;
                if(!(words >> group)) throw std::runtime_error("A group ID is required");
                request["group_id"] = group;
                if(command == "show") request["op"] = "group_details";
                else if(command == "delete" || command == "stop") request["op"] = "delete_group";
                else if(command == "phase"){
                    std::string phase; words >> phase;
                    request.update({{"op", "set_phase"}, {"phase", phase}});
                }
                else if(command == "team"){
                    unsigned session = 0, team = 0;
                    if(!(words >> session >> team)) throw std::runtime_error("Usage: team <group> <session> <team>");
                    request.update({{"op", "set_team"}, {"session_id", session}, {"team", team}});
                }
                else throw std::runtime_error("Unknown server command");
            }
            std::cout << Success(request, groups.Operator(request)).dump() << '\n' << std::flush;
        }
        catch(const std::exception& error){
            step = 0;
            std::cerr << "Operator error: " << error.what() << '\n';
        }
    }
}
