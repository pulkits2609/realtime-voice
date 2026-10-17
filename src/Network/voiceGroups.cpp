#include "Network/voiceGroups.hpp"
#include <iomanip>
#include <random>
#include <sstream>

VoiceGroups::VoiceGroups(Send send, Close close):
send(std::move(send)), close(std::move(close)){
    std::random_device random;
    instance = (static_cast<std::uint64_t>(random()) << 32) | random();
    if(instance == 0) instance = 1;
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << instance;
    instanceText = text.str();
}

std::string VoiceGroups::Instance() const{
    return instanceText;
}

std::uint64_t VoiceGroups::InstanceNumber() const{
    return instance;
}

VoiceGroups::Group& VoiceGroups::FindGroup(std::uint32_t id){
    auto found = groups.find(id);
    if(found == groups.end()){
        throw ProtocolError("group_missing", "The group no longer exists");
    }
    return found->second;
}

void VoiceGroups::RequireOwner(const Group& group, std::uint32_t caller, bool local){
    if(!local && group.owner != caller){
        throw ProtocolError("not_owner", "This management connection does not own the group");
    }
}

Json VoiceGroups::Open(std::uint32_t id, const Json& hello){
    std::lock_guard<std::mutex> lock(mutex);
    ValidateRequest(hello);
    if(Text(hello, "op") != "hello"){
        throw ProtocolError("hello_required", "The first request must be hello");
    }
    Session session;
    const auto role = Text(hello, "role");
    if(role == "manager"){
        if(manager != 0){
            throw ProtocolError("manager_busy", "One management connection is already active");
        }
        session.manager = true;
        session.name = "GFX management";
        manager = id;
    }
    else if(role == "voice"){
        session.name = Text(hello, "name");
        session.intent = hello;
        if(hello.contains("member_id")) Text(hello, "member_id");
        if(hello.contains("server_instance") && Text(hello, "server_instance", 16) != instanceText){
            throw ProtocolError("server_restarted", "The supplied server instance no longer exists");
        }
        auto& group = FindGroup(Number(hello, "group_id"));
        const auto channel = Number(hello, "channel_id");
        if(channel != group.all && std::find(group.teams.begin(), group.teams.end(), channel) == group.teams.end()){
            throw ProtocolError("invalid_channel", "The channel is not part of the requested group");
        }
        if(group.owner != 0){
            Text(hello, "member_id");
            if(!hello.contains("server_instance")){
                throw ProtocolError("invalid_request", "Managed joins require server_instance");
            }
        }
        if(hello.contains("mutes")){
            session.microphone = Boolean(hello["mutes"], "microphone");
            session.all = Boolean(hello["mutes"], "all");
            session.teamMuted = Boolean(hello["mutes"], "team");
        }
    }
    else{
        throw ProtocolError("invalid_request", "role must be voice or manager");
    }
    sessions.emplace(id, std::move(session));
    return {{"session_id", id}, {"server_instance", instanceText},
            {"role", role}, {"state", role == "voice" ? "pending" : "ready"},
            {"binding_required", role == "voice" && groups.at(Number(hello, "group_id")).owner != 0}};
}

Json VoiceGroups::Status(std::uint32_t id) const{
    const auto& session = sessions.at(id);
    Json result = {{"session_id", id}, {"server_instance", instanceText},
        {"state", session.group ? "joined" : "pending"}, {"group_id", session.group},
        {"team", session.team}, {"transmit_channel_id", session.transmit},
        {"send_revision", session.sendRevision}, {"receive_revision", session.receiveRevision},
        {"mutes", {{"microphone", session.microphone}, {"all", session.all}, {"team", session.teamMuted}}},
        {"receive_channels", Json::array()}, {"peers", Json::array()}};
    if(!session.group) return result;
    const auto& group = groups.at(session.group);
    result["group_type"] = group.type;
    result["phase"] = group.phase;
    result["all_channel_id"] = group.all;
    result["team_channel_id"] = session.team ? Json(group.teams.at(session.team - 1)) : Json(nullptr);
    result["receive_channels"].push_back(group.all);
    if(session.team) result["receive_channels"].push_back(group.teams.at(session.team - 1));
    for(const auto& [peerId, peer] : sessions){
        if(peer.group == session.group && peerId != id){
            result["peers"].push_back({{"session_id", peerId}, {"name", peer.name},
                {"team", peer.team}, {"send_revision", peer.sendRevision}});
        }
    }
    return result;
}

Json VoiceGroups::Details(const Group& group) const{
    Json result = {{"group_id", group.id}, {"name", group.name}, {"type", group.type},
        {"phase", group.phase}, {"owner", group.owner ? Json{{"type", "gfx"}, {"session_id", group.owner}}
                                                     : Json{{"type", "manual"}}},
        {"all_channel_id", group.all}, {"team_channel_ids", group.teams},
        {"connected_players", 0}, {"channels", Json::array()}, {"participants", Json::array()}};
    std::vector<std::uint32_t> channels{group.all};
    channels.insert(channels.end(), group.teams.begin(), group.teams.end());
    for(std::size_t index = 0; index < channels.size(); index++){
        unsigned receivers = 0, transmitters = 0;
        for(const auto& [id, session] : sessions){
            if(session.group != group.id) continue;
            if(index == 0 || session.team == index) receivers++;
            if(session.transmit == channels[index]) transmitters++;
        }
        result["channels"].push_back({{"channel_id", channels[index]},
            {"name", index == 0 ? (group.type == "game" ? "All" : "Lobby") : "Team " + std::to_string(index)},
            {"receive_members", receivers}, {"selected_transmitters", transmitters}});
    }
    for(const auto& [id, session] : sessions){
        if(session.group == group.id){
            result["participants"].push_back({{"session_id", id}, {"name", session.name},
                {"member_id", session.intent.value("member_id", session.name)},
                {"team", session.team}, {"transmit_channel_id", session.transmit}});
        }
    }
    result["connected_players"] = result["participants"].size();
    return result;
}

void VoiceGroups::Changed(std::uint32_t group){
    for(const auto& [id, session] : sessions){
        if(session.group == group){
            send(id, Event("state", Status(id)));
        }
    }
    if(manager) send(manager, Event("group_updated", {{"group_id", group}}));
}

void VoiceGroups::Join(std::uint32_t id, Group& group, std::uint32_t team, std::uint32_t channel){
    auto& session = sessions.at(id);
    if(session.manager || session.group){
        throw ProtocolError("already_joined", "Disconnect before joining another group");
    }
    if(group.type == "game" && (team == 0 || team > group.teams.size())){
        throw ProtocolError("invalid_team", "The team is outside this Game's team range");
    }
    if(group.type == "lobby" && team != 0){
        throw ProtocolError("invalid_team", "Lobby members use team 0");
    }
    if(channel != group.all && (team == 0 || channel != group.teams.at(team - 1))){
        throw ProtocolError("invalid_channel", "Only All or the assigned team is allowed");
    }
    unsigned count = 0;
    for(const auto& [otherId, other] : sessions){
        if(other.group == group.id) count++;
    }
    if(count >= 128) throw ProtocolError("group_full", "A group supports 128 connected players");
    session.group = group.id;
    session.team = team;
    session.transmit = group.phase == "waiting" ? group.all : channel;
    session.sendRevision++;
    session.receiveRevision++;
    Changed(group.id);
}

void VoiceGroups::Delete(std::uint32_t id, const std::string& reason){
    auto found = groups.find(id);
    if(found == groups.end()) return;
    for(auto& [sessionId, session] : sessions){
        if(session.group != id) continue;
        session.group = 0;
        session.transmit = 0;
        session.sendRevision++;
        session.receiveRevision++;
        send(sessionId, Event("group_deleted", {{"group_id", id}, {"reason", reason}}));
        close(sessionId);
    }
    groups.erase(found);
    if(manager) send(manager, Event("group_deleted", {{"group_id", id}, {"reason", reason}}));
}

Json VoiceGroups::Execute(std::uint32_t caller, const Json& request, bool local){
    ValidateRequest(request);
    const auto op = Text(request, "op");
    const bool management = local || (sessions.count(caller) && sessions.at(caller).manager);
    if(op == "status" && !management) return Status(caller);
    if(op == "join" && !management){
        auto& session = sessions.at(caller);
        auto& group = FindGroup(Number(request, "group_id"));
        if(group.owner) throw ProtocolError("binding_required", "Management must bind this voice session");
        if(Number(session.intent, "group_id") != group.id){
            throw ProtocolError("invalid_group", "Join must match the hello group");
        }
        Join(caller, group, Number(request, "team", true), Number(request, "channel_id"));
        return Status(caller);
    }
    if(op == "set_transmit" || op == "set_mutes"){
        if(management) throw ProtocolError("wrong_role", "This is a voice-session operation");
        auto& session = sessions.at(caller);
        if(!session.group) throw ProtocolError("not_joined", "The session is not joined");
        auto& group = FindGroup(session.group);
        if(op == "set_transmit"){
            const auto channel = Number(request, "channel_id");
            if(channel != group.all){
                if(!session.team || channel != group.teams.at(session.team - 1)){
                    throw ProtocolError("invalid_channel", "Only All and the assigned Team are allowed");
                }
                if(group.phase != "playing"){
                    throw ProtocolError("team_disabled", "Team transmission is disabled while waiting");
                }
            }
            if(channel != session.transmit){
                session.transmit = channel;
                session.sendRevision++;
            }
        }
        else{
            const bool mic = Boolean(request, "microphone");
            const bool all = Boolean(request, "all");
            const bool team = Boolean(request, "team");
            if(mic != session.microphone) session.sendRevision++;
            if(all != session.all || team != session.teamMuted) session.receiveRevision++;
            session.microphone = mic;
            session.all = all;
            session.teamMuted = team;
        }
        Changed(group.id);
        return Status(caller);
    }
    if(!management) throw ProtocolError("forbidden", "This operation belongs to server management");
    if(op == "create_group"){
        const auto type = Text(request, "type");
        if(type != "game" && type != "lobby"){
            throw ProtocolError("invalid_request", "type must be game or lobby");
        }
        if(!local && type != "game"){
            throw ProtocolError("invalid_request", "GFX management creates Game groups");
        }
        std::uint32_t teams = 0;
        if(type == "game"){
            teams = Number(request, "teams");
            if(teams < 2 || teams > 4){
                throw ProtocolError("invalid_team_count", "A Game requires exactly 2 to 4 teams");
            }
        }
        if(nextGroup == 0 || nextChannel == 0 || nextChannel > UINT32_MAX - teams){
            throw ProtocolError("ids_exhausted", "Restart the server to allocate new IDs");
        }
        Group group;
        group.id = nextGroup++;
        group.owner = local ? 0 : caller;
        group.type = type;
        group.name = request.contains("name") ? Text(request, "name") : type;
        group.all = nextChannel++;
        for(std::uint32_t i = 0; i < teams; i++) group.teams.push_back(nextChannel++);
        groups.emplace(group.id, group);
        if(manager) send(manager, Event("group_created", {{"group_id", group.id}, {"type", type}}));
        return Details(group);
    }
    if(op == "list_groups" || op == "status"){
        Json list = Json::array();
        const auto after = request.contains("after_id") ? Number(request, "after_id", true) : 0;
        unsigned owned = 0;
        for(const auto& [id, group] : groups){
            if(group.owner && group.type == "game") owned++;
            if(id > after && list.size() < 32){
                Json details = Details(group);
                details.erase("participants");
                list.push_back(std::move(details));
            }
        }
        return {{"server_instance", instanceText}, {"groups", list},
                {"gfx_game_groups", owned}, {"total_groups", groups.size()},
                {"next_after_id", list.size() == 32 ? list.back()["group_id"] : Json(nullptr)}};
    }
    if(op == "delete_channel"){
        throw ProtocolError("delete_group_required", "Delete the parent group, not an individual child channel");
    }
    if(op != "group_details" && op != "delete_group" && op != "set_phase" &&
       op != "bind_player" && op != "set_team" && op != "remove_player"){
        throw ProtocolError("unknown_operation", "Unknown management operation");
    }
    auto& group = FindGroup(Number(request, "group_id"));
    RequireOwner(group, caller, local);
    if(op == "group_details") return Details(group);
    if(op == "delete_group"){
        const auto id = group.id;
        Delete(id, local ? "operator_deleted" : "manager_deleted");
        return {{"group_id", id}};
    }
    if(op == "set_phase"){
        if(group.type != "game") throw ProtocolError("wrong_group_type", "Only Game groups have match phases");
        const auto phase = Text(request, "phase");
        if(phase != "waiting" && phase != "playing"){
            throw ProtocolError("invalid_phase", "phase must be waiting or playing");
        }
        if(group.phase != phase){
            group.phase = phase;
            for(auto& [id, session] : sessions){
                if(session.group != group.id) continue;
                session.transmit = phase == "playing" ? group.teams.at(session.team - 1) : group.all;
                session.sendRevision++;
                session.receiveRevision++;
            }
            Changed(group.id);
        }
        return Details(group);
    }
    if(op == "bind_player"){
        const auto id = Number(request, "session_id");
        auto found = sessions.find(id);
        if(found == sessions.end() || found->second.manager){
            throw ProtocolError("session_missing", "No pending voice session has this ID");
        }
        auto& session = found->second;
        const auto member = Text(request, "member_id");
        if(group.owner == 0 || Number(session.intent, "group_id") != group.id ||
           session.intent.value("member_id", "") != member){
            throw ProtocolError("binding_mismatch", "Bind must match the pending group's player identifier");
        }
        for(const auto& [otherId, other] : sessions){
            if(otherId != id && other.group == group.id && other.intent.value("member_id", "") == member){
                throw ProtocolError("member_in_use", "Remove the previous membership before approving a rejoin");
            }
        }
        const auto team = Number(request, "team");
        if(team > group.teams.size()) throw ProtocolError("invalid_team", "Team does not exist");
        Join(id, group, team, group.phase == "playing" ? group.teams.at(team - 1) : group.all);
        return Status(id);
    }
    if(op == "set_team" || op == "remove_player"){
        const auto id = Number(request, "session_id");
        auto found = sessions.find(id);
        if(found == sessions.end() || found->second.group != group.id){
            throw ProtocolError("session_missing", "The voice session is not a member of this group");
        }
        auto& session = found->second;
        if(op == "remove_player"){
            session.group = 0;
            session.transmit = 0;
            session.sendRevision++;
            session.receiveRevision++;
            send(id, Event("membership_removed", {{"group_id", group.id}}));
            close(id);
        }
        else{
            if(group.type != "game") throw ProtocolError("wrong_group_type", "Lobby groups have no teams");
            const auto team = Number(request, "team");
            if(team > group.teams.size()) throw ProtocolError("invalid_team", "Team does not exist");
            if(team != session.team){
                const bool wasTeam = session.transmit != group.all;
                session.team = team;
                session.transmit = group.phase == "playing" && wasTeam ? group.teams.at(team - 1) : group.all;
                session.sendRevision++;
                session.receiveRevision++;
            }
        }
        Changed(group.id);
        return {{"session_id", id}};
    }
    throw ProtocolError("unknown_operation", "Unknown management operation");
}

Json VoiceGroups::Request(std::uint32_t id, const Json& request){
    std::lock_guard<std::mutex> lock(mutex);
    if(!sessions.count(id)) throw ProtocolError("session_missing", "The session has ended");
    return Execute(id, request, false);
}

Json VoiceGroups::Operator(const Json& request){
    std::lock_guard<std::mutex> lock(mutex);
    return Execute(0, request, true);
}

void VoiceGroups::Remove(std::uint32_t id){
    std::lock_guard<std::mutex> lock(mutex);
    auto found = sessions.find(id);
    if(found == sessions.end()) return;
    const auto group = found->second.group;
    if(found->second.manager){
        manager = 0;
        std::vector<std::uint32_t> owned;
        for(const auto& [groupId, value] : groups){
            if(value.owner == id) owned.push_back(groupId);
        }
        for(auto groupId : owned) Delete(groupId, "manager_disconnected");
    }
    sessions.erase(id);
    if(group && groups.count(group)) Changed(group);
}

void VoiceGroups::Route(RoutedPacket packet, const boost::asio::ip::udp::endpoint& endpoint, const Relay& relay){
    std::lock_guard<std::mutex> lock(mutex);
    auto found = sessions.find(packet.GetClientId());
    if(packet.GetType() != PacketType::Voice || packet.GetInstance() != instance ||
       found == sessions.end() || !found->second.group) return;
    auto& sender = found->second;
    if(packet.GetGroupId() != sender.group || packet.GetChannelId() != sender.transmit ||
       packet.GetSendRevision() != sender.sendRevision || packet.GetReceiveRevision() != 0) return;
    if(sender.udpRegistered && sender.endpoint != endpoint) return;
    sender.endpoint = endpoint;
    sender.udpRegistered = true;
    if(packet.GetPayload().empty() || sender.microphone) return;
    const auto& group = groups.at(sender.group);
    const bool all = sender.transmit == group.all;
    if(!all && group.phase != "playing") return;
    // Hold routing state through nonblocking sends: deletion cannot race a recipient snapshot.
    for(const auto& [id, receiver] : sessions){
        if(id == found->first || receiver.group != sender.group || !receiver.udpRegistered) continue;
        if(all ? receiver.all : (receiver.team != sender.team || receiver.teamMuted)) continue;
        packet.SetContext(instance, sender.group, sender.transmit, sender.sendRevision, receiver.receiveRevision);
        relay(packet, receiver.endpoint);
    }
}
