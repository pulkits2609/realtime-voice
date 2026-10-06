#include <iostream>
#include "Network/client.hpp"

using boost::asio::ip::udp;

int main(
    int argc,
    char* argv[]
){
    if(argc != 4){
        std::cerr<<"Usage: voice_client <server_address> <server_port> <client_name> \n";

        return 1;
    }
    try{

        const std::string serverAddress = argv[1];

        const unsigned short serverPort = static_cast<unsigned short>(
            std::stoi(
                argv[2]
            )
        );

        const std::string clientName = argv[3];
        Client client(
            serverAddress,
            serverPort,
            clientName
        );

        client.Run();
    }
    catch(const std::exception &e){
        std::cerr<<"Client error : "<<e.what()<<"\n";

        return 1;
    }

    return 0;
}