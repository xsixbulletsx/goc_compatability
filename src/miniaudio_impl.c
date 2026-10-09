/* miniaudio implementation unit: built-in mp3 decoder + WASAPI only (no dependency on Windows codecs). */
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define MA_NO_WAV
#define MA_NO_NODE_GRAPH_EXTRAS
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MA_ENABLE_WINMM
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
