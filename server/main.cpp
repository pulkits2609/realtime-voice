#include <iostream>
#include <string>
#include <boost/asio.hpp>

using boost::asio::ip::udp;

int main()
{
    std::cout << "Voice Server started!\n";
    //starting the server's main loop
    try{
        boost::asio::io_context io_context; //this is like the engine that does all the boosts async operations

        udp::socket socket(io_context, udp::endpoint(udp::v4(),8000));
        //this creates a UDP Socket immediately and binds it to IPV4 port 8000

        std::cout<<"Voice Server started on port 8000";

        //receiving a packet
        char buffer[1024];

        udp::endpoint sender_endpoint;

        std::size_t bytes_received = socket.receive_from(
            boost::asio::buffer(buffer),sender_endpoint
        );

        std::string message(buffer, bytes_received);

        std::cout << "\nReceived : "<<message<<"\n";
    }
    catch(const std::exception& e){
        std::cerr<<"Server Error : "<<e.what()<<"\n";
    }


    return 0;
}