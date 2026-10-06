#pragma once

#include <boost/asio.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

enum class ControlMessageType : std::uint8_t{
    Join = 1,
    JoinAccepted = 2,
    ClientConnected = 3,
    ClientDisconnected = 4
};

class TcpControlClient{
    private:
        boost::asio::ip::tcp::socket socket;
        std::uint32_t clientId = 0;

        void SendFrame(
            const std::vector<std::uint8_t>& payload
        );

        std::vector<std::uint8_t> ReceiveFrame();

    public:
        explicit TcpControlClient(
            boost::asio::io_context& io_context
        );

        bool Connect(
            const std::string& serverAddress,
            unsigned short serverPort,
            const std::string& clientName
        );

        void StartReceive(
            std::function<void(
                ControlMessageType,
                std::uint32_t,
                const std::string&
            )> callback
        );

        std::uint32_t GetClientId() const;
};

class TcpControlServer{
    private:    
        struct ClientConnection{
            std::uint32_t clientId;
            std::string clientName;

            std::shared_ptr<
                boost::asio::ip::tcp::socket
            > socket;
        };

        boost::asio::io_context& io_context;

        boost::asio::ip::tcp::acceptor acceptor;

        std::map<
            std::uint32_t,
            ClientConnection
        > clients;

        std::mutex clientsMutex;

        std::thread acceptThread;

        bool running = false;

        std::uint32_t nextClientId = 1;

        std::vector<std::uint8_t> ReceiveFrame(
            boost::asio::ip::tcp::socket& socket
        );

        void SendFrame(
            boost::asio::ip::tcp::socket& socket,
            const std::vector<std::uint8_t>& payload
        );

        void AcceptLoop();

        void HandleClient(
            std::shared_ptr<
                boost::asio::ip::tcp::socket
            > socket
        );

        void Broadcast(
            const std::vector<std::uint8_t>& payload,
            std::uint32_t excludedClientId = 0
        );

    public:
        TcpControlServer(
            boost::asio::io_context& io_context,
            unsigned short port
        );

        void Start();
};