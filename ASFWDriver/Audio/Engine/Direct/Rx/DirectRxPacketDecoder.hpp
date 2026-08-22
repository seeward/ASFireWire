#pragma once

#include "DirectRxTypes.hpp"
#include "RxCaptureChannelMap.hpp"
#include "../../../Wire/AM824/AM824Decoder.hpp"
// AudioWireFormat. Previously reached this header only via whoever included it
// first; naming it here lets the decoder be included on its own.
#include "../../../Wire/AMDTP/AmdtpTypes.hpp"
#include <cstdint>

namespace ASFW::AudioEngine::Direct::Rx {

namespace Detail {

[[nodiscard]] constexpr float Signed24ToFloat32(int32_t sample) noexcept {
    if (sample <= -8388608) {
        return -1.0f;
    }
    return static_cast<float>(sample) / 8388607.0f;
}

[[nodiscard]] inline float DecodeAm824SlotToFloat32(uint32_t wireQuadlet) noexcept {
    auto sample = ASFW::Isoch::AM824Decoder::DecodeSample(wireQuadlet);
    return sample ? Signed24ToFloat32(*sample) : 0.0f;
}

[[nodiscard]] inline float DecodeRawSlotAsLabeledMBLAToFloat32(uint32_t wireQuadlet) noexcept {
    const uint32_t hostQuadlet = OSSwapBigToHostInt32(wireQuadlet);
    // Saffire raw capture carries native signed 24-in-32 slots. Normalize it
    // to an AM824 MBLA slot by adding the 0x40 label at the wire/content seam;
    // cross-validated with Linux amdtp-am824.c:170 and :200.
    const uint32_t labeledHostQuadlet =
        0x40000000u | (hostQuadlet & 0x00FFFFFFu);
    return DecodeAm824SlotToFloat32(OSSwapHostToBigInt32(labeledHostQuadlet));
}

} // namespace Detail

inline void DecodeDirectRxFrame(const uint32_t* inWireQuadlets,
                                uint32_t pcmChannels,
                                uint32_t am824Slots,
                                ASFW::Encoding::AudioWireFormat format,
                                float* outPcmFrame) noexcept {
    (void)am824Slots;
    for (uint32_t ch = 0; ch < pcmChannels; ++ch) {
        if (format == ASFW::Encoding::AudioWireFormat::kRawPcm24In32) {
            outPcmFrame[ch] =
                Detail::DecodeRawSlotAsLabeledMBLAToFloat32(inWireQuadlets[ch]);
        } else {
            outPcmFrame[ch] =
                Detail::DecodeAm824SlotToFloat32(inWireQuadlets[ch]);
        }
    }
}

/// Decodes one wire frame through a capture channel map.
///
/// Channels the map marks as delayed are written to `outDelayedFrame` — the
/// same channel index, but a frame further along the writer's timeline. Because
/// the writer is addressed by absolute frame, "delay this channel by N frames"
/// and "write this channel N frames later" are the same operation, so the delay
/// costs no history buffer and no per-stream state.
///
/// `outDelayedFrame` may be null only when the map declares no delay.
inline void DecodeDirectRxFrameMapped(const uint32_t* inWireQuadlets,
                                      uint32_t pcmChannels,
                                      ASFW::Encoding::AudioWireFormat format,
                                      const RxCaptureChannelMap& map,
                                      float* outPcmFrame,
                                      float* outDelayedFrame) noexcept {
    for (uint32_t ch = 0; ch < pcmChannels; ++ch) {
        float* destination = outPcmFrame;
        if (map.IsDelayed(ch)) {
            if (outDelayedFrame == nullptr) {
                // The delayed frame fell outside the writer's range. Dropping
                // the sample keeps the undelayed channels correct rather than
                // writing this one to the wrong instant.
                continue;
            }
            destination = outDelayedFrame;
        }
        const uint32_t quadlet = inWireQuadlets[map.SlotFor(ch)];
        destination[ch] =
            format == ASFW::Encoding::AudioWireFormat::kRawPcm24In32
                ? Detail::DecodeRawSlotAsLabeledMBLAToFloat32(quadlet)
                : Detail::DecodeAm824SlotToFloat32(quadlet);
    }
}

/// Silences the delayed channels of one frame.
///
/// Used to prime the head of a delay line: the first `delayFrames` frames of a
/// stream have no predecessor to source those channels from, so without this
/// they would expose whatever the shared input buffer held from a previous run.
inline void SilenceDelayedChannels(uint32_t pcmChannels,
                                   const RxCaptureChannelMap& map,
                                   float* outPcmFrame) noexcept {
    for (uint32_t ch = 0; ch < pcmChannels; ++ch) {
        if (map.IsDelayed(ch)) {
            outPcmFrame[ch] = 0.0f;
        }
    }
}

} // namespace ASFW::AudioEngine::Direct::Rx
