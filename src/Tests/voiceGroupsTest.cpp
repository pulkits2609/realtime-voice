#include "Network/voiceGroups.hpp"
#include <iostream>
#include <stdexcept>
#include <set>

void Check(bool value, const char* message){
    if(!value) throw std::runtime_error(message);
}

int main(){
    try{
        std::map<std::uint32_t, Json> state;
        std::set<std::uint32_t> closed;
        VoiceGroups groups([&](auto id, const Json& event){
            if(event["event"] == "state") state[id] = event["data"];
        }, [&](auto id){ closed.insert(id); });
        auto request = [](const std::string& op){ return Json{{"v", 2}, {"id", "test"}, {"op", op}}; };
        auto error = [&](auto action, const std::string& code){
            try{ action(); }
            catch(const ProtocolError& failure){ Check(failure.code == code, "Wrong error code"); return; }
            throw std::runtime_error("Expected request rejection");
        };
        auto create = [&](int teams){
            auto value = request("create_group"); value["type"] = "game"; value["teams"] = teams;
            return groups.Operator(value);
        };
        for(int teams : {1, 5}){
            error([&]{ create(teams); }, "invalid_team_count");
        }
        Check(create(3)["team_channel_ids"].size() == 3, "Three teams");
        Check(create(4)["team_channel_ids"].size() == 4, "Four teams");
        const auto game = create(2);
        const auto other = create(2);
        const auto gid = game["group_id"].get<std::uint32_t>();
        const auto all = game["all_channel_id"].get<std::uint32_t>();
        const auto team1 = game["team_channel_ids"][0].get<std::uint32_t>();
        auto open = [&](unsigned id, const Json& gameInfo, unsigned team){
            auto hello = request("hello");
            hello.update({{"role", "voice"}, {"name", "player"}, {"group_id", gameInfo["group_id"]},
                          {"channel_id", gameInfo["all_channel_id"]}});
            groups.Open(id, hello);
            auto join = request("join");
            join.update({{"group_id", gameInfo["group_id"]}, {"channel_id", gameInfo["all_channel_id"]}, {"team", team}});
            groups.Request(id, join);
            auto mutes = request("set_mutes");
            mutes.update({{"microphone", false}, {"all", false}, {"team", false}});
            groups.Request(id, mutes);
        };
        open(1, game, 1); open(2, game, 1); open(3, game, 2); open(4, other, 1);
        auto set = request("set_transmit"); set["channel_id"] = team1;
        error([&]{ groups.Request(1, set); }, "team_disabled");
        error([&]{ groups.Request(1, request("list_groups")); }, "forbidden");
        std::set<unsigned> recipients;
        auto route = [&](unsigned id, bool audio){
            RoutedPacket packet(PacketType::Voice, audio ? std::vector<std::uint8_t>{1} : std::vector<std::uint8_t>{});
            packet.SetClientId(id);
            const auto& current = state.at(id);
            packet.SetContext(groups.InstanceNumber(), current["group_id"], current["transmit_channel_id"], current["send_revision"]);
            groups.Route(packet, {boost::asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(30000 + id)},
                [&](const auto&, const auto& endpoint){ recipients.insert(endpoint.port() - 30000); });
            return packet;
        };
        for(unsigned id = 1; id <= 4; id++) route(id, false);
        route(1, true);
        Check(recipients == std::set<unsigned>({2, 3}), "All routing leaks or self echo");
        recipients.clear();
        auto phase = request("set_phase"); phase.update({{"group_id", gid}, {"phase", "playing"}});
        groups.Operator(phase);
        Check(state[1]["transmit_channel_id"] == team1, "Match did not force Team");
        auto selectAll = request("set_transmit"); selectAll["channel_id"] = all;
        groups.Request(2, selectAll);
        auto stale = route(1, true);
        Check(recipients == std::set<unsigned>({2}), "Team recipient must hear while transmitting All");
        recipients.clear();
        auto wire = stale.Serialize();
        RoutedPacket decoded;
        Check(wire.size() == RoutedPacket::HeaderSize + 1 && decoded.Deserialize(wire), "Versioned packet round trip");
        Check(decoded.GetInstance() == groups.InstanceNumber() && decoded.GetChannelId() == team1,
              "Routing fields were not preserved");
        for(std::size_t length = 0; length < wire.size(); length++){
            Check(!decoded.Deserialize({wire.begin(), wire.begin() + length}), "Accepted truncated routing header");
        }
        auto invalidWire = wire; invalidWire[2] = 1;
        Check(!decoded.Deserialize(invalidWire), "Accepted incompatible UDP version");
        invalidWire = wire; invalidWire.push_back(0);
        Check(!decoded.Deserialize(invalidWire), "Accepted trailing UDP bytes");
        auto forged = stale;
        forged.SetContext(groups.InstanceNumber(), gid, game["team_channel_ids"][1], stale.GetSendRevision());
        groups.Route(forged, {boost::asio::ip::make_address("127.0.0.1"), 30001},
            [&](const auto&, const auto&){ recipients.insert(100); });
        Check(recipients.empty(), "Accepted another team's claimed target");
        auto mute = request("set_mutes"); mute.update({{"microphone", false}, {"all", true}, {"team", false}});
        groups.Request(2, mute);
        route(1, true);
        Check(recipients == std::set<unsigned>({2}), "All mute incorrectly mutes Team");
        recipients.clear();
        mute["team"] = true; groups.Request(2, mute); route(1, true);
        Check(recipients.empty(), "Team mute failed");
        mute.update({{"microphone", true}, {"all", false}, {"team", false}});
        groups.Request(1, mute); route(1, true);
        Check(recipients.empty(), "Muted sender transmitted");
        auto changeTeam = request("set_team"); changeTeam.update({{"group_id", gid}, {"session_id", 2}, {"team", 2}});
        groups.Operator(changeTeam);
        Check(state[2]["team"] == 2 && state[2]["transmit_channel_id"] == all, "Authoritative team update failed");
        phase["phase"] = "waiting"; groups.Operator(phase);
        Check(state[1]["transmit_channel_id"] == all && state[1]["mutes"]["microphone"] == true, "Lobby return changed mutes");
        groups.Route(stale, {boost::asio::ip::make_address("127.0.0.1"), 30001},
            [&](const auto&, const auto&){ recipients.insert(100); });
        Check(recipients.empty(), "Stale routing revision accepted");
        auto child = request("delete_channel"); child["channel_id"] = team1;
        error([&]{ groups.Operator(child); }, "delete_group_required");
        auto managerHello = request("hello"); managerHello["role"] = "manager";
        groups.Open(10, managerHello);
        error([&]{ groups.Open(11, managerHello); }, "manager_busy");
        auto managedRequest = request("create_group"); managedRequest.update({{"type", "game"}, {"teams", 2}});
        auto managed = groups.Request(10, managedRequest);
        for(int i = 0; i < 5; i++) groups.Request(10, managedRequest);
        Check(groups.Request(10, request("status"))["gfx_game_groups"] == 6,
              "The standalone server incorrectly imposes GFX's five-group limit");
        auto hello = request("hello");
        hello.update({{"role", "voice"}, {"name", "gfx"}, {"member_id", "player-1"},
            {"server_instance", groups.Instance()}, {"group_id", managed["group_id"]},
            {"channel_id", managed["all_channel_id"]}});
        groups.Open(12, hello);
        auto bind = request("bind_player");
        bind.update({{"group_id", managed["group_id"]}, {"session_id", 12}, {"member_id", "player-1"}, {"team", 1}});
        groups.Request(10, bind);
        Check(state[12]["mutes"] == Json({{"microphone",true},{"all",true},{"team",true}}), "Fresh mute defaults");
        groups.Remove(10);
        Check(closed.count(12), "Manager disconnect did not disconnect owned participants");
        auto details = request("group_details"); details["group_id"] = gid;
        Check(groups.Operator(details)["connected_players"] == 3, "Manual groups destroyed or people double counted");
        auto lobby = request("create_group"); lobby["type"] = "lobby";
        auto lobbyInfo = groups.Operator(lobby);
        Check(lobbyInfo["channels"].size() == 1 && lobbyInfo["team_channel_ids"].empty(), "Standalone Lobby shape");
        auto remove = request("delete_group"); remove["group_id"] = gid;
        groups.Operator(remove);
        Check(closed.count(1) && closed.count(2) && closed.count(3), "Group deletion missed participants");
        std::cout << "Voice group tests passed\n";
    }
    catch(const std::exception& error){
        std::cerr << error.what() << '\n'; return 1;
    }
}
