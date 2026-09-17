#include "Codec/opusEncoder.hpp"

OpusEncoderWrapper::OpusEncoderWrapper(){

}

OpusEncoderWrapper::~OpusEncoderWrapper(){
    if(encoder != nullptr){
        opus_encoder_destroy(
            encoder
        );

        encoder = nullptr;
    }
}

bool OpusEncoderWrapper::Initialize(
    int sampleRate,
    int channels,
    int bitrate
){
    int error = OPUS_OK;

    //here we are requesting to create
    //an Opus encoder
    //for 48000Hz , 1 channel, for voice Communication
    encoder = opus_encoder_create(
        sampleRate,
        channels,
        OPUS_APPLICATION_VOIP,
        &error
    );

    if(encoder == nullptr || error != OPUS_OK){
        encoder == nullptr;
        return false;
    }

    const int bitrateResult = opus_encoder_ctl(
        encoder,
        OPUS_SET_BITRATE(
            bitrate
        )
    );

    if(bitrateResult != OPUS_OK){
        opus_encoder_destroy(
            encoder
        );
        encoder = nullptr;

        return false;
    }
    return true;
}


int OpusEncoderWrapper::Encode(
    const float* pcm,
    int frameSize,
    std::uint8_t* output,
    int maxOutputBytes
){
    if(encoder == nullptr || pcm == nullptr || output == nullptr){
        return OPUS_BAD_ARG;
    }


    //this function takes encoder, PCM samples, frame size, output buffer, maximum output size, and returns the number of incoded bytes ! or a negative OPUS ERROR CODE
    return opus_encode_float(
        encoder,
        pcm,
        frameSize,
        output,
        maxOutputBytes
    );
}


