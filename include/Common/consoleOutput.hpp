#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

// A parent that stops reading stdout must not prevent stdin EOF/shutdown handling.
class ConsoleOutput{
    public:
        ConsoleOutput(){
#ifdef _WIN32
            handle = GetStdHandle(STD_OUTPUT_HANDLE);
            if(GetFileType(handle) == FILE_TYPE_PIPE){
                DWORD mode = PIPE_NOWAIT;
                if(!SetNamedPipeHandleState(handle, &mode, nullptr, nullptr)){
                    throw std::runtime_error("Cannot configure nonblocking IPC stdout");
                }
            }
#else
            flags = fcntl(STDOUT_FILENO, F_GETFL);
            if(flags < 0 || fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0){
                throw std::runtime_error("Cannot configure nonblocking IPC stdout");
            }
#endif
        }
        ~ConsoleOutput(){
#ifndef _WIN32
            if(flags >= 0) fcntl(STDOUT_FILENO, F_SETFL, flags);
#endif
        }
        bool Poll(std::string& pending, std::size_t& offset){
            if(offset >= pending.size()) return true;
            const auto size = std::min<std::size_t>(2048, pending.size() - offset);
#ifdef _WIN32
            DWORD written = 0;
            if(!WriteFile(handle, pending.data() + offset, static_cast<DWORD>(size), &written, nullptr)){
                const auto error = GetLastError();
                return error == ERROR_PIPE_BUSY;
            }
            offset += written;
#else
            const auto written = write(STDOUT_FILENO, pending.data() + offset, size);
            if(written < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
            offset += static_cast<std::size_t>(written);
#endif
            return true;
        }

    private:
#ifdef _WIN32
        HANDLE handle;
#else
        int flags = -1;
#endif
};

