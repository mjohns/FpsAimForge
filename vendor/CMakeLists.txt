set(SPDLOG_USE_STD_FORMAT ON)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_ENABLE_INSTALL ON)
if (NOT BUILD_SHARED_LIBS)
  set(ABSL_MSVC_STATIC_RUNTIME ON)
endif()

set(GLM_BUILD_LIBRARY ON)

set(SDLMIXER_FLAC OFF)
set(SDLMIXER_GME OFF)
set(SDLMIXER_MP3 OFF)
set(SDLMIXER_MIDI OFF)
set(SDLMIXER_WAVE OFF)
set(SDLMIXER_WAVPACK OFF)
set(SDLMIXER_OPUS OFF)
set(SDLMIXER_VORBIS_TREMOR OFF)
set(SDLMIXER_VORBIS_VORBISFILE OFF)
set(SDLMIXER_MOD OFF)

set(protobuf_LOCAL_DEPENDENCIES_ONLY ON)
set(protobuf_BUILD_TESTS OFF)
set(protobuf_BUILD_PROTOC_BINARIES  ON)
set(protobuf_BUILD_LIBPROTOC ON)
set(protobuf_BUILD_LIBUPB ON)
set(protobuf_BUILD_EXAMPLES OFF)
set(protobuf_BUILD_CONFORMANCE OFF)
set(protobuf_WITH_ZLIB OFF)

set(SDL_TESTS off)
set(SDL_EXAMPLES off)

add_subdirectory(absl)
add_subdirectory(imgui)
add_subdirectory(sqlite3)
add_subdirectory(glm)
add_subdirectory(implot)
add_subdirectory(spdlog)
add_subdirectory(protobuf)
add_subdirectory(googletest)
add_subdirectory(protobuf-matchers)
add_subdirectory(miniz)
include(${CMAKE_CURRENT_SOURCE_DIR}/protobuf/cmake/protobuf-generate.cmake)

# version 3.4.14
add_subdirectory(SDL3)
add_subdirectory(SDL_mixer)

add_subdirectory(SDL_image)

