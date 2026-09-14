#include <iostream>
#include "Network/server.hpp"

using boost::asio::ip::udp;

int main()
{
    std::cout << "Voice Server started!\n";
    
    try{
        Server server(
            8000
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