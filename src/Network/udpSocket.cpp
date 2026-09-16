#include "Network/udpSocket.hpp"

#include<array>
#include<string>

UdpSocket::UdpSocket(
    boost::asio::io_context& io_context
):
socket(io_context){

}

void UdpSocket::Open(){
    socket.open(
        boost::asio::ip::udp::v4()
    );
}

void UdpSocket::Bind(
    unsigned short port
){
    socket.bind(
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(),
            port
        )
    );
}

void UdpSocket::SendTo(
    // const std::string& message,
    const std::vector<std::uint8_t>& data,
    const boost::asio::ip::udp::endpoint& endpoint
){
    socket.send_to(
        boost::asio::buffer(data),
        endpoint
    );
}

std::vector<std::uint8_t> UdpSocket::ReceiveFrom(
    boost::asio::ip::udp::endpoint& sender
){
    std::array<char,1024> buffer{};

    const std::size_t bytesReceived = socket.receive_from(
        boost::asio::buffer(buffer),
        sender
    );

    // return std::string(buffer.data(), bytesReceived);
    return std::vector<std::uint8_t>(
        buffer.begin(),
        buffer.end()+bytesReceived
    );
}