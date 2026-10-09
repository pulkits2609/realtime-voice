#include "Network/client.hpp"
#include <iostream>
#include "Network/packet.hpp"
#include <thread>
#include <array>
#include "Common/debugLog.hpp"
#include <chrono>
#include <stdexcept>

Client::Client(
    const std::string& serverAddress,
    unsigned short serverPort,
    const std::string& clientName
):
socket(io_context),
serverEndpoint(
    boost::asio::ip::make_address(serverAddress),
    serverPort
),
audioCapture(),
captureQueue(8),
pcmBuffer(9600),
encoder(),
audioPlayback(),
networkQueue(8),
clientName(clientName),
controlClient(io_context),
controlQueue(0){

}

void Client::SendMessage(const std::string& message){
    // socket.SendTo(message, serverEndpoint);
    Packet packet(
        PacketType::Text,
        message
    );
    const std::vector<std::uint8_t> data= packet.Serialize();

    socket.SendTo(
        data,
        serverEndpoint
    );
}

std::string Client::ReceiveMessage(){
    // return socket.ReceiveFrom(serverEndpoint);

    const std::vector<std::uint8_t> data = socket.ReceiveFrom(
        serverEndpoint
    );

    Packet packet;
    if(!packet.Deserialize(data)){
        return "Invalid Packet";
    }

    return packet.GetTextMessage();
}

void Client::Run(){
    running = true;

    try{
        if(!controlClient.Connect(
            serverEndpoint.address().to_string(),
            serverEndpoint.port(),
            clientName
        )){
            throw std::runtime_error(
                "Unable to connect to voice server"
            );
        }

        clientId = controlClient.GetClientId();

        std::cout
            <<"Connected to server as : "
            <<clientName
            <<" | Client ID: "
            <<clientId
            <<"\n";

        controlClient.StartReceive(
            [this](
                ControlMessageType type,
                std::uint32_t remoteClientId,
                const std::string& remoteClientName
            ){
                controlQueue.Push(
                    ControlEvent{
                        type,
                        remoteClientId,
                        remoteClientName
                    }
                );

                if(type == ControlMessageType::ServerDisconnected){
                    running = false;
                }
            }
        );

        if(!encoder.Initialize(48000, 1, 32000)){
            throw std::runtime_error(
                "Failed to initialize Opus encoder"
            );
        }

        socket.Open();
        socket.Bind(0);
        socket.SetNonBlocking(true);

        if(!audioCapture.Initialize(
            [this](
                const float* samples,
                std::size_t sampleCount
            ){
                HandleCapture(samples, sampleCount);
            }
        )){
            throw std::runtime_error(
                "Failed to initialize microphone"
            );
        }

        if(!audioCapture.Start()){
            throw std::runtime_error(
                "Failed to start microphone"
            );
        }

        if(!audioPlayback.Initialize()){
            throw std::runtime_error(
                "Failed to initialize audio playback"
            );
        }

        if(!audioPlayback.Start()){
            throw std::runtime_error(
                "Failed to start audio playback"
            );
        }

        if(!running){
            throw std::runtime_error(
                "Server connection was lost during startup"
            );
        }

        receiveThread = std::thread(
            &Client::ReceiveVoice,
            this
        );

        std::array<float,960> frame{};
        std::array<std::uint8_t,4000> encodedData{};

        std::uint32_t sequenceNumber = 0;

        while(running){
            ProcessControlEvents();

            if(!running){
                break;
            }

            std::vector<float> capturedSamples;

            if(captureQueue.Pop(capturedSamples)){
                pcmBuffer.Push(
                    capturedSamples.data(),
                    capturedSamples.size()
                );
            }

            if(pcmBuffer.PopExact(
                frame.data(),
                frame.size()
            )){
                const int encodedBytes = encoder.Encode(
                    frame.data(),
                    static_cast<int>(frame.size()),
                    encodedData.data(),
                    static_cast<int>(encodedData.size())
                );

                if(encodedBytes < 0){
                    std::cerr
                        <<"Opus encode failed: "
                        <<opus_strerror(encodedBytes)
                        <<"\n";

                    continue;
                }

                const std::vector<std::uint8_t> opusPayload(
                    encodedData.begin(),
                    encodedData.begin() + encodedBytes
                );

                Packet voicePacket(
                    PacketType::Voice,
                    opusPayload
                );

                voicePacket.SetClientId(clientId);
                voicePacket.SetSequenceNumber(++sequenceNumber);

                socket.SendTo(
                    voicePacket.Serialize(),
                    serverEndpoint
                );
            }

            ProcessReceivedVoice();
        }

        // including a final server-disconnect notification
        // if it causes the processing loop to exit
        ProcessControlEvents();

        Stop();
    }
    catch(...){
        Stop();
        throw;
    }
}

void Client::HandleCapture(
    const float* samples,
    std::size_t sampleCount
){
    std::vector<float> capturedSamples(
        samples,
        samples + sampleCount
    );

    captureQueue.Push(
        std::move(capturedSamples)
    );
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
}

//old receive processing is now moved into client processing thread
void Client::ProcessReceivedVoice(){

    std::vector<std::uint8_t> data;

    while(networkQueue.Pop(
        data
    )){

        ProcessControlEvents();

        if(!running){
            return;
        }

        Packet packet;

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

        //get this remote client's audio state
        auto [
            remoteClientIterator,
            inserted
        ] = remoteClients.try_emplace(
            senderClientId
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

        //first packet establishes this client's
        //sequence starting point
        if(!remoteClient.receivedFirstPacket){

            remoteClient.expectedSequenceNumber =
                sequenceNumber;

            remoteClient.receivedFirstPacket = true;
        }

        const std::uint32_t sequenceDistance =
            sequenceNumber - remoteClient.expectedSequenceNumber;

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

        //wait until three packets are buffered
        if(
            !remoteClient.jitterBufferStarted &&
            remoteClient.jitterBuffer.Size() >= 3
        ){

            remoteClient.jitterBufferStarted = true;

            DEBUG_LOG(
                "Jitter Buffer Started for Client "
                <<senderClientId
                <<"\n"
            );
        }

        if(!remoteClient.jitterBufferStarted){
            continue;
        }

        std::array<float,960> decodedFrame{};

        while(true){

            std::vector<std::uint8_t> nextPayload;

            //try to get exactly the packet
            //we are expecting
            if(remoteClient.jitterBuffer.Pop(
                remoteClient.expectedSequenceNumber,
                nextPayload
            )){

                const int decodedSamples =
                    remoteClient.decoder.Decode(
                        nextPayload.data(),
                        static_cast<int>(
                            nextPayload.size()
                        ),
                        decodedFrame.data(),
                        static_cast<int>(
                            decodedFrame.size()
                        )
                    );

                if(decodedSamples < 0){

                    std::cerr
                        <<"Opus Decoding Failed : "
                        <<opus_strerror(
                            decodedSamples
                        )
                        <<"\n";

                    remoteClient.expectedSequenceNumber++;

                    continue;
                }

                audioPlayback.Push(
                    decodedFrame.data(),
                    static_cast<std::size_t>(
                        decodedSamples
                    )
                );

                remoteClient.frameNumber++;
                
                //this cout was very long..... bruhhh
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

                remoteClient.expectedSequenceNumber++;

                continue;
            }

            //expected packet is missing
            if(remoteClient.jitterBuffer.Size() >= 3){

                DEBUG_LOG(
                    "Packet Loss Detected : Client "<<senderClientId<<" : sequence "<<remoteClient.expectedSequenceNumber<<"\n"
                );

                //tell Opus that the packet was lost
                const int decodedSamples =
                    remoteClient.decoder.Decode(
                        nullptr,
                        0,
                        decodedFrame.data(),
                        static_cast<int>(
                            decodedFrame.size()
                        )
                    );

                if(decodedSamples < 0){
                    std::cerr
                        <<"Opus PLC Failed : "<<opus_strerror(
                            decodedSamples
                        )<<"\n";
                    remoteClient.expectedSequenceNumber++;
                    continue;
                }

                audioPlayback.Push(
                    decodedFrame.data(),
                    static_cast<std::size_t>(
                        decodedSamples
                    )
                );

                DEBUG_LOG(
                    "Generated PLC Audio : Client "<<senderClientId<<" : sequence "<<remoteClient.expectedSequenceNumber<<"\n"
                );

                remoteClient.expectedSequenceNumber++;

                continue;
            }

            break;
        }
    }
}

Client::~Client(){
    Stop();
}

void Client::ProcessControlEvents(){
    ControlEvent event;

    while(controlQueue.Pop(event)){
        if(event.type == ControlMessageType::ServerDisconnected){
            running = false;

            activeClients.clear();
            remoteClients.clear();

            std::cout
                <<"Server disconnected; voice session ended\n";

            return;
        }

        if(event.clientId == 0 || event.clientId == clientId){
            continue;
        }

        if(event.type == ControlMessageType::ClientConnected){
            const auto [iterator, inserted] =
                activeClients.emplace(
                    event.clientId,
                    event.clientName
                );

            if(inserted){
                std::cout
                    <<"Remote Client Added : "
                    <<event.clientName
                    <<" | ID : "
                    <<event.clientId
                    <<"\n";
            }
        }
        else if(
            event.type == ControlMessageType::ClientDisconnected
        ){
            const bool wasActive =
                activeClients.erase(event.clientId) > 0;

            remoteClients.erase(event.clientId);

            if(wasActive){
                std::cout
                    <<"Remote Client Disconnected : "
                    <<event.clientId
                    <<"\n";
            }
        }
    }
}

void Client::Stop(){
    running = false;

    // Stop capture callbacks while their queues still exist.
    audioCapture.Stop();
    audioPlayback.Stop();

    controlClient.Disconnect();

    if(receiveThread.joinable()){
        receiveThread.join();
    }

    // The UDP receiver has finished before socket closure.
    socket.Close();

    activeClients.clear();
    remoteClients.clear();
}
