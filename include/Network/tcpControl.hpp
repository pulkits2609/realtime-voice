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

class TcpControlClient{
    private:
        boost::asio::io_context& io_context;
        boost::asio::ip::tcp::socket socket;
        std::atomic<bool> running{false};
        std::thread receiveThread;
        ThreadSafeQueue<Json> outgoing{128};

    public:
        explicit TcpControlClient(boost::asio::io_context& io_context);
        ~TcpControlClient();
        Json Connect(const std::string& serverAddress, unsigned short port,
                     const Json& hello, const std::atomic<bool>& sessionRunning);
        void StartReceive(std::function<void(const Json&)> callback);
        void Post(const Json& request);
        void Disconnect();
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
