#pragma once

#include<boost/asio.hpp>

#include <string>
#include <vector>
#include <cstdint>
#include <map>

#include "Network/udpSocket.hpp"
#include "Network/threadSafeQueue.hpp"

//audio pipeline
#include "Audio/audioCapture.hpp"
#include "Audio/pcmBuffer.hpp"

#include "Codec/opusDecoder.hpp"
#include "Codec/opusEncoder.hpp"

#include "Audio/audioPlayback.hpp"
#include "Network/jitterBuffer.hpp"

#include <thread>

#include "Network/tcpControl.hpp"
#include "Network/routedPacket.hpp"
#include <set>

#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>

struct MuteState{
    std::atomic<bool> microphone{true}, all{true}, team{true};
    std::atomic<std::uint64_t> captureGeneration{1};

    Json Snapshot() const{
        return {{"microphone", microphone.load()}, {"all", all.load()}, {"team", team.load()}};
    }
    void Set(const Json& value){
        const bool mic = Boolean(value, "microphone");
        const bool incomingAll = Boolean(value, "all");
        const bool incomingTeam = Boolean(value, "team");
        if(mic != microphone.load()) captureGeneration++;
        microphone = mic; all = incomingAll; team = incomingTeam;
    }
};

class Client{
    private:
        using Clock = std::chrono::steady_clock;

        static constexpr std::size_t FrameSamples = 960;
        static constexpr auto FrameDuration = std::chrono::milliseconds(20);
        static constexpr auto JitterDelay = std::chrono::milliseconds(60);
        static constexpr std::uint32_t JitterCapacity = 8;
        static constexpr unsigned int MaxConsecutiveLosses = 5;

        boost::asio::io_context io_context;

        UdpSocket socket;

        boost::asio::ip::udp::endpoint serverEndpoint;

        AudioCapture audioCapture;


        Json joinInfo, session = Json::object();
        std::shared_ptr<MuteState> mutes;
        std::function<void(const Json&)> emit;
        bool audioEnabled = true, audioStarted = false;
        std::atomic<bool> finished{false};
        std::map<std::uint32_t, std::uint32_t> peerRevisions, peerTeams;
        std::uint64_t serverInstance = 0;
        std::uint32_t groupId = 0, allChannel = 0, teamChannel = 0;
        std::uint32_t sendRevision = 0, receiveRevision = 0;
        std::set<std::string> pendingRequests;
        std::map<std::string, unsigned> muteRequests;
        std::atomic<unsigned> pendingMicMutes{0}, pendingIncomingMutes{0};
        struct ClientCommand{
            Json request;
            unsigned muteChanges = 0; // bit 1: microphone; bit 2: incoming routes
        };
        ThreadSafeQueue<ClientCommand> commands{128};
        mutable std::mutex statusMutex;
        Json published = {{"state", "connecting"}};
        std::atomic<std::uint64_t> decodedFrames{0}, mixedFrames{0};

        struct CapturedBlock{
            std::uint64_t generation;
            std::vector<float> samples;
        };
        void Publish();
        void ClearAudio();
        void ClearIncoming();
        void StartAudio();
        void ProcessCommands();

        //audio data waiting to process
        ThreadSafeQueue<CapturedBlock> captureQueue;

        PcmBuffer pcmBuffer;
        
        OpusEncoderWrapper encoder;

        AudioPlayback audioPlayback;

        //network packets waiting to process
        ThreadSafeQueue<std::vector<std::uint8_t>> networkQueue;

        std::string serverAddress;
        std::string clientName;
        std::uint32_t clientId = 0;

        TcpControlClient controlClient;

        std::atomic<bool> running{true};
        std::thread receiveThread;

        std::mutex workMutex;
        std::condition_variable workCondition;
        bool workPending = false;

        // TCP membership is authoritative.
        std::map<std::uint32_t, std::string> activeClients;


        //instead of a global state for all clients
        //now each client will have their separate states
        struct RemoteClientState{
            JitterBuffer jitterBuffer;
            OpusDecoderWrapper decoder;
            std::uint32_t expectedSequenceNumber = 0;
            bool receivedFirstPacket = false;
            bool jitterBufferStarted = false;
            int frameNumber = 0;
            unsigned int consecutiveLosses = 0;
            Clock::time_point playbackStart = Clock::time_point::max();

            RemoteClientState():
                jitterBuffer(JitterCapacity),
                decoder(){

            }
        };

        std::map<std::uint64_t,
            RemoteClientState
            > remoteClients;
        
        ThreadSafeQueue<Json> controlQueue;
        
        void HandleCapture(
            const float* samples,
            std::size_t sampleCount
        );

        void ProcessReceivedVoice();

        void RebufferRemoteClient(
            RemoteClientState& remoteClient,
            Clock::time_point now
        );

        void ProcessPlayback(
            Clock::time_point now,
            std::uint64_t skippedFrames
        );

        void NotifyWork();

        void ProcessControlEvents();

        void Stop();
    
    public:
        Client(
            const std::string& serverAddress,
            unsigned short serverPort,
            const std::string& clientName,
            Json joinInfo, std::shared_ptr<MuteState> mutes,
            std::function<void(const Json&)> emit, bool audioEnabled = true
        );
        ~Client();

        void Run();
        void RequestStop();
        void Post(const Json& request);
        Json GetStatus() const;
        bool Done() const { return finished.load(); }
        friend struct ClientAudioTest;

        void ReceiveVoice();
};
