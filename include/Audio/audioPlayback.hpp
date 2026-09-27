#pragma once

#include <cstddef>
#include <third_party/miniaudio.h>

#include <Audio/pcmBuffer.hpp>

class AudioPlayback{
    private:
        static void DataCallback(
            ma_device* device,
            void* output,
            const void* input,
            ma_uint32 framecount
        );

        ma_device device;
        PcmBuffer pcmBuffer;

        bool initialized = false;
        bool started = false; //same as audiocapture
        
    public:
        AudioPlayback();
        ~AudioPlayback();

        bool Initialize();
        bool Start();
        void Stop();

        void Push(
            const float* samples,
            std::size_t sampleCount
        );
};