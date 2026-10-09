#include "Network/client.hpp"
#include <iostream>
#include "Network/packet.hpp"
#include <thread>
#include <array>
#include "Common/debugLog.hpp"

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
controlClient(io_context){

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
    if(!controlClient.Connect(
        serverEndpoint.address().to_string(),
        serverEndpoint.port(),
        clientName
    )){
        std::cerr<<"Unable to Connect to voice server\n";

        return;
    }
    
    clientId = controlClient.GetClientId();

    std::cout<<"Connected to server as : "<<clientName<<" | Client ID: "<<clientId<<"\n";

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
        }
    );

    //initialize Opus
    if(!encoder.Initialize(
        48000,1,32000
    )){
        std::cerr<<"Failed to initialize Opus Encoder\n";

        return;
    }

    //open UDP Socket
    socket.Open();
    socket.Bind(0); //let operating system choose an available local UDP Port

    const bool audioInitialized = audioCapture.Initialize(
        [this](
            const float* samples,
            std::size_t sampleCount
        ){
            HandleCapture(
                samples,sampleCount
            );
        }
    ); //now the buffer belongs to the client

    //starting the audio device
    if(!audioInitialized){
        std::cerr<<"Failed to initialize microphone\n";
        
        return;
    }

    if(!audioCapture.Start()){
        std::cerr<<"Failed to start microphone\n";
        
        return;
    }
    //now Client Run has responsibility for starting microphone

    //initializing speaker playback
    if(!audioPlayback.Initialize()){
        std::cerr<<"Failed to initialize audioPlayback\n";

        return;
    }

    //starting speaker playback
    if(!audioPlayback.Start()){
        std::cerr<<"Failed to start audio Playback\n";

        return;
    }

    //now in a separate thread we start receiving our voice packets
    std::thread receiveThread(
        &Client::ReceiveVoice,
        this
    );

    receiveThread.detach();

    //processing buffers
    std::array<float,960> frame{};

    std::array<std::uint8_t,4000> encodedData{};

    std::uint32_t sequenceNumber = 0;

    while(true){

        //take captured audio from capture queue
        std::vector<float> capturedSamples;
        if(captureQueue.Pop(
            capturedSamples
        )){
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
                std::cerr<<"Opus Encode Failed :"<<opus_strerror(encodedBytes)<<"\n";
                continue;
            }

            //we create a Voice Packet using encoded Opus Data
            const std::vector<std::uint8_t> opusPayload(
                encodedData.begin(),
                encodedData.begin()+encodedBytes
            );

            Packet voicePacket(
                PacketType::Voice,
                opusPayload
            );

            voicePacket.SetClientId(
                clientId
            );

            voicePacket.SetSequenceNumber(
                ++sequenceNumber
            );

            //converting the voice packet to bytes
            const std::vector<std::uint8_t> voiceData = voicePacket.Serialize();

            //sending serialized voice packet to server
            socket.SendTo(
                voiceData,
                serverEndpoint
            );
        }

        ProcessControlEvents();
        ProcessReceivedVoice();
    }
}

//previously : MiniAudio callback -> HandleCapture -> PcmBuffer

//now : MiniAudio Callback -> HandleCapture -> CaptureQueue -> Client Processing -> PcmBuffer

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

//now this thread only has a single job
void Client::ReceiveVoice(){
    boost::asio::ip::udp::endpoint sender;

    while(true){
        std::vector<std::uint8_t> data = socket.ReceiveFrom(
            sender
        );

        if(data.empty()){
            continue;
        }

        //only accept packets coming from our server
        if(sender != serverEndpoint){
            continue;
        }
        networkQueue.Push(std::move(data));
    }
}

//old receive processing is now moved into client processing thread
void Client::ProcessReceivedVoice(){

    std::vector<std::uint8_t> data;

    while(networkQueue.Pop(
        data
    )){

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
        if(senderClientId == clientId){
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

        //packet is older than what we expect
        //so it is late or duplicate
        if(
            sequenceNumber <
            remoteClient.expectedSequenceNumber
        ){

            DEBUG_LOG(
                "Out of Order / Duplicate Packet : Client "<<senderClientId<<" : sequence "<<sequenceNumber<<"\n"
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
    audioPlayback.Stop();
    audioCapture.Stop();
}

void Client::ProcessControlEvents(){
    ControlEvent event;
    while(controlQueue.Pop(
        event
    )){
        if(
            event.type ==
            ControlMessageType::ClientConnected
        ){
            std::cout<<
                "Remote Client Added : "
                <<event.clientName
                <<" | ID : "
                <<event.clientId
                <<"\n";
        }
        else if(
            event.type ==
            ControlMessageType::ClientDisconnected
        ){
            if(
                remoteClients.erase(
                    event.clientId
                ) > 0
            ){
                std::cout<<"Remote Client Disconnected : "<<event.clientId<<"\n";
            }
        }
    }
}