#include "Audio/pcmBuffer.hpp"

PcmBuffer::PcmBuffer(
    std::size_t maxSamples
):
maxSamples(maxSamples){

}

void PcmBuffer::Push(
    const float* samples,
    std::size_t count
){
    if(samples == nullptr || count == 0) return;

    std::lock_guard<std::mutex> lock(
        mutex_ //as soon as execution reaches here, lock the mutex, execute function, unlock mutex automatically when it leaves scope (we dont need to deal with lock and unlocking manually)
    );

    for(std::size_t i=0; i<count; i++){
        this->samples.push_back(
            samples[i]
        );
    }

    while(this->samples.size() > maxSamples){
        this->samples.pop_front();
    }

    //for eg if maxSamples = 4800
    //and we accidentally receive 6000 samples
    //the oldest 1200 are discarded
    //for an audio stream keeping the newest audio is generally more useful than allowing queue to grow indefinitely
}

bool PcmBuffer::PopExact(
    float* output, //notice that this is not const, because we will actually be adding data to this
    std::size_t count
){
    if(output == nullptr || count == 0){
        return false;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if(samples.size() < count){
        return false;
    }

    for(std::size_t i=0; i<count; i++){
        output[i] = samples.front();
        //we are copying the oldest buffered sample into the output
        samples.pop_front();
    }

    return true;
}

//last is this teeny tiny helper func

std::size_t PcmBuffer::Size() const{
    std::lock_guard<std::mutex> lock(
        mutex_
    );
    return samples.size();
}
