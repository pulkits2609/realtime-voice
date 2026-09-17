#pragma once

#include <cstddef>
#include <cstdint>

#include <opus/opus.h>

class OpusDecoderWrapper{
    private:
        OpusDecoderWrapper(
            const OpusDecoderWrapper&
        ) = delete;

        OpusDecoderWrapper& operator = (
            const OpusDecoderWrapper&
        ) = delete;

        ::OpusDecoder* decoder = nullptr;

    public:
        OpusDecoderWrapper();
        ~OpusDecoderWrapper();

        bool Initialize(
            int sampleRate,
            int channels
        );

        int Decode(
            const std::uint8_t* packet,
            int packetBytes,
            float* output,
            int maxFrameSize
        );

};