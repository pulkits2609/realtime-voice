#include "Network/client.hpp"
#include <iostream>
#include "Network/packet.hpp"
#include <thread>

Client::Client(
    const std::string& serverAddress,
    unsigned short serverPort
):
socket(io_context),
serverEndpoint(
    boost::asio::ip::make_address(serverAddress),
    serverPort
),audioCapture(),
pcmBuffer(48000),
encoder(),decoder(){

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

    return packet.GetMessage();
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

    //processing loop
    std::array<float,960> frame{};

    std::array<std::uint8_t,4000> encodedData{};
    std::array<float,960> decodedFrame{};

    int frameNumber = 0;

    while(true){
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

            //converting the voice packet to bytes
            const std::vector<std::uint8_t> voiceData = voicePacket.Serialize();

            //sending serialized voice packet to server
            socket.SendTo(
                voiceData,
                serverEndpoint
            );

            //immediately decode what we encoded
            // const int decodedSamples = decoder.Decode(
            //     encodedData.data(),
            //     encodedBytes,
            //     decodedFrame.data(),
            //     static_cast<int>(decodedFrame.size())
            // );

            // if(decodedSamples < 0){
            //     std::cerr<<"Opus Decode failed : "<<opus_strerror(decodedSamples)<<"\n";

            //     continue;
            // }

            // frameNumber++;

            // std::cout<<"Encoded frame #"<<frameNumber<<" "<<encodedBytes<<" bytes,"
            // <<" decoded "<<decodedSamples<<" samples\n";

            //for now we arent decoding back the packet (already tested it)


        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1)
        );
    }
}

void Client::HandleCapture(
    const float* samples,
    std::size_t sampleCount
){
    pcmBuffer.Push(
        samples,sampleCount
    );
}

void Client::ReceiveVoice(){
    boost::asio::ip::udp::endpoint sender;

    std::array<float,960> decodedFrame{};

    int frameNumber = 0;

    while(true){
        //waiting for the next UDP packet
        const std::vector<std::uint8_t> data = socket.ReceiveFrom(
            sender
        );

        if(data.empty()){
            continue;
        }

        //only accept packets coming from our server
        if(sender != serverEndpoint){
            continue;
        }
        
        Packet packet;
        if(!packet.Deserialize(data)){
            std::cerr<<"Received Invalid Voice packet\n";

            continue;
        }

        //ignore other type of packets
        if(packet.GetType() != PacketType::Voice){
            continue;
        }

        const std::vector<std::uint8_t>& payload = packet.GetPayload();

        if(payload.empty()){
            continue;
        }

        //decoding opus packet back into PCM
        const int decodedSamples = decoder.Decode(
            payload.data(),
            static_cast<int>(payload.size()),
            decodedFrame.data(),
            static_cast<int>(decodedFrame.size())
        );

        if(decodedSamples < 0){
            std::cerr<<"Opus Decoding Failed : "<<opus_strerror(decodedSamples)<<"\n";

            continue;
        }

        //now sending decoded PCM Samples to speaker buffer
        audioPlayback.Push(
            decodedFrame.data(),
            static_cast<std::size_t>(decodedSamples)
        );

        frameNumber++;

        std::cout<<"Received Voice Packet : "<<frameNumber<<" : "<<payload.size()<<"bytes, decoded "<<decodedSamples<<" samples\n";
    }
}

Client::~Client(){
    audioPlayback.Stop();
    audioCapture.Stop();
}
