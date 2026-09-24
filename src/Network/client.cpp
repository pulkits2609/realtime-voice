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
encoder(){

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

    //processing loop
    std::array<float,960> frame{};

    std::array<std::uint8_t,4000> encodedData{};

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

            frameNumber++;

            std::cout<<"Encoded frame #"<<frameNumber<<" "<<encodedBytes<<" bytes\n";
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1)
        );
    }

    // socket.Open();
   
    // std::cout<<"Client Connected\n";
    // std::cout<<"Sending Message to server\n";
    // SendMessage(
    //     "Hello From Client"
    // );
    // std::cout<<"Waiting for server response : \n";
    // const std::string response = ReceiveMessage();

    // std::cout<<"Server Response : \n"<<response<<"\n";

    //for now no need of socket transfer
}

void Client::HandleCapture(
    const float* samples,
    std::size_t sampleCount
){
    pcmBuffer.Push(
        samples,sampleCount
    );
}

Client::~Client(){
    audioCapture.Stop();
}
