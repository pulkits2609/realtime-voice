#include <iostream>
#include <stdexcept>
#include <sstream>
#include <csignal>
#include "Network/client.hpp"
#include "Common/consoleInput.hpp"

namespace{
    std::uint32_t Argument(const char* text, bool zero = false){
        std::size_t count = 0;
        const auto number = std::stoll(text, &count);
        if(count != std::string(text).size() || number < (zero ? 0 : 1) || number > UINT32_MAX){
            throw std::invalid_argument("Invalid numeric argument");
        }
        return static_cast<std::uint32_t>(number);
    }
}

int main(int argc, char* argv[]){
    if(argc < 6 || argc > 8){
        std::cerr << "Usage: voice_client <host> <port> <name> <group_id> <channel_id> [team] [--no-audio]\n";
        return 1;
    }
    try{
        const auto port = Argument(argv[2]);
        if(port > 65535) throw std::invalid_argument("Port must be between 1 and 65535");
        Json join = {{"group_id", Argument(argv[4])}, {"channel_id", Argument(argv[5])},
                     {"member_id", std::string(argv[3])}, {"team", 0}};
        bool audio = true;
        for(int i = 6; i < argc; i++){
            if(std::string(argv[i]) == "--no-audio") audio = false;
            else join["team"] = Argument(argv[i], true);
        }
        auto mutes = std::make_shared<MuteState>();
        std::mutex logMutex;
        Client client(argv[1], static_cast<unsigned short>(port), argv[3], join, mutes,
            [&](const Json& event){
                std::lock_guard<std::mutex> lock(logMutex);
                std::cerr << event.dump() << '\n';
            }, audio);
        boost::asio::io_context signalsContext;
        boost::asio::signal_set signals(signalsContext, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int){ client.RequestStop(); });
        std::thread worker([&]{ client.Run(); });
        ConsoleInput input;
        std::uint32_t requestId = 0;
        std::cerr << "Muted by default. Commands: mic on|off, all on|off, team on|off, tx <channel>, status, disconnect\n";
        while(!client.Done()){
            signalsContext.poll();
            std::string line;
            const auto result = input.Poll(line);
            if(result == ConsoleInput::Closed){ client.RequestStop(); break; }
            if(result == ConsoleInput::TooLong){ std::cerr << "Input too long\n"; continue; }
            if(result == ConsoleInput::None){
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;
            }
            try{
                std::istringstream words(line);
                std::string command, value; words >> command >> value;
                if(command == "disconnect" || command == "quit"){ client.RequestStop(); break; }
                if(command == "status"){ std::cerr << client.GetStatus().dump(2) << '\n'; continue; }
                Json request = {{"v", 2}, {"id", std::to_string(++requestId)}};
                if(command == "tx"){
                    request.update({{"op", "set_transmit"}, {"channel_id", Argument(value.c_str())}});
                }
                else if(command == "mic" || command == "all" || command == "team"){
                    if(value != "on" && value != "off") throw std::runtime_error("Use on or off");
                    request.update(mutes->Snapshot());
                    request["op"] = "set_mutes";
                    request[command == "mic" ? "microphone" : command] = value == "off";
                }
                else throw std::runtime_error("Unknown client command");
                client.Post(request);
            }
            catch(const std::exception& error){ std::cerr << error.what() << '\n'; }
        }
        client.RequestStop();
        worker.join();
        return 0;
    }
    catch(const std::exception& error){
        std::cerr << "Client error: " << error.what() << '\n';
        return 1;
    }
}
