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
        std::cout << Event("server_ready", {{"port", port}, {"tcp", true}, {"udp", true}})
            .dump(ConsoleInput::IsTerminal(1) ? 2 : -1) << std::endl;
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
    const bool interactive = ConsoleInput::IsTerminal() && ConsoleInput::IsTerminal(1);
    enum class Step { Command, GroupType, TeamCount, Name, GroupId, Phase, SessionId, TeamId };
    Step step = Step::Command;
    std::string line, pending;
    bool needsPrompt = true;
    const auto menu = [](){
        std::cout << "\n==================================================\n"
                  << "  realtime-voice | Server\n"
                  << "==================================================\n"
                  << "  1  Create a group\n"
                  << "  2  List groups\n"
                  << "  3  Show group details and participants\n"
                  << "  4  Change Game phase (waiting / playing)\n"
                  << "  5  Assign a player's team\n"
                  << "  6  Delete a group and all its channels\n"
                  << "  0  Shut down the server\n\n"
                  << "Type a number or command. help/menu shows this menu.\n"
                  << "Use back to cancel a guided action. IDs come from list.\n"
                  << "Shortcuts: create game <2-4> [name] | create lobby [name]\n"
                  << "list [after_id] | show <group> | delete <group>\n"
                  << "phase <group> waiting|playing | team <group> <session> <team>\n";
    };
    const auto ask = [&](){
        if(!interactive) return;
        switch(step){
            case Step::Command: std::cout << "\nserver> "; break;
            case Step::GroupType: std::cout << "Group type [1 Game / 2 Lobby]: "; break;
            case Step::TeamCount: std::cout << "Number of teams [2-4]: "; break;
            case Step::Name: std::cout << "Group name [Enter for default]: "; break;
            case Step::GroupId: std::cout << "Group ID: "; break;
            case Step::Phase: std::cout << "Phase [1 waiting / 2 playing]: "; break;
            case Step::SessionId: std::cout << "Player session ID (from group details): "; break;
            case Step::TeamId: std::cout << "Assigned team number [1-4, within this Game]: "; break;
        }
        std::cout << std::flush;
    };
    const auto validId = [](const std::string& value){
        std::uint64_t number = 0;
        if(value.empty()) return false;
        for(char digit : value){
            if(digit < '0' || digit > '9') return false;
            number = number * 10 + (digit - '0');
            if(number > UINT32_MAX) return false;
        }
        return number > 0;
    };
    menu();
    while(running){
        if(needsPrompt){ ask(); needsPrompt = false; }
        const auto result = input.Poll(line);
        if(result == ConsoleInput::Closed) return; // Headless service continues after terminal EOF.
        if(result == ConsoleInput::None){
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if(result == ConsoleInput::TooLong){
            std::cerr << "Command exceeds 8192 bytes. Try a shorter command.\n";
            needsPrompt = true; continue;
        }
        needsPrompt = true;
        const auto first = line.find_first_not_of(" \t");
        line = first == std::string::npos ? "" : line.substr(first, line.find_last_not_of(" \t") - first + 1);
        try{
            if(line == "menu" || line == "help" || line == "?" || line == "back" || line == "cancel"){
                step = Step::Command; pending.clear(); menu(); continue;
            }
            if(step == Step::GroupType){
                if(line == "1") line = "game";
                if(line == "2") line = "lobby";
                if(line != "game" && line != "lobby") throw std::runtime_error("Choose 1/game or 2/lobby, or type back");
                pending = "create " + line;
                step = line == "game" ? Step::TeamCount : Step::Name;
                continue;
            }
            if(step == Step::TeamCount){
                if(line != "2" && line != "3" && line != "4") throw std::runtime_error("A Game needs 2, 3, or 4 teams");
                pending += " " + line; step = Step::Name; continue;
            }
            if(step == Step::Name){
                if(!line.empty()) pending += " " + line;
                line = pending; step = Step::Command;
            }
            else if(step == Step::GroupId || step == Step::SessionId || step == Step::TeamId){
                if(!validId(line)) throw std::runtime_error("Enter a positive numeric ID, or type back");
                pending += " " + line;
                if(step == Step::GroupId && pending.rfind("phase ", 0) == 0){ step = Step::Phase; continue; }
                if(step == Step::GroupId && pending.rfind("team ", 0) == 0){ step = Step::SessionId; continue; }
                if(step == Step::SessionId){ step = Step::TeamId; continue; }
                line = pending; step = Step::Command;
            }
            else if(step == Step::Phase){
                if(line == "1") line = "waiting";
                if(line == "2") line = "playing";
                if(line != "waiting" && line != "playing") throw std::runtime_error("Choose 1/waiting or 2/playing");
                line = pending + " " + line; step = Step::Command;
            }
            if(line == "1") line = "create";
            else if(line == "2") line = "list";
            else if(line == "3") line = "show";
            else if(line == "4") line = "phase";
            else if(line == "5") line = "team";
            else if(line == "6") line = "delete";
            else if(line == "0") line = "shutdown";
            if(line == "show" || line == "phase" || line == "team" || line == "delete" || line == "stop"){
                pending = line; step = Step::GroupId; continue;
            }
            std::istringstream words(line);
            std::string command;
            words >> command;
            if(command.empty()) continue;
            if(command == "shutdown" || command == "quit"){
                std::cout << "Shutting down voice server...\n"; running = false; break;
            }
            Json request = {{"v", 2}, {"id", "operator"}};
            if(command == "create"){
                std::string type;
                if(!(words >> type)){ step = Step::GroupType; continue; }
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
                if(command != "show" && command != "delete" && command != "stop" && command != "phase" && command != "team"){
                    throw std::runtime_error("Unknown command. Type help to see the menu");
                }
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
            const auto data = groups.Operator(request);
            if(interactive){
                std::cout << "\n--- " << command << " completed ---\n";
                if(data.contains("total_groups")) std::cout << "Total groups: " << data["total_groups"] << '\n';
                if(data.contains("connected_players")) std::cout << "Connected players: " << data["connected_players"] << '\n';
            }
            std::cout << Success(request, data).dump(ConsoleInput::IsTerminal(1) ? 2 : -1) << '\n' << std::flush;
        }
        catch(const std::exception& error){
            std::cerr << "Operator error: " << error.what() << '\n';
        }
    }
}
