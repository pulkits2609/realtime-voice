#pragma once

#include <boost/asio.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Network/threadSafeQueue.hpp"

enum class ControlMessageType : std::uint8_t{
    Join = 1,
    JoinAccepted = 2,
    ClientConnected = 3,
    ClientDisconnected = 4,

    Ping = 5,
    Pong = 6,

    // Local notification; never sent over TCP.
    ServerDisconnected = 7
};

class TcpControlClient{
    private:
        boost::asio::io_context& io_context;
        boost::asio::ip::tcp::socket socket;

        std::uint32_t clientId = 0;

        std::atomic<bool> running{false};
        std::thread receiveThread;

        void SendFrame(
            const std::vector<std::uint8_t>& payload
        );

        std::vector<std::uint8_t> ReceiveFrame();

    public:
        explicit TcpControlClient(
            boost::asio::io_context& io_context
        );

        ~TcpControlClient();

        bool Connect(
            const std::string& serverAddress,
            unsigned short serverPort,
            const std::string& clientName,
            const std::atomic<bool>& sessionRunning
        );

        void StartReceive(
            std::function<void(
                ControlMessageType,
                std::uint32_t,
                const std::string&
            )> callback
        );

        void Disconnect();

        std::uint32_t GetClientId() const;
        boost::asio::ip::address GetServerAddress() const;
};

class TcpControlServer{
    private:
        struct ClientConnection{
            std::uint32_t clientId = 0;
            std::string clientName;

            std::shared_ptr<
                boost::asio::ip::tcp::socket
            > socket;

            // Only this connection's writer sends TCP frames.
            ThreadSafeQueue<
                std::vector<std::uint8_t>
            > outgoing{0};

            std::atomic<bool> active{true};
        };

        boost::asio::io_context& io_context;
        boost::asio::ip::tcp::acceptor acceptor;

        std::map<
            std::uint32_t,
            std::shared_ptr<ClientConnection>
        > clients;

        std::mutex clientsMutex;

        std::thread acceptThread;
        std::vector<std::future<void>> handlers;

        std::atomic<bool> running{false};
        std::uint32_t nextClientId = 1;

        std::function<void(
            std::uint32_t,
            const std::string&
        )> clientConnectedCallback;

        std::function<void(
            std::uint32_t
        )> clientDisconnectedCallback;

        void AcceptLoop();

        void HandleClient(
            std::shared_ptr<
                boost::asio::ip::tcp::socket
            > socket
        );

        void RemoveClient(
            const std::shared_ptr<ClientConnection>& client
        );

    public:
        using ClientConnectedCallback =
            std::function<void(
                std::uint32_t,
                const std::string&
            )>;

        using ClientDisconnectedCallback =
            std::function<void(
                std::uint32_t
            )>;

        TcpControlServer(
            boost::asio::io_context& io_context,
            unsigned short port
        );

        ~TcpControlServer();

        void Start();
        void Stop();

        void SetClientConnectedCallback(
            ClientConnectedCallback callback
        );

        void SetClientDisconnectedCallback(
            ClientDisconnectedCallback callback
        );
};