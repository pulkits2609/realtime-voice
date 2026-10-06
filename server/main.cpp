#include <iostream>
#include "Network/server.hpp"

using boost::asio::ip::udp;

int main(
    int argc,
    char* argv[]
)
{
    if(argc != 2){
        std::cerr<<"Usage: <voice_server> <port>\n";

        return 1;
    }
    std::cout << "Voice Server started!\n";
    
    try{

        const unsigned short port = static_cast<unsigned short>(
            std::stoi(
                argv[1]
            )
        );
        Server server(
            port
        );

        server.Run();
    }
    catch(const std::exception& e){
        std::cerr<<"Server Error : "<<
        e.what()<<"\n";

        return 1;
    }

    return 0;
}