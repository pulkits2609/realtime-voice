#pragma once

#include <iostream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <conio.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

// Polling terminal/pipe input keeps partial lines from blocking shutdown.
// No input thread is detached or left waiting in getline().
class ConsoleInput{
    public:
        enum Result { None, Line, Closed, TooLong };
        explicit ConsoleInput(bool echo = true):echo(echo){}
        static bool IsTerminal(){
#ifdef _WIN32
            DWORD mode = 0;
            return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
#else
            return isatty(STDIN_FILENO) != 0;
#endif
        }
        Result Poll(std::string& line){
            for(unsigned count = 0; count < 256; count++){
                char character = 0;
#ifdef _WIN32
                HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
                DWORD mode = 0, available = 0, read = 0;
                if(GetConsoleMode(handle, &mode)){
                    if(!_kbhit()) return None;
                    const int key = _getch();
                    if(key == 0 || key == 224){ if(_kbhit()) _getch(); continue; }
                    character = static_cast<char>(key);
                    if(character == '\r') character = '\n';
                    if(character == '\b'){
                        if(!buffer.empty()) buffer.pop_back();
                        if(echo) std::cout << "\b \b" << std::flush;
                        continue;
                    }
                    if(echo) std::cout << character << std::flush;
                }
                else{
                    if(GetFileType(handle) != FILE_TYPE_DISK){
                        if(!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) return Closed;
                        if(!available) return None;
                    }
                    if(!ReadFile(handle, &character, 1, &read, nullptr) || !read) return Closed;
                }
#else
                pollfd input{STDIN_FILENO, POLLIN, 0};
                const int ready = poll(&input, 1, 0);
                if(ready <= 0) return None;
                if(read(STDIN_FILENO, &character, 1) <= 0) return Closed;
#endif
                if(character == '\r') continue;
                if(character == '\n'){
                    if(overflow){
                        buffer.clear(); overflow = false; return TooLong;
                    }
                    line.swap(buffer); buffer.clear(); return Line;
                }
                if(buffer.size() < 8192) buffer.push_back(character);
                else overflow = true;
            }
            return None;
        }

    private:
        std::string buffer;
        bool overflow = false, echo;
};

