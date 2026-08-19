#pragma once

#include "Capabilities.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ASFW::Device {

enum class StreamDirection {
    Capture,  // device -> host (recording)
    Playback, // host -> device (playback)
};

struct ResolvedAudioStream {
    StreamDirection direction;
    uint32_t channels;
    std::string name;
};

struct ResolvedStreamConfiguration {
    SampleRate sampleRate{48000};
    std::vector<ResolvedAudioStream> streams;
};

} // namespace ASFW::Device
