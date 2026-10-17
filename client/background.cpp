#include "Network/client.hpp"
#include "Common/consoleInput.hpp"
#include "Common/consoleOutput.hpp"
#include <csignal>
#include <iostream>

int RunBackgroundClient(){
    ThreadSafeQueue<Json> output{128};
    std::atomic<bool> overflow{false};
    auto publish = [&](const Json& message){
        if(!output.TryPush(message)) overflow = true;
    };
    auto mutes = std::make_shared<MuteState>();
    std::unique_ptr<Client> client;
    std::thread worker;
    const auto disconnect = [&](){
        if(client) client->RequestStop();
        if(worker.joinable()) worker.join();
        client.reset();
    };
    const auto status = [&](){
        return client ? client->GetStatus() : Json{{"state", "disconnected"}, {"group_id", 0},
            {"transmit_channel_id", nullptr}, {"receive_channels", Json::array()}, {"mutes", mutes->Snapshot()}};
    };
    bool stopping = false;
    boost::asio::io_context signalsContext;
    boost::asio::signal_set signals(signalsContext, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int){ stopping = true; });
#ifndef _WIN32
    // A closed parent output pipe is an IPC failure, not an unhandled process signal.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    try{
        ConsoleInput input(false);
        ConsoleOutput writer;
        std::string pending;
        std::size_t offset = 0;
        std::set<std::string> outstanding;
        publish(Event("ready", {{"protocol", 2}, {"mutes", mutes->Snapshot()}}));
        while(!stopping && !overflow){
            signalsContext.poll();
            std::string line;
            const auto result = input.Poll(line);
            if(result == ConsoleInput::Closed){ stopping = true; break; }
            if(result == ConsoleInput::TooLong){
                publish(Failure(Json::object(), "input_too_large", "IPC input is limited to 8192 bytes per line"));
            }
            else if(result == ConsoleInput::Line){
                Json request = Json::object();
                try{
                    request = Json::parse(line);
                    ValidateRequest(request);
                    const auto id = Text(request, "id");
                    if(id.rfind("__", 0) == 0) throw ProtocolError("invalid_request", "IDs beginning __ are reserved");
                    if(outstanding.count(id)) throw ProtocolError("duplicate_id", "This request ID is still outstanding");
                    const auto op = Text(request, "op");
                    if(op == "shutdown"){
                        disconnect();
                        publish(Success(request));
                        stopping = true;
                    }
                    else if(op == "disconnect"){
                        disconnect();
                        publish(Success(request, status()));
                    }
                    else if(op == "get_status"){
                        publish(Success(request, status()));
                    }
                    else if(op == "connect"){
                        if(client && !client->Done()) throw ProtocolError("already_connected", "Disconnect before connecting again");
                        const auto host = Text(request, "host", 253);
                        const auto port = Number(request, "port");
                        if(port > 65535) throw ProtocolError("invalid_request", "port must be between 1 and 65535");
                        const auto name = Text(request, "name");
                        Json join = {{"group_id", Number(request, "group_id")},
                                     {"channel_id", Number(request, "channel_id")},
                                     {"member_id", Text(request, "member_id")},
                                     {"server_instance", Text(request, "server_instance", 16)},
                                     {"team", request.contains("team") ? Number(request, "team", true) : 0}};
                        const bool audio = request.contains("audio") ? Boolean(request, "audio") : true;
                        disconnect();
                        client = std::make_unique<Client>(host, static_cast<unsigned short>(port), name, join, mutes,
                            [&, id](Json message){
                                if(message.value("event", "") == "error") message["data"]["request_id"] = id;
                                publish(message);
                            }, audio);
                        worker = std::thread([&](){ client->Run(); });
                        publish(Success(request, {{"accepted", true}, {"state", "connecting"}}));
                    }
                    else if(op == "set_transmit"){
                        Number(request, "channel_id");
                        if(!client || client->Done()) throw ProtocolError("not_connected", "Voice is disconnected");
                        client->Post(request);
                        outstanding.insert(id);
                    }
                    else if(op == "set_microphone" || op == "set_incoming_all" || op == "set_incoming_team" || op == "set_mutes"){
                        Json values = mutes->Snapshot();
                        if(op == "set_mutes"){
                            for(const char* key : {"microphone", "all", "team"}) values[key] = Boolean(request, key);
                        }
                        else{
                            const char* key = op == "set_microphone" ? "microphone" : op == "set_incoming_all" ? "all" : "team";
                            values[key] = Boolean(request, "muted");
                        }
                        if(client && !client->Done()){
                            Json remote = request;
                            remote["op"] = "set_mutes";
                            remote.update(values);
                            client->Post(remote);
                            outstanding.insert(id);
                        }
                        else{
                            mutes->Set(values);
                            publish(Success(request, {{"mutes", values}, {"applied", false}}));
                            publish(Event("mute_changed", values));
                        }
                    }
                    else throw ProtocolError("unknown_operation", "Unknown background-client command");
                }
                catch(const ProtocolError& error){
                    publish(Failure(request, error.code, error.what()));
                }
                catch(const Json::exception& error){
                    publish(Failure(request, "invalid_request", error.what()));
                }
                catch(const std::exception& error){
                    publish(Failure(request, "client_error", error.what()));
                }
            }
            if(offset >= pending.size()){
                Json message;
                if(output.Pop(message)){
                    if(message.contains("id") && message["id"].is_string()){
                        outstanding.erase(message["id"].get<std::string>());
                    }
                    pending = message.dump() + "\n"; offset = 0;
                }
            }
            if(!writer.Poll(pending, offset)){ stopping = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        disconnect();
        // Best-effort final responses without waiting forever for a non-reading parent.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while(std::chrono::steady_clock::now() < deadline){
            if(offset >= pending.size()){
                Json message;
                if(!output.Pop(message)) break;
                pending = message.dump() + "\n"; offset = 0;
            }
            if(!writer.Poll(pending, offset)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(overflow) std::cerr << "IPC output queue full; parent must drain stdout\n";
        return overflow ? 2 : 0;
    }
    catch(const std::exception& error){
        disconnect();
        std::cerr << "Background client failed: " << error.what() << '\n';
        return 1;
    }
}

