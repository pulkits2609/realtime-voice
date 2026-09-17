#include "Codec/opusDecoder.hpp"

OpusDecoderWrapper::OpusDecoderWrapper(){

}

OpusDecoderWrapper::~OpusDecoderWrapper(){
    if(decoder != nullptr){
        opus_decoder_destroy(
            decoder
        );

        decoder = nullptr;
    }
}

bool OpusDecoderWrapper::Initialize(
    int sampleRate,
    int channels
){
    if(decoder != nullptr){
        return false;
    }

    int error = OPUS_OK;

    decoder = opus_decoder_create(
        sampleRate,
        channels,
        &error
    );

    if(decoder == nullptr || error != OPUS_OK){
        decoder = nullptr;
        return false;
    }
    
    return true;
}

int OpusDecoderWrapper::Decode(
    const std::uint8_t* packet,
    int packetBytes,
    float* output,
    int maxFrameSize
){
    if(decoder == nullptr || packet == nullptr || output == nullptr){
        return OPUS_BAD_ARG;
    }

    return opus_decode_float(
        decoder,
        packet,
        packetBytes,
        output,
        maxFrameSize,
        0 //this 0 means : decode_fec = false , typically required for packet loss recovery and handling, we arent implementing it yet
    );
}