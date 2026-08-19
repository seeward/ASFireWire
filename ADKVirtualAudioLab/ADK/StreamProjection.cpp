#include "StreamProjection.hpp"

namespace ASFW::ADK {

ProjectedStreamConfiguration projectStreams(
    const Device::ResolvedStreamConfiguration& config) {

    ProjectedStreamConfiguration result;
    result.sampleRate = static_cast<double>(config.sampleRate);

    uint32_t captureStartingChannel = 1;
    uint32_t playbackStartingChannel = 1;

    for (const auto& stream : config.streams) {
        ProjectedAudioStream projected;
        projected.name = stream.name;
        projected.channelCount = stream.channels;

        const uint32_t bytesPerFrame = stream.channels * sizeof(float);
        projected.format = ProjectedStreamFormat{
            .sampleRate = result.sampleRate,
            .formatID = 0x6c70636d,
            .formatFlags = 0x9,
            .bytesPerPacket = bytesPerFrame,
            .framesPerPacket = 1,
            .bytesPerFrame = bytesPerFrame,
            .channelsPerFrame = stream.channels,
            .bitsPerChannel = 32,
        };

        if (stream.direction == Device::StreamDirection::Capture) {
            projected.direction = ProjectedStreamDirection::Input;
            projected.startingChannel = captureStartingChannel;
            captureStartingChannel += stream.channels;
            result.totalCaptureChannels += stream.channels;
        } else {
            projected.direction = ProjectedStreamDirection::Output;
            projected.startingChannel = playbackStartingChannel;
            playbackStartingChannel += stream.channels;
            result.totalPlaybackChannels += stream.channels;
        }

        result.streams.push_back(std::move(projected));
    }

    return result;
}

} // namespace ASFW::ADK
