#include <iostream>
#include "Network/client.hpp"
#include "Audio/audioCapture.hpp"
#include "Audio/pcmBuffer.hpp"

//temp
#include<chrono>
#include<thread>
#include<array>

using boost::asio::ip::udp;

int main()
{
    // std::cout << "Voice Client started!\n";

    // try{
    //     Client client{
    //         "127.0.0.1",
    //         8000
    //     };

    //     client.Run();
    // }
    // catch(const std::exception& e){
    //     std::cerr<<"Client error : "<<
    //     e.what()<<"\n";

    //     return 1;
    // }

    //we temporarily test the audio capture in main directly

    AudioCapture capture;

    PcmBuffer buffer(
        48000
    );

    const bool initialized = capture.Initialize(
        [&buffer](
            const float* samples,
            std::size_t sampleCount
        ){
            // std::cout<<"Received : "<<sampleCount<<" samples\n";
            buffer.Push(
                samples,sampleCount
            );
        }
    );
    
    if(!initialized){
        std::cerr<<"Failed to initialize microphone\n";

        return 1;
    }
    if(!capture.Start()){
        std::cerr<<"Failed to start microphone\n";

        return 1;
    }

    std::cout<<"Microphone Running. Press Enter to Stop\n";
    // std::cin.get();

    std::array<float,960> frame{};
    while(true){
        if(buffer.PopExact(
            frame.data(),frame.size()
        )){
            std::cout<<"Got a complete 960-sample frame!\n";
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10)
        );
    }

    return 0;
}