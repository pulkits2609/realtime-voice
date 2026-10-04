#pragma once
#include <iostream>

#ifdef VOICE_DEBUG

    #define DEBUG_LOG(...) \
        do{ \
            std::cout << __VA_ARGS__; \
        }while(false)

#else

    #define DEBUG_LOG(...) \
        do{ \
        }while(false)

#endif