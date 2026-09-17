#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "Codec/opusDecoder.hpp"
#include "Codec/opusEncoder.hpp"

int main()
{
    constexpr int sampleRate = 48000;
    constexpr int channels = 1;
    constexpr int frameSize = 960;
    constexpr int bitrate = 32000;

    std::cout<<"Starting Opus codec test...\n";

    OpusEncoderWrapper encoder;

    if(!encoder.Initialize(
        sampleRate,
        channels,
        bitrate
    )){
        std::cerr<<"Failed to initialize Opus encoder.\n";
        return 1;
    }

    OpusDecoderWrapper decoder;

    if(!decoder.Initialize(
        sampleRate,
        channels
    )){
        std::cerr<<"Failed to initialize Opus decoder.\n";

        return 1;
    }

    std::array<float, frameSize> inputPcm{};
    constexpr float frequency = 440.0f;
    constexpr float amplitude = 0.25f;
    constexpr float pi = M_PI;

    for(int i=0; i<frameSize; i++){
        const float time =static_cast<float>(i) / static_cast<float>(sampleRate);

        inputPcm[i] = amplitude * std::sin(
                2.0f * pi * frequency * time
            );
    }

    std::array<std::uint8_t, 4000> encodedData{};

    const int encodedBytes = encoder.Encode(
            inputPcm.data(),
            frameSize,
            encodedData.data(),
            static_cast<int>(
                encodedData.size()
            )
        );

    if(encodedBytes < 0){
        std::cerr<<"Opus encode failed: "<< opus_strerror(
                encodedBytes
            )<<'\n';

        return 1;
    }

    std::cout<<"Encoded "<<frameSize<<" PCM samples into "<<encodedBytes<<" bytes.";

    std::array<float, frameSize> decodedPcm{};

    const int decodedSamples = decoder.Decode(
            encodedData.data(),
            encodedBytes,
            decodedPcm.data(),
            frameSize
        );

    if(decodedSamples < 0){
        std::cerr
            << "Opus decode failed: "
            << opus_strerror(
                decodedSamples
            )
            << '\n';

        std::cerr<<"Opus Decode Failed : "<<opus_strerror(
            decodedSamples
        )<<"\n";

        return 1;
    }

    std::cout<<"Decoded "<< encodedBytes<< " bytes into "<< decodedSamples<< " PCM samples.\n";

    double errorSum = 0.0;

    for(int i=0; i<decodedSamples; i++){
        const double difference =
            static_cast<double>(
                inputPcm[i]
            )
            -
            static_cast<double>(
                decodedPcm[i]
            );

        errorSum += difference * difference;
    }

    const double meanSquaredError =
        errorSum /
        static_cast<double>(
            decodedSamples
        );

    std::cout<<"Mean squared error: "<<meanSquaredError<<'\n';

    std::cout<<"First input sample: "<<inputPcm[0]<<'\n';

    std::cout<<"First decoded sample: "<<decodedPcm[0]<<'\n';

    std::cout<<"Opus codec test completed.\n";

    return 0;
}