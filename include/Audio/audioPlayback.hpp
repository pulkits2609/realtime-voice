#pragma once

#include <cstddef>
#include <atomic>
#include <third_party/miniaudio.h>

#include <Audio/pcmBuffer.hpp>

class AudioPlayback{
    friend struct ClientAudioTest;
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
        std::atomic<bool> enabled{true};
        bool started = false; //same as audiocapture
        
    public:
        AudioPlayback();
        ~AudioPlayback();

        bool Initialize();
        bool Start();
        void Stop();
        void Clear(){ pcmBuffer.Clear(); }
        void SetEnabled(bool value){ enabled = value; if(!value) Clear(); }

        void Push(
            const float* samples,
            std::size_t sampleCount
        );
};
