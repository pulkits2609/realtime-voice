#pragma once

#include<cstddef>
#include<functional>
#include<third_party/miniaudio.h>

class AudioCapture{
    public:
        using Callback = std::function<void(
            const float* samples,
            std::size_t sampleCount
        )>;
    
    private:
        static void DataCallback(
            ma_device* device,
            void* output,
            const void* input,
            ma_uint32 frameCOunt
        );

        
        ma_device device;
        Callback callback;

        bool initialized = false;
        bool started_ = false; //I dont know why but started is some sort of keyword being used somewhere

    public:
        AudioCapture();
        ~AudioCapture();

        bool Initialize(Callback callback);

        bool Start();
        void Stop();
};

//a sample is basically one measurement of the audio signal at one instant in time

//AudioCapture doesnt care what happens to the samples, it will simply give it to whoever is interested!


//the callback means : 
// std::function<void(
//     const float* samples,
//     std::size_t sampleCount
// )>;
//whenever some microphone audio is available, give me a pointer to the captured audio smaples and tell me how many samples are there

//Samples Representation :
//thinking of sound as a waveform : 
// Amplitude
//    ^
// 1  |       /\          /\
//    |      /  \        /  \
// 0  |-----/----\------/----\------> time
//    |    /      \    /
// -1 |   /        \__/

// A computer can't store an infinitely continuous waveform. Instead, it measures the waveform repeatedly:


// Continuous waveform:

//        /\
//       /  \       /
// -----/----\-----/---->
//     /      \   /

// Samples:

//        ●
//       / \
//      ●   ●       ●
// ----●-----●-----●---->
//    ●       ●   ●
//              ●


//for eg if we are capturing at 48000Hz
//the mic / audio system produces 48000 samples per second per channel for mono audio

//samples are conventionally represented in range of float : -1.0f to +1.0f