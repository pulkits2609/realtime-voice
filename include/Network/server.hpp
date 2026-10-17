#pragma once

#include <boost/asio.hpp>
#include <atomic>
#include <thread>
#include "Network/udpSocket.hpp"
#include "Network/tcpControl.hpp"
#include "Network/voiceGroups.hpp"

class Server{
    private:
        boost::asio::io_context io_context;
        UdpSocket socket;
        unsigned short port;
        std::atomic<bool> running{false};
        VoiceGroups groups;
        TcpControlServer controlServer;
        void HandleMessage();
        void ConsoleLoop();

    public:
        explicit Server(unsigned short port);
        void Run();
};
