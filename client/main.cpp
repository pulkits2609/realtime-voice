#include <iostream>
#include "Network/client.hpp"

using boost::asio::ip::udp;

int main()
{
    std::cout << "Voice Client started!\n";

    try{
        Client client{
            "127.0.0.1",
            8000
        };

        client.Run();
    }
    catch(const std::exception& e){
        std::cerr<<"Client error : "<<
        e.what()<<"\n";

        return 1;
    }


    return 0;
}