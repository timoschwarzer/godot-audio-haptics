#if defined(DEBUG_ENABLED)
    #define MA_DEBUG_OUTPUT
#endif

#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_WASAPI
#define MA_ENABLE_COREAUDIO

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wduplicated-branches"
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "miniaudio.h"

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
    #pragma GCC diagnostic pop
#endif
