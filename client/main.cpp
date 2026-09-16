#include <iostream>
#include "Network/client.hpp"
#include "Audio/audioCapture.hpp"

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
    const bool initialized = capture.Initialize(
        [](
            const float* samples,
            std::size_t sampleCount
        ){
            std::cout<<"Received : "<<sampleCount<<" samples\n";
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
    std::cin.get();

    return 0;


    return 0;
}