#include <iostream>
#include <stdexcept>
#include "Network/server.hpp"
#include "Common/consoleInput.hpp"

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
    std::cout << "Starting Voice Server...\n";
    
    try{

        std::size_t parsedCharacters = 0;
        const int portValue = std::stoi(argv[1], &parsedCharacters);

        if(parsedCharacters != std::string(argv[1]).size() || portValue < 1 || portValue > 65535){
            throw std::invalid_argument(
                "Port must be a number between 1 and 65535"
            );
        }

        const unsigned short port = static_cast<unsigned short>(portValue);
        Server server(
            port
        );

        server.Run();
    }
    catch(const boost::system::system_error& e){
        std::cerr << Event("server_error", {
            {"code", e.code() == boost::asio::error::address_in_use ? "address_in_use" : "socket_error"},
            {"native_code", e.code().value()}, {"message", e.what()}})
            .dump(ConsoleInput::IsTerminal(2) ? 2 : -1) << std::endl;
        return 1;
    }
    catch(const std::exception& e){
        std::cerr<<"Server Error : "<<
        e.what()<<"\n";

        return 1;
    }

    return 0;
}
