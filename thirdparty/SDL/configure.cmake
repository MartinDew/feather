FetchContent_Declare(SDL3 SDL_Mixer SDL_Image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG e6987e2452aa8619d4174ac06bb465e36de337c9 # release 3.2.x
        EXCLUDE_FROM_ALL
)

set(FETCHED_TPS SDL3)

# if release build then use static linking
IF (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SDL_SHARED OFF CACHE BOOL "Use shared SDL library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Use shared SDL library" FORCE)
ELSE()
    set(SDL_SHARED ON CACHE BOOL "Use shared SDL library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Use shared SDL library" FORCE)
ENDIF()

# SDL_mixer (used for playing audio)
#set(SDLMIXER_MIDI_NATIVE OFF)     # disable formats we don't use to make the build faster and smaller. Also some of these don't work on all platforms so you'll need to do some experimentation.
#set(SDLMIXER_GME OFF)
#set(SDLMIXER_WAVPACK OFF)
#set(SDLMIXER_MOD OFF)
#set(SDLMIXER_OPUS OFF)
#set(SDLMIXER_MP3_MPG123 OFF)
#set(SDLMIXER_VORBIS_VORBISFILE OFF)
#set(SDLMIXER_FLAC_LIBFLAC OFF)
# set(SDLMIXER_VENDORED ON)   # tell SDL_mixer to build its own dependencies
