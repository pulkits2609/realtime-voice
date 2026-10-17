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

int RunBackgroundClient();

int main(int argc, char* argv[]){
    if(argc == 2 && std::string(argv[1]) == "--ipc") return RunBackgroundClient();
    if(argc < 6 || argc > 8){
        std::cerr << "Usage: voice_client <host> <port> <name> <group_id> <channel_id> [team] [--no-audio]\n";
        std::cerr << "Or: voice_client --ipc\n";
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
        const bool pretty = ConsoleInput::IsTerminal(2);
        const bool interactive = ConsoleInput::IsTerminal() && pretty;
        std::atomic<bool> notice{false};
        const auto write = [&](const std::string& text){
            std::lock_guard<std::mutex> lock(logMutex);
            std::cerr << text << std::flush;
        };
        Client client(argv[1], static_cast<unsigned short>(port), argv[3], join, mutes,
            [&](const Json& event){
                write((interactive ? "\n" : "") + event.dump(pretty ? 2 : -1) + "\n");
                notice = true;
            }, audio);
        const auto menu = [&](){
            const auto status = client.GetStatus();
            const auto preferences = mutes->Snapshot();
            std::ostringstream text;
            text << "\n==================================================\n"
                 << "  realtime-voice | Client\n"
                 << "==================================================\n"
                 << "  Server: " << argv[1] << ':' << port << " | Name: " << argv[3] << '\n'
                 << "  Status: " << status.value("state", "connecting")
                 << " | Group: " << status.value("group_id", 0u) << '\n'
                 << "  1  Microphone       [" << (preferences["microphone"].get<bool>() ? "MUTED" : "OPEN") << "] - toggle\n"
                 << "  2  Incoming All     [" << (preferences["all"].get<bool>() ? "MUTED" : "ON") << "] - toggle\n"
                 << "  3  Incoming Team    [" << (preferences["team"].get<bool>() ? "MUTED" : "ON") << "] - toggle (Game groups)\n"
                 << "  4  Choose transmit destination (All / own Team)\n"
                 << "  5  Show full status and receive routes\n"
                 << "  0  Disconnect and exit\n\n"
                 << "All three controls start muted. on means unmuted.\n"
                 << "Type a number or command. help/menu refreshes this menu.\n"
                 << "Shortcuts: mic on|off | all on|off | team on|off\n"
                 << "tx all|team|<channel_id> | status | disconnect\n"
                 << "Transmit selection does not change what you can receive.\n";
            write(text.str());
        };
        boost::asio::io_context signalsContext;
        boost::asio::signal_set signals(signalsContext, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int){ client.RequestStop(); });
        std::thread worker([&]{ client.Run(); });
        ConsoleInput input;
        std::uint32_t requestId = 0;
        bool choosingTransmit = false, needsPrompt = true;
        menu();
        while(!client.Done()){
            signalsContext.poll();
            if(notice.exchange(false)) needsPrompt = true;
            if(needsPrompt && interactive){
                write(choosingTransmit ? "Destination [all / team / channel ID; back cancels]: " : "\nclient> ");
                needsPrompt = false;
            }
            std::string line;
            const auto result = input.Poll(line);
            if(result == ConsoleInput::Closed){ client.RequestStop(); break; }
            if(result == ConsoleInput::TooLong){ write("Input too long. Try a shorter command.\n"); needsPrompt = true; continue; }
            if(result == ConsoleInput::None){
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;
            }
            needsPrompt = true;
            try{
                std::istringstream words(line);
                std::string command, value; words >> command >> value;
                if(command.empty()) continue;
                if(command == "help" || command == "menu" || command == "?" || command == "back" || command == "cancel"){
                    choosingTransmit = false; menu(); continue;
                }
                if(command == "disconnect" || command == "quit" || command == "0"){ client.RequestStop(); break; }
                if(choosingTransmit){ value = command; command = "tx"; }
                else if(command == "1" || command == "2" || command == "3"){
                    command = command == "1" ? "mic" : command == "2" ? "all" : "team";
                    const auto field = command == "mic" ? "microphone" : command;
                    value = mutes->Snapshot()[field].get<bool>() ? "on" : "off";
                }
                else if(command == "4") command = "tx";
                else if(command == "5") command = "status";
                else if(command == "0") command = "disconnect";
                if(command == "disconnect" || command == "quit"){ client.RequestStop(); break; }
                if(command == "status"){ write(client.GetStatus().dump(pretty ? 2 : -1) + "\n"); continue; }
                Json request = {{"v", 2}, {"id", std::to_string(++requestId)}};
                if(command == "tx"){
                    const auto status = client.GetStatus();
                    if(status.value("group_id", 0u) == 0) throw std::runtime_error("Wait until you have joined a group before changing transmit");
                    if(value.empty()){
                        std::ostringstream text;
                        text << "All channel: " << status.at("all_channel_id");
                        if(!status.at("team_channel_id").is_null()) text << " | Your Team channel: " << status.at("team_channel_id");
                        text << " | Phase: " << status.at("phase").get<std::string>() << '\n';
                        text << (status.at("team_channel_id").is_null()
                            ? "A Lobby has only the shared All route.\n"
                            : "Team transmission is available only during playing.\n");
                        write(text.str()); choosingTransmit = true; continue;
                    }
                    if(value == "all") value = std::to_string(status.at("all_channel_id").get<std::uint32_t>());
                    else if(value == "team"){
                        if(status.at("team_channel_id").is_null()) throw std::runtime_error("Lobby groups have no Team channel. Choose all instead");
                        value = std::to_string(status.at("team_channel_id").get<std::uint32_t>());
                    }
                    request.update({{"op", "set_transmit"}, {"channel_id", Argument(value.c_str())}});
                    choosingTransmit = false;
                }
                else if(command == "mic" || command == "all" || command == "team"){
                    if(value != "on" && value != "off") throw std::runtime_error("Use on or off");
                    request.update(mutes->Snapshot());
                    request["op"] = "set_mutes";
                    request[command == "mic" ? "microphone" : command] = value == "off";
                }
                else throw std::runtime_error("Unknown command. Type help to see the menu");
                client.Post(request);
            }
            catch(const std::exception& error){ write(std::string("Client: ") + error.what() + "\n"); }
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
