#include "Network/client.hpp"
#include <iostream>
#include "Network/packet.hpp"
#include <thread>
#include <array>
#include "Common/debugLog.hpp"
#include <chrono>
#include <stdexcept>
#include <csignal>
#include <algorithm>
#include <cmath>

Client::Client(const std::string& serverAddress, unsigned short serverPort,
               const std::string& clientName, Json joinInfo,
               std::shared_ptr<MuteState> mutes, std::function<void(const Json&)> emit,
               bool audioEnabled):
socket(io_context), serverEndpoint(boost::asio::ip::udp::v4(), serverPort),
joinInfo(std::move(joinInfo)), mutes(std::move(mutes)), emit(std::move(emit)),
audioEnabled(audioEnabled), captureQueue(8), pcmBuffer(9600), networkQueue(128),
serverAddress(serverAddress), clientName(clientName), controlClient(io_context), controlQueue(128){

}

void Client::Run(){
    io_context.restart();
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([this](const boost::system::error_code& error, int){
        if(!error) RequestStop();
    });
    try{
        Json hello = joinInfo;
        hello.update({{"v", 2}, {"id", "__hello"}, {"op", "hello"}, {"role", "voice"},
                      {"name", clientName}, {"mutes", mutes->Snapshot()}});
        const auto connected = controlClient.Connect(serverAddress, serverEndpoint.port(), hello, running);
        clientId = Number(connected, "session_id");
        serverInstance = std::stoull(Text(connected, "server_instance", 16), nullptr, 16);
        session = connected;
        session["state"] = "pending";
        Publish();
        emit(Event("session_ready", GetStatus()));
        serverEndpoint.address(controlClient.GetServerAddress());
        controlClient.StartReceive([this](const Json& event){
            if(!controlQueue.TryPush(event)) running = false;
            NotifyWork();
        });
        socket.Open();
        socket.Bind(0);
        socket.SetNonBlocking(true);
        if(!encoder.Initialize(48000, 1, 32000)){
            throw ProtocolError("codec_failed", "Failed to initialize Opus encoder");
        }
        receiveThread = std::thread(&Client::ReceiveVoice, this);
        if(!connected.at("binding_required").get<bool>()){
            controlClient.Post({{"v", 2}, {"id", "__join"}, {"op", "join"},
                {"group_id", Number(joinInfo, "group_id")}, {"channel_id", Number(joinInfo, "channel_id")},
                {"team", joinInfo.contains("team") ? Number(joinInfo, "team", true) : 0}});
        }

        std::array<float,FrameSamples> frame{};
        std::array<std::uint8_t,4000> encodedData{};
        std::uint32_t sequenceNumber = 0;
        auto nextUdpKeepalive = Clock::now();
        auto nextPlayback = nextUdpKeepalive + FrameDuration;
        while(running){
            {
                std::lock_guard<std::mutex> lock(workMutex);
                workPending = false;
            }
            io_context.poll();
            ProcessControlEvents();
            ProcessCommands();
            if(!running) break;
            if(groupId && audioEnabled && !audioStarted) StartAudio();
            const auto now = Clock::now();
            if(groupId && now >= nextUdpKeepalive){
                RoutedPacket keepalive(PacketType::Voice, std::vector<std::uint8_t>{});
                keepalive.SetClientId(clientId);
                keepalive.SetContext(serverInstance, groupId, session.at("transmit_channel_id"), sendRevision);
                socket.SendTo(keepalive.Serialize(), serverEndpoint);
                nextUdpKeepalive = now + std::chrono::seconds(2);
            }
            ProcessReceivedVoice();
            if(!running) break;
            const auto playbackNow = Clock::now();
            if(playbackNow >= nextPlayback){
                const auto skippedFrames = (playbackNow - nextPlayback) / FrameDuration;
                nextPlayback += FrameDuration * (skippedFrames + 1);
                ProcessPlayback(playbackNow, static_cast<std::uint64_t>(skippedFrames));
            }
            CapturedBlock capturedSamples;
            if(captureQueue.Pop(capturedSamples) &&
               capturedSamples.generation == mutes->captureGeneration.load() && !mutes->microphone){
                pcmBuffer.Push(capturedSamples.samples.data(), capturedSamples.samples.size());
            }
            if(groupId && !mutes->microphone && !session.at("mutes").at("microphone").get<bool>() &&
               pcmBuffer.PopExact(frame.data(), frame.size())){
                const int encodedBytes = encoder.Encode(frame.data(), static_cast<int>(frame.size()),
                    encodedData.data(), static_cast<int>(encodedData.size()));
                if(encodedBytes < 0){
                    std::cerr << "Opus encode failed: " << opus_strerror(encodedBytes) << '\n';
                    continue;
                }
                const std::vector<std::uint8_t> opusPayload(encodedData.begin(), encodedData.begin() + encodedBytes);
                RoutedPacket voicePacket(PacketType::Voice, opusPayload);
                voicePacket.SetClientId(clientId);
                voicePacket.SetSequenceNumber(++sequenceNumber);
                voicePacket.SetContext(serverInstance, groupId, session.at("transmit_channel_id"), sendRevision);
                if(!mutes->microphone) socket.SendTo(voicePacket.Serialize(), serverEndpoint);
            }
            if(captureQueue.Size() > 0 || networkQueue.Size() > 0 || controlQueue.Size() > 0 || commands.Size() > 0){
                continue;
            }
            std::unique_lock<std::mutex> lock(workMutex);
            workCondition.wait_until(lock, nextPlayback, [this]{ return workPending || !running; });
        }
    }
    catch(const ProtocolError& error){
        if(running) emit(Event("error", {{"code", error.code}, {"message", error.what()}}));
    }
    catch(const std::exception& error){
        if(running) emit(Event("error", {{"code", "connection_failed"}, {"message", error.what()}}));
    }
    Stop();
    session["state"] = "disconnected";
    Publish();
    emit(Event("disconnected", GetStatus()));
    finished = true;
}

void Client::StartAudio(){
    if(!audioCapture.Initialize([this](const float* samples, std::size_t count){ HandleCapture(samples, count); }) ||
       !audioCapture.Start() || !audioPlayback.Initialize() || !audioPlayback.Start()){
        throw ProtocolError("audio_device_failed", "Failed to initialize/start default microphone or playback device");
    }
    audioStarted = true;
    Publish();
}

void Client::HandleCapture(const float* samples, std::size_t sampleCount){
    const auto generation = mutes->captureGeneration.load();
    if(mutes->microphone) return;
    captureQueue.Push(CapturedBlock{generation, std::vector<float>(samples, samples + sampleCount)});
    NotifyWork();
}

void Client::RequestStop(){
    running = false;
    audioPlayback.SetEnabled(false);
    NotifyWork();
}

void Client::Post(const Json& request){
    if(request.at("op") == "set_mutes"){
        mutes->Set({{"microphone", Boolean(request, "microphone")},
                    {"all", Boolean(request, "all")}, {"team", Boolean(request, "team")}});
        audioPlayback.SetEnabled(false);
    }
    if(!running || !commands.TryPush(request)){
        throw ProtocolError("not_connected", "Voice is disconnected or its command queue is full");
    }
    NotifyWork();
}

void Client::Publish(){
    Json status = session;
    if(!status.contains("group_id")) status["group_id"] = 0;
    if(!status.contains("transmit_channel_id")) status["transmit_channel_id"] = nullptr;
    if(!status.contains("receive_channels")) status["receive_channels"] = Json::array();
    status["requested_group_id"] = joinInfo.value("group_id", 0u);
    status["member_id"] = joinInfo.value("member_id", clientName);
    status["mutes"] = mutes->Snapshot();
    status["audio_enabled"] = audioEnabled;
    status["audio_initialized"] = audioStarted;
    status["decoded_frames"] = decodedFrames.load();
    status["mixed_frames"] = mixedFrames.load();
    std::lock_guard<std::mutex> lock(statusMutex);
    published = std::move(status);
}

Json Client::GetStatus() const{
    std::lock_guard<std::mutex> lock(statusMutex);
    Json status = published;
    status["mutes"] = mutes->Snapshot();
    status["decoded_frames"] = decodedFrames.load();
    status["mixed_frames"] = mixedFrames.load();
    return status;
}

void Client::ClearAudio(){
    captureQueue.Clear();
    pcmBuffer.Clear();
    networkQueue.Clear();
    audioPlayback.Clear();
    remoteClients.clear();
}

void Client::ProcessCommands(){
    Json request;
    while(commands.Pop(request)){
        if(request.at("op") == "set_mutes" && groupId == 0){
            emit(Success(request, {{"mutes", mutes->Snapshot()}, {"applied", false}}));
            emit(Event("mute_changed", mutes->Snapshot()));
            continue;
        }
        try{
            if(pendingRequests.size() >= 128){
                throw ProtocolError("too_many_requests", "At most 128 control requests may be outstanding");
            }
            controlClient.Post(request);
            pendingRequests.insert(Text(request, "id"));
        }
        catch(const ProtocolError& error){
            emit(Failure(request, error.code, error.what()));
        }
    }
}

void Client::ReceiveVoice(){
    boost::asio::ip::udp::endpoint sender;

    while(running){
        try{
            std::vector<std::uint8_t> data =
                socket.ReceiveFrom(sender);

            if(data.empty() || sender != serverEndpoint){
                continue;
            }

            networkQueue.Push(std::move(data));
            NotifyWork();
        }
        catch(const boost::system::system_error& error){
            if(!running){
                break;
            }

            const auto code = error.code();

            if(
                code == boost::asio::error::would_block ||
                code == boost::asio::error::try_again
            ){
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(2)
                );
                continue;
            }

            // Discard an oversized datagram or a transient
            // UDP reset. TCP heartbeat determines session life.
            if(
                code == boost::asio::error::message_size ||
                code == boost::asio::error::connection_reset
            ){
                continue;
            }

            std::cerr
                <<"UDP receive failed: "
                <<error.what()
                <<"\n";

            running = false;
        }
        catch(const std::exception& error){
            std::cerr
                <<"Voice receiver failed: "
                <<error.what()
                <<"\n";

            running = false;
        }
    }

    NotifyWork();
}

//old receive processing is now moved into client processing thread
void Client::ProcessReceivedVoice(){

    std::vector<std::uint8_t> data;

    // Process a bounded batch so incoming traffic cannot starve playback.
    const std::size_t packetCount = networkQueue.Size();

    for(std::size_t i = 0; i < packetCount; i++){
        if(!networkQueue.Pop(data)){
            break;
        }

        ProcessControlEvents();

        if(!running){
            return;
        }

        RoutedPacket packet;

        if(!packet.Deserialize(data)){

            std::cerr
                <<"Received Invalid Voice packet\n";

            continue;
        }

        if(packet.GetType() != PacketType::Voice){
            continue;
        }

        const std::uint32_t senderClientId =
            packet.GetClientId();

        //ignore our own packets if they somehow
        //reach us
        if(senderClientId == 0 || senderClientId == clientId || activeClients.find(senderClientId) == activeClients.end()){
            continue;
        }

        const std::uint32_t sequenceNumber =
            packet.GetSequenceNumber();

        const std::vector<std::uint8_t>& payload =
            packet.GetPayload();

        if(payload.empty()){
            continue;
        }

        // This protocol uses exactly one 20 ms frame per voice packet.
        if(
            opus_packet_get_nb_samples(
                payload.data(),
                static_cast<opus_int32>(payload.size()),
                48000
            ) != static_cast<int>(FrameSamples)
        ){
            continue;
        }

        if(packet.GetInstance() != serverInstance || packet.GetGroupId() != groupId ||
           packet.GetReceiveRevision() != receiveRevision ||
           packet.GetSendRevision() != peerRevisions.at(senderClientId)) continue;
        const auto channel = packet.GetChannelId();
        if(channel == allChannel){
            if(mutes->all || session.at("mutes").at("all").get<bool>()) continue;
        }
        else if(channel == teamChannel && teamChannel != 0){
            if(mutes->team || session.at("mutes").at("team").get<bool>() ||
               peerTeams.at(senderClientId) != session.at("team")) continue;
        }
        else continue;
        const auto streamId = (static_cast<std::uint64_t>(senderClientId) << 32) | channel;

        //get this remote client's audio state
        auto [
            remoteClientIterator,
            inserted
        ] = remoteClients.try_emplace(
            streamId
        );

        RemoteClientState& remoteClient =
            remoteClientIterator->second;

        //initialize decoder when we first
        //see this remote client
        if(inserted){

            if(!remoteClient.decoder.Initialize(
                48000,
                1
            )){

                std::cerr
                    <<"Failed to initialize decoder for Client "
                    <<senderClientId
                    <<"\n";

                remoteClients.erase(
                    remoteClientIterator
                );

                continue;
            }

            DEBUG_LOG(
                "Created Audio State for Client "
                <<senderClientId
                <<"\n"
            );
        }

        const auto now = Clock::now();
        bool rebuffer = !remoteClient.receivedFirstPacket;

        const std::uint32_t sequenceDistance =
            sequenceNumber - remoteClient.expectedSequenceNumber;

        if(remoteClient.receivedFirstPacket){
            if(sequenceDistance >= 0x80000000u){
                DEBUG_LOG(
                    "Late / Duplicate Packet : Client "
                    <<senderClientId
                    <<" : sequence "
                    <<sequenceNumber
                    <<"\n"
                );

                continue;
            }

            rebuffer =
                sequenceDistance >= JitterCapacity ||
                remoteClient.playbackStart == Clock::time_point::max();
        }

        //first packet establishes this client's
        //sequence starting point
        if(rebuffer){
            remoteClient.jitterBuffer.Clear();
            remoteClient.expectedSequenceNumber = sequenceNumber;
            remoteClient.receivedFirstPacket = true;
        }

        //store packet inside this client's
        //jitter buffer
        if(!remoteClient.jitterBuffer.Push(
            sequenceNumber,
            payload
        )){
            DEBUG_LOG(
                "Duplicate / Full Jitter Buffer : Client "<<senderClientId<<" : sequence "<<sequenceNumber<<"\n"
            );

            continue;
        }

        if(rebuffer){
            RebufferRemoteClient(
                remoteClient,
                now
            );
        }
    }
}

Client::~Client(){
    Stop();
}

void Client::ProcessControlEvents(){
    Json message;
    while(controlQueue.Pop(message)){
        if(message.contains("event")){
            const auto event = Text(message, "event");
            if(event == "state"){
                const auto& state = message.at("data");
                const auto nextGroup = Number(state, "group_id", true);
                const auto nextSend = Number(state, "send_revision");
                const auto nextReceive = Number(state, "receive_revision");
                const bool joined = groupId == 0 && nextGroup != 0;
                const auto oldTransmit = session.value("transmit_channel_id", 0u);
                if(nextGroup != groupId || nextReceive != receiveRevision){
                    ClearAudio();
                }
                if(nextSend != sendRevision){
                    mutes->captureGeneration++;
                    captureQueue.Clear();
                    pcmBuffer.Clear();
                    encoder.Reset();
                }
                std::map<std::uint32_t, std::uint32_t> revisions, teams;
                activeClients.clear();
                for(const auto& peer : state.at("peers")){
                    const auto id = Number(peer, "session_id");
                    activeClients[id] = Text(peer, "name");
                    revisions[id] = Number(peer, "send_revision");
                    teams[id] = Number(peer, "team", true);
                }
                for(auto iterator = remoteClients.begin(); iterator != remoteClients.end();){
                    const auto id = static_cast<std::uint32_t>(iterator->first >> 32);
                    if(!revisions.count(id) || !peerRevisions.count(id) ||
                       revisions[id] != peerRevisions[id]){
                        iterator = remoteClients.erase(iterator);
                        audioPlayback.Clear();
                    }
                    else ++iterator;
                }
                peerRevisions = std::move(revisions);
                peerTeams = std::move(teams);
                session = state;
                groupId = nextGroup; sendRevision = nextSend; receiveRevision = nextReceive;
                allChannel = Number(state, "all_channel_id");
                teamChannel = state.at("team_channel_id").is_null() ? 0 : Number(state, "team_channel_id");
                if(joined){
                    if(audioEnabled) StartAudio();
                    if(state.at("mutes") != mutes->Snapshot()){
                        auto sync = mutes->Snapshot();
                        sync.update({{"v", 2}, {"id", "__sync"}, {"op", "set_mutes"}});
                        controlClient.Post(sync);
                    }
                }
                Publish();
                if(joined) emit(Event("connected", GetStatus()));
                if(oldTransmit != session.at("transmit_channel_id")){
                    emit(Event("transmit_changed", GetStatus()));
                }
                emit(Event("status", GetStatus()));
            }
            else if(event == "group_deleted" || event == "membership_removed" || event == "server_disconnected"){
                ClearAudio();
                groupId = 0;
                RequestStop();
                session["state"] = "disconnected";
                session["group_id"] = 0;
                session["transmit_channel_id"] = nullptr;
                session["receive_channels"] = Json::array();
                Publish();
                emit(message);
                return;
            }
        }
        else{
            const auto id = Text(message, "id");
            if(id.rfind("__", 0) == 0){
                if(!message.value("ok", false)){
                    throw ProtocolError(message.at("error").at("code"), message.at("error").at("message"));
                }
                continue;
            }
            pendingRequests.erase(id);
            emit(message);
            if(message.value("ok", false) && message.at("data").contains("mutes")){
                emit(Event("mute_changed", mutes->Snapshot()));
            }
        }
    }
}

void Client::Stop(){
    running = false;

    // Stop capture callbacks while their queues still exist.
    audioCapture.Stop();
    audioPlayback.Stop();
    audioStarted = false;

    controlClient.Disconnect();

    if(receiveThread.joinable()){
        receiveThread.join();
    }

    // The UDP receiver has finished before socket closure.
    socket.Close();

    ClearAudio();
    for(const auto& id : pendingRequests){
        emit(Failure(Json{{"id", id}}, "disconnected", "Voice disconnected before the response arrived"));
    }
    pendingRequests.clear();
    Json request;
    while(commands.Pop(request)) emit(Failure(request, "disconnected", "Voice disconnected"));
    activeClients.clear();
    remoteClients.clear();
    groupId = 0;
    session["group_id"] = 0;
    session["transmit_channel_id"] = nullptr;
    session["receive_channels"] = Json::array();
    session["peers"] = Json::array();
}

void Client::RebufferRemoteClient(
    RemoteClientState& remoteClient,
    Clock::time_point now
){
    if(!remoteClient.decoder.Reset()){
        throw std::runtime_error(
            "Failed to reset Opus decoder"
        );
    }

    remoteClient.jitterBufferStarted = false;
    remoteClient.consecutiveLosses = 0;
    remoteClient.jitterBuffer.DiscardBefore(
        remoteClient.expectedSequenceNumber
    );

    std::uint32_t nextSequenceNumber = 0;

    if(remoteClient.jitterBuffer.GetNextSequence(
        remoteClient.expectedSequenceNumber,
        nextSequenceNumber
    )){
        remoteClient.expectedSequenceNumber = nextSequenceNumber;
        remoteClient.playbackStart = now + JitterDelay;
    }
    else{
        // Wait for fresh data after prolonged loss.
        remoteClient.playbackStart = Clock::time_point::max();
    }
}

void Client::ProcessPlayback(
    Clock::time_point now,
    std::uint64_t skippedFrames
){
    std::array<float,FrameSamples> mixedFrame{};
    bool hasAudio = false;

    for(auto& [senderClientId, remoteClient] : remoteClients){
        const auto channel = static_cast<std::uint32_t>(senderClientId);
        if((channel == allChannel && mutes->all) || (channel == teamChannel && mutes->team)) continue;
        if(
            !remoteClient.receivedFirstPacket ||
            now < remoteClient.playbackStart
        ){
            continue;
        }

        // During startup, count only slots expired for this stream.
        const std::uint64_t expiredFrames =
            remoteClient.jitterBufferStarted
                ? skippedFrames
                : static_cast<std::uint64_t>(
                    (now - remoteClient.playbackStart) / FrameDuration
                );

        if(expiredFrames > 0){
            if(expiredFrames >= MaxConsecutiveLosses){
                // The sender may have paused too. Keep the sequence floor
                // and restart from fresh data instead of guessing its clock.
                remoteClient.jitterBuffer.Clear();
                RebufferRemoteClient(remoteClient, now);
                continue;
            }

            // Skip expired slots without producing a catch-up burst.
            remoteClient.expectedSequenceNumber +=
                static_cast<std::uint32_t>(expiredFrames);

            remoteClient.jitterBuffer.DiscardBefore(
                remoteClient.expectedSequenceNumber
            );

            if(!remoteClient.decoder.Reset()){
                throw std::runtime_error(
                    "Failed to reset Opus decoder"
                );
            }

            remoteClient.consecutiveLosses = 0;
        }

        if(!remoteClient.jitterBufferStarted){
            remoteClient.jitterBufferStarted = true;

            DEBUG_LOG(
                "Jitter Buffer Started for Client "
                <<senderClientId
                <<"\n"
            );
        }

        std::array<float,FrameSamples> decodedFrame{};
        std::vector<std::uint8_t> nextPayload;
        int decodedSamples = 0;

        //try to get exactly the packet
        //we are expecting
        if(remoteClient.jitterBuffer.Pop(
            remoteClient.expectedSequenceNumber,
            nextPayload
        )){
            decodedSamples = remoteClient.decoder.Decode(
                nextPayload.data(),
                static_cast<int>(nextPayload.size()),
                decodedFrame.data(),
                static_cast<int>(decodedFrame.size())
            );

            if(decodedSamples != static_cast<int>(FrameSamples)){
                std::cerr
                    <<"Opus Decoding Failed : "
                    <<decodedSamples
                    <<"\n";
            }
            else{
                remoteClient.consecutiveLosses = 0;
                remoteClient.frameNumber++;
                decodedFrames++;

                DEBUG_LOG(
                    "Received Voice Packet : Client "
                    <<senderClientId
                    <<" : frame "
                    <<remoteClient.frameNumber
                    <<" : sequence "
                    <<remoteClient.expectedSequenceNumber
                    <<" : "
                    <<nextPayload.size()
                    <<"bytes, decoded "
                    <<decodedSamples
                    <<" samples\n"
                );
            }
        }

        // The deadline has arrived: missing or invalid data uses PLC.
        if(decodedSamples != static_cast<int>(FrameSamples)){
            decodedFrame.fill(0.0f);

            //tell Opus that the packet was lost
            decodedSamples = remoteClient.decoder.Decode(
                nullptr,
                0,
                decodedFrame.data(),
                static_cast<int>(decodedFrame.size())
            );

            remoteClient.consecutiveLosses++;

            if(decodedSamples < 0){
                std::cerr
                    <<"Opus PLC Failed : "<<opus_strerror(
                        decodedSamples
                    )<<"\n";
            }

            DEBUG_LOG(
                "Generated PLC Audio : Client "<<senderClientId<<" : sequence "<<remoteClient.expectedSequenceNumber<<"\n"
            );
        }

        if(decodedSamples == static_cast<int>(FrameSamples)){
            for(std::size_t i = 0; i < mixedFrame.size(); i++){
                mixedFrame[i] += decodedFrame[i];
            }

            hasAudio = true;
        }

        remoteClient.expectedSequenceNumber++;

        if(remoteClient.consecutiveLosses >= MaxConsecutiveLosses){
            RebufferRemoteClient(remoteClient, now);
        }
    }

    if(!hasAudio){
        return;
    }

    // Preserve relative speaker levels while keeping the sum in range.
    float peak = 1.0f;

    for(const float sample : mixedFrame){
        peak = std::max(peak, std::abs(sample));
    }

    if(peak > 1.0f){
        for(float& sample : mixedFrame){
            sample /= peak;
        }
    }

    // One mixed frame per deadline, regardless of the speaker count.
    const bool enabled = session.contains("mutes") &&
        session["mutes"]["all"] == mutes->all.load() &&
        session["mutes"]["team"] == mutes->team.load();
    audioPlayback.SetEnabled(enabled);
    mixedFrames++;
    audioPlayback.Push(
        mixedFrame.data(),
        mixedFrame.size()
    );
}

void Client::NotifyWork(){
    {
        std::lock_guard<std::mutex> lock(
            workMutex
        );
        workPending = true;
    }

    workCondition.notify_one();
}
