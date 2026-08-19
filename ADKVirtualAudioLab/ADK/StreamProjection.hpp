#pragma once

#include "../Core/Device/Stream.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ASFW::ADK {

struct ProjectedStreamFormat {
    double sampleRate{48000.0};
    uint32_t formatID{0x6c70636d}; // 'lpcm'
    uint32_t formatFlags{0x9};     // Float32 | NativeEndian
    uint32_t bytesPerPacket{0};
    uint32_t framesPerPacket{1};
    uint32_t bytesPerFrame{0};
    uint32_t channelsPerFrame{0};
    uint32_t bitsPerChannel{32};
};

enum class ProjectedStreamDirection {
    Input,  // Core Audio Input = Capture
    Output, // Core Audio Output = Playback
};

struct ProjectedAudioStream {
    ProjectedStreamDirection direction;
    std::string name;
    uint32_t channelCount{0};
    ProjectedStreamFormat format;
    uint32_t startingChannel{1};
};

struct ProjectedStreamConfiguration {
    double sampleRate{48000.0};
    std::vector<ProjectedAudioStream> streams;
    uint32_t totalCaptureChannels{0};
    uint32_t totalPlaybackChannels{0};
};

ProjectedStreamConfiguration projectStreams(
    const Device::ResolvedStreamConfiguration& config);

} // namespace ASFW::ADK
