#include <iostream>
#include <stdexcept>
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

        std::size_t parsedCharacters = 0;
        const int portValue = std::stoi(argv[2], &parsedCharacters);

        if(parsedCharacters != std::string(argv[2]).size() || portValue < 1 || portValue > 65535){
            throw std::invalid_argument(
                "Port must be a number between 1 and 65535"
            );
        }

        const unsigned short serverPort = static_cast<unsigned short>(portValue);

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
