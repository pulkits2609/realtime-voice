#include "Audio/audioPlayback.hpp"
#include <algorithm>

AudioPlayback::AudioPlayback():
pcmBuffer(48000){

}

AudioPlayback::~AudioPlayback(){
    Stop();
}

bool AudioPlayback::Initialize(){
    if(initialized){
        return false;
    }

    ma_device_config config = ma_device_config_init(
        ma_device_type_playback
    );

    config.sampleRate = 48000;
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.dataCallback = DataCallback;
    config.pUserData = this;

    const ma_result result = ma_device_init(
        nullptr,
        &config,
        &device
    );

    if(result != MA_SUCCESS){
        return false;
    }

    initialized = true;
    return true;
}

bool AudioPlayback::Start(){
    if(!initialized || started){
        return false;
    }

    const ma_result result = ma_device_start(
        &device
    );

    if(result != MA_SUCCESS){
        return false;
    }

    started = true;
    return true;
}

void AudioPlayback::Stop(){
    if(!initialized){
        return;
    }
    if(started){
        ma_device_stop(
            &device
        );
        started = false;
    }

    ma_device_uninit(
        &device
    );
    initialized = false;
}

void AudioPlayback::Push(
    const float* samples,
    std::size_t sampleCount
){
    pcmBuffer.Push(
        samples,sampleCount
    );
}

//mini audio calls this whenever teh speaker needs more audio

void AudioPlayback::DataCallback(
    ma_device* device,
    void* output,
    const void* input,
    ma_uint32 framecount
){
    auto* audioPlayback = static_cast<AudioPlayback*>(
        device->pUserData
    );
    if(audioPlayback == nullptr || output == nullptr){
        return;
    }
    
    float* outputSamples = static_cast<float*>(output);

    //we are going to fill the entire buffer with silence, because lets say we dont have enough decoded samples available, remaining samples remain silent !
    std::fill(
        outputSamples,
        outputSamples+framecount,
        0.0f
    );

    //now copy the decoded samples, JITNA BHI HAI SAB KA SAB !
    audioPlayback->pcmBuffer.PopAvailable(
        outputSamples,
        framecount
    );

}