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

class Client{
    private:
        boost::asio::io_context io_context;

        UdpSocket socket;

        boost::asio::ip::udp::endpoint serverEndpoint;

        AudioCapture audioCapture;

        //audio data waiting to process
        ThreadSafeQueue<std::vector<float>> captureQueue;

        PcmBuffer pcmBuffer;
        
        OpusEncoderWrapper encoder;

        AudioPlayback audioPlayback;

        //network packets waiting to process
        ThreadSafeQueue<std::vector<std::uint8_t>> networkQueue;

        std::string clientName;
        std::uint32_t clientId = 0;

        TcpControlClient controlClient;


        //instead of a global state for all clients
        //now each client will have their separate states
        struct RemoteClientState{
            JitterBuffer jitterBuffer;
            OpusDecoderWrapper decoder;
            std::uint32_t expectedSequenceNumber = 0;
            bool receivedFirstPacket = false;
            bool jitterBufferStarted = false;
            int frameNumber = 0;

            RemoteClientState():
                jitterBuffer(3),
                decoder(){

            }
        };

        struct ControlEvent{
            ControlMessageType type;
            std::uint32_t clientId;
            std::string clientName;
        };

        std::map<std::uint32_t,
            RemoteClientState
            > remoteClients;
        
        ThreadSafeQueue<ControlEvent> controlQueue;
        
        void SendMessage(
            const std::string& message
        );
        std::string ReceiveMessage();

        void HandleCapture(
            const float* samples,
            std::size_t sampleCount
        );

        void ProcessReceivedVoice();

        void ProcessControlEvents();
    
    public:
        Client(
            const std::string& serverAddress,
            unsigned short serverPort,
            const std::string& clientName
        );
        ~Client();

        void Run();

        void ReceiveVoice();
};