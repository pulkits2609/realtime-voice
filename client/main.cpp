#include <iostream>
#include <string>
#include <boost/asio.hpp>

using boost::asio::ip::udp;

int main()
{
    std::cout << "Voice Client started!\n";

    //building the UDP CLient that directly sends message to the server
    //because its UDP , we just have to build the message and send it directly
    //no connect or handshaking process
    try{
        boost::asio::io_context io_context;
        udp::socket socket(io_context, udp::v4());

        udp::endpoint server_endpoint(
            boost::asio::ip::make_address("127.0.0.1"),8000
        );

        std::string message = "Hello Server";

        socket.send_to(
            boost::asio::buffer(message),
            server_endpoint
        );

        std::cout<<"Message Sent\n";
    }
    catch(const std::exception& e){
        std::cerr<<"Client Error : "<<e.what()<<"\n";
    }
    return 0;
}