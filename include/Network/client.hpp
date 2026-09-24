#pragma once

#include<boost/asio.hpp>
#include<string>

#include "Network/udpSocket.hpp"
//moving audio pipeline to the client
#include "Audio/audioCapture.hpp"
#include "Audio/pcmBuffer.hpp"
#include "Codec/opusEncoder.hpp"
#include "Codec/opusDecoder.hpp"

class Client{
    private:
        boost::asio::io_context io_context;

        UdpSocket socket;

        boost::asio::ip::udp::endpoint serverEndpoint;

        AudioCapture audioCapture;

        PcmBuffer pcmBuffer;
        
        OpusEncoderWrapper encoder;
        OpusDecoderWrapper decoder;

        void SendMessage(
            const std::string& message
        );
        std::string ReceiveMessage();

        void HandleCapture(
            const float* samples,
            std::size_t sampleCount
        );
    
    public:
        Client(
            const std::string& serverAddress,
            unsigned short serverPort
        );
        ~Client();

        void Run();
};