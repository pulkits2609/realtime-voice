#include "Audio/audioCapture.hpp"

AudioCapture::AudioCapture(){

}

AudioCapture::~AudioCapture(){
    Stop();
}

bool AudioCapture::Initialize(
    Callback callback
){
    if(initialized) return false;

    this->callback = std::move(callback);

    ma_device_config config = ma_device_config_init(
        ma_device_type_capture
    );

    config.sampleRate = 48000;

    config.capture.format = ma_format_f32;

    config.capture.channels = 1;

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

bool AudioCapture::Start(){
    if(!initialized || started_){ //I hate using _ in variable names...
        return false;
    }

    const ma_result result = ma_device_start(
        &device
    );

    if(result != MA_SUCCESS){
        return false;
    }

    started_ = true;
    return true;
}

void AudioCapture::Stop(){
    if(!initialized){
        return;
    }
    if(started_){
        ma_device_stop(
            &device
        );

        started_ = false;
    }

    ma_device_uninit(
        &device
    );

    initialized = false;
}

//miniAudio calls DataCallback()
//whenever it has microphone data

//the function receives const void* input, and ma_uint32 frameCount

void AudioCapture::DataCallback(
    ma_device* device,
    void* output,
    const void* input,
    ma_uint32 framecount
){
    auto* audioCapture = static_cast<AudioCapture*>(
        device->pUserData
    );

    if(audioCapture == nullptr || input == nullptr){
        return;
    }

    const float* samples = static_cast<const float*>(input);

    if(audioCapture -> callback){
        audioCapture->callback(
            samples,framecount
        );
    }
    //currently void* output is completely unused because we are just testing capture
}