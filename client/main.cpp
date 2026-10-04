#include <iostream>
#include "Network/client.hpp"

using boost::asio::ip::udp;

int main()
{
    try{
        Client client(
            "192.168.1.103",
            8080
        );

        client.Run();
    }
    catch(const std::exception &e){
        std::cerr<<"Client error : "<<e.what()<<"\n";

        return 1;
    }

    return 0;
}