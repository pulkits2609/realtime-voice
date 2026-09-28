#pragma once

#include <queue>
#include <mutex>
#include <cstddef>
#include <utility>

//currently in this project, there are 3 independent
//tasks working :
//thread 1 : AudioCapture / MiniAudio
//thread 2: Network Receive
//thread 3 : Client Processing (encoding + sending audio , processing received network audio)

//why we are using this thread template : 
//because multiple threads want to safely put, update, extract data from a single queue

//therefore this same class can be used for a vector<float> , or mic audio, or network packets, even for messages in string

//the queue logic stays identical, only type changes

template<typename T>
class ThreadSafeQueue{
    private:
        std::queue<T> queue;
        mutable std::mutex mutex;
    
    public:
        void Push(
            const T& value
        );

        void Push(
            T&& value //this push supports moving objects too
        );

        bool Pop(
            T& value
        );

        std::size_t Size() const;
};

//push now supports both copying value, and moving value

template<typename T>
void ThreadSafeQueue<T>::Push(
    const T& value
){
    std::lock_guard<std::mutex> lock(
        mutex
    );

    queue.push(
        value
    );
}

template<typename T>
void ThreadSafeQueue<T>::Push(
    T&& value
){
    std::lock_guard<std::mutex> lock(mutex);

    queue.push(
        std::move(value)
    );
}

template<typename T>
bool ThreadSafeQueue<T>::Pop(
    T& value
){
    std::lock_guard<std::mutex> lock(
        mutex
    );
    if(queue.empty()){
        return false;
    }
    value = queue.front();
    queue.pop();
    return true;
}

template<typename T>
std::size_t ThreadSafeQueue<T>::Size() const{
    std::lock_guard<std::mutex> lock(
        mutex
    );

    return queue.size();
}