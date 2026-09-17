#pragma once

#include <cstddef>
#include <cstdint>

#include <opus/opus.h>

class OpusEncoderWrapper{
    private:
        OpusEncoderWrapper(
            const OpusEncoderWrapper&
        ) = delete;

        OpusEncoderWrapper& operator=(
            const OpusEncoderWrapper&
        ) = delete;

        ::OpusEncoder* encoder = nullptr;

        public:
            OpusEncoderWrapper();
            ~OpusEncoderWrapper();

            bool Initialize(
                int sampleRate,
                int channels,
                int bitrate
            );

            int Encode(
                const float* pcm,
                int frameSize,
                std::uint8_t* output,
                int maxOutputBytes
            );
};