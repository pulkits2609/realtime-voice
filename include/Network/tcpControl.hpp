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
#include "Network/protocol.hpp"

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
            std::shared_ptr<boost::asio::ip::tcp::socket> socket;
            // A slow reader is disconnected; reliable control is never silently dropped.
            ThreadSafeQueue<std::vector<std::uint8_t>> outgoing{128};
            std::atomic<bool> active{true}, closing{false};
            std::atomic<std::uint32_t> pendingPing{0};
        };

        boost::asio::io_context& io_context;
        boost::asio::ip::tcp::acceptor acceptor;
        std::map<std::uint32_t, std::shared_ptr<ClientConnection>> clients;
        std::mutex clientsMutex;
        std::thread acceptThread;
        std::vector<std::future<void>> handlers;
        std::atomic<bool> running{false};
        std::uint32_t nextClientId = 1;
        std::function<Json(std::uint32_t, const Json&)> opened, requested;
        std::function<void(std::uint32_t)> removed;

        void AcceptLoop();
        void HandleClient(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
        void RemoveClient(const std::shared_ptr<ClientConnection>& client);

    public:
        TcpControlServer(boost::asio::io_context& io_context, unsigned short port);
        ~TcpControlServer();
        void Start();
        void Stop();
        void SetHandlers(std::function<Json(std::uint32_t, const Json&)> open,
                         std::function<Json(std::uint32_t, const Json&)> request,
                         std::function<void(std::uint32_t)> remove);
        void Send(std::uint32_t id, const Json& message);
        void Close(std::uint32_t id);
};
