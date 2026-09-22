#pragma once

#include <cstddef>
#include <deque>
#include <mutex>

//this class will be used to buffer the audio samples
//current problem lies in PCM Samples :
//Our Callback gives us audio in small chunks
//1200 samples chunks....

//eventually the Opus Encoder would be requiring a specific number of samples per frame

//for our config of 48000Hz, and 20ms frame
//48000 x 0.020 = 960 samples

//so we will be transforming 1200 samples into 960
//the remaining 240 samples will be retained in next frame
//that is why we use deque for this !
//adding samples from end, removing from first !

class PcmBuffer{
    private:
        std::deque<float> samples;
        std::size_t maxSamples;

    public:
        //this will impose a buffer limit on the PcmBuffer, so that it doesnt keep using memory infinitely (memory Nuke)
        explicit PcmBuffer( 
            std::size_t maxSamples
        );

        //gives pointer to the actual samples, and count of them
        void Push(
            const float* samples,
            std::size_t count
        );

        //this says that give me exactly this many samples, but if the buffer doesnt have them, return false and keep buffer untouched
        bool PopExact(
            float* output,
            std::size_t count
        );
        
        //return how many samples are currently buffered, useful in debugging
        std::size_t Size() const;

        mutable std::mutex mutex_;
};