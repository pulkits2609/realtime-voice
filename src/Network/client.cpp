#include "Network/client.hpp"
#include <iostream>
#include "Network/packet.hpp"
#include <thread>
#include <array>
#include <chrono>
#include "Common/debugLog.hpp"

Client::Client(
    const std::string& serverAddress,
    unsigned short serverPort
):
socket(io_context),
serverEndpoint(
    boost::asio::ip::make_address(serverAddress),
    serverPort
),
audioCapture(),
captureQueue(),
pcmBuffer(48000),
encoder(),
decoder(),
audioPlayback(),
jitterBuffer(3), //buffer is kept very small deliberately
networkQueue(){

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
    //initialize Opus
    if(!encoder.Initialize(
        48000,1,32000
    )){
        std::cerr<<"Failed to initialize Opus Encoder\n";

        return;
    }
    if(!decoder.Initialize(48000,1)){
        std::cerr<<"Failed to initialize Opus Decoder\n";

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

        ProcessReceivedVoice();

        //prevent this processing loop from continously consuming CPU
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1)
        );
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

    while(networkQueue.Pop(data)){

        Packet packet;

        if(!packet.Deserialize(data)){
            std::cerr<<"Received Invalid Voice packet\n";

            continue;
        }

        //ignore other type of packets
        if(packet.GetType() != PacketType::Voice){
            continue;
        }

        const std::uint32_t sequenceNumber = packet.GetSequenceNumber();

        const std::vector<std::uint8_t>& payload = packet.GetPayload();

        if(payload.empty()){
            continue;
        }

        //the first packet tells where the sequence starts
        if(!receivedFirstPacket){

            expectedSequenceNumber =
                sequenceNumber;

            receivedFirstPacket = true;
        }

        //if packet is older than what we already expect
        //it is either late or duplicate
        if(
            sequenceNumber <
            expectedSequenceNumber
        ){

            DEBUG_LOG(
                <<"Out of Order / Duplicate Packet : "
                <<sequenceNumber
                <<"\n");

            continue;
        }

        //store packet inside jitter buffer
        if(!jitterBuffer.Push(
            sequenceNumber,
            payload
        )){

            DEBUG_LOG(
                <<"Duplicate / Full Jitter Buffer : "
                <<sequenceNumber
                <<"\n");

            continue;
        }

        //wait until jitter buffer has a few packets
        //before starting playback
        if(
            !jitterBufferStarted &&
            jitterBuffer.Size() >= 3
        ){

            jitterBufferStarted = true;

            DEBUG_LOG("Jitter Buffer Started\n");
        }

        if(!jitterBufferStarted){
            continue;
        }

        std::array<float,960> decodedFrame{};

        while(true){

            std::vector<std::uint8_t> nextPayload;

            //try to get exactly the packet we are expecting
            if(jitterBuffer.Pop(
                expectedSequenceNumber,
                nextPayload
            )){

                const int decodedSamples =
                    decoder.Decode(
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

                    expectedSequenceNumber++;

                    continue;
                }

                audioPlayback.Push(
                    decodedFrame.data(),
                    static_cast<std::size_t>(
                        decodedSamples
                    )
                );

                frameNumber++;

                DEBUG_LOG(
                    <<"Received Voice Packet : "
                    <<frameNumber
                    <<" : sequence "
                    <<expectedSequenceNumber
                    <<" : "
                    <<nextPayload.size()
                    <<"bytes, decoded "
                    <<decodedSamples
                    <<" samples\n");

                expectedSequenceNumber++;

                continue;
            }

            //expected packet missing
            //if we already have 3 packets waiting,
            //assume the packet was lost
            if(jitterBuffer.Size() >= 3){

                DEBUG_LOG(
                    <<"Packet Loss Detected : sequence "
                    <<expectedSequenceNumber
                    <<"\n");

                //tell Opus that packet was lost
                const int decodedSamples =
                    decoder.Decode(
                        nullptr,
                        0,
                        decodedFrame.data(),
                        static_cast<int>(
                            decodedFrame.size()
                        )
                    );

                if(decodedSamples < 0){

                    std::cerr
                        <<"Opus PLC Failed : "
                        <<opus_strerror(
                            decodedSamples
                        )
                        <<"\n";

                    expectedSequenceNumber++;

                    continue;
                }

                //send generated replacement PCM
                //to the speaker buffer
                audioPlayback.Push(
                    decodedFrame.data(),
                    static_cast<std::size_t>(
                        decodedSamples
                    )
                );

                DEBUG_LOG(
                    <<"Generated PLC Audio for sequence "
                    <<expectedSequenceNumber
                    <<"\n");

                expectedSequenceNumber++;

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
