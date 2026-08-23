// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "MAudioSpecialConsoleLayout.hpp"

#include "MAudioSpecialParameters.hpp"

namespace ASFW::Audio::BeBoB {

namespace {

using Strip = AudioSemanticConsoleStrip;
using Crosspoint = AudioSemanticConsoleCrosspoint;

constexpr uint8_t kInputFlags =
    kAudioSemanticConsoleStripLinkable |
    kAudioSemanticConsoleStripMuteable |
    kAudioSemanticConsoleStripSoloable |
    kAudioSemanticConsoleStripAssignable;
constexpr uint8_t kOutputFlags =
    kAudioSemanticConsoleStripLinkable |
    kAudioSemanticConsoleStripMuteable |
    kAudioSemanticConsoleStripAssignable;

[[nodiscard]] constexpr uint32_t Control(MAudio1814ControlGroup group, uint32_t index) noexcept {
    return MakeMAudio1814ControlId(group, index);
}

} // namespace

bool BuildMAudioSpecialConsoleLayout(
    MAudioDigitalFormat captureFormat,
    AudioSemanticConsoleLayoutSnapshot& outSnapshot) noexcept {
    using Bus = AudioSemanticConsoleBus;
    using Signal = AudioSemanticSignalKind;
    using Source = AudioSemanticConsoleSourceKind;
    using StripKind = AudioSemanticConsoleStripKind;

    outSnapshot = {};
    outSnapshot.deviceKind = kMAudioSpecialSemanticDeviceKind;
    auto appendStrip = [&outSnapshot](const Strip& strip) {
        if (outSnapshot.stripCount >= outSnapshot.strips.size()) return false;
        outSnapshot.strips[outSnapshot.stripCount++] = strip;
        return true;
    };
    auto appendCrosspoint = [&outSnapshot](const Crosspoint& crosspoint) {
        if (outSnapshot.crosspointCount >= outSnapshot.crosspoints.size()) return false;
        outSnapshot.crosspoints[outSnapshot.crosspointCount++] = crosspoint;
        return true;
    };

    // Four analogue input pairs. The first semantic signal index is the
    // physical jack number, not a parameter-window index.
    for (uint32_t pair = 0; pair < 4; ++pair) {
        const uint32_t channel = pair * 2;
        if (!appendStrip({1 + pair, StripKind::Input, Signal::AnalogLine, 2, kInputFlags,
                          channel + 1,
                          Control(MAudio1814ControlGroup::MixerAnalogGain, channel),
                          Control(MAudio1814ControlGroup::MixerAnalogBalance, channel),
                          Control(MAudio1814ControlGroup::AuxAnalogGain, channel),
                          0, Source::None, 2, static_cast<uint16_t>(channel)})) {
            return false;
        }
        if (!appendCrosspoint({pair * 2 + 1, 1 + pair, Bus::Main12, {},
                               Control(MAudio1814ControlGroup::PhysicalMixerSendMask, 0),
                               1U << pair}) ||
            !appendCrosspoint({pair * 2 + 2, 1 + pair, Bus::Main34, {},
                               Control(MAudio1814ControlGroup::PhysicalMixerSendMask, 0),
                               1U << (pair + 4U)})) {
            return false;
        }
    }

    if (captureFormat == MAudioDigitalFormat::ADAT) {
        for (uint32_t pair = 0; pair < 4; ++pair) {
            const uint32_t channel = pair * 2;
            const uint32_t stripId = 11 + pair;
            if (!appendStrip({stripId, StripKind::Input, Signal::DigitalAdat, 2, kInputFlags,
                              channel + 1,
                              Control(MAudio1814ControlGroup::MixerAdatGain, channel),
                              Control(MAudio1814ControlGroup::MixerAdatBalance, channel),
                              Control(MAudio1814ControlGroup::AuxAdatGain, channel),
                              0, Source::None, 2, static_cast<uint16_t>(10 + channel)})) {
                return false;
            }
            if (!appendCrosspoint({9 + pair * 2, stripId, Bus::Main12, {},
                                   Control(MAudio1814ControlGroup::PhysicalMixerSendMask, 0),
                                   1U << (8U + pair)}) ||
                !appendCrosspoint({10 + pair * 2, stripId, Bus::Main34, {},
                                   Control(MAudio1814ControlGroup::PhysicalMixerSendMask, 0),
                                   1U << (12U + pair)})) {
                return false;
            }
        }
    } else {
        if (!appendStrip({11, StripKind::Input, Signal::DigitalSpdif, 2, kInputFlags, 1,
                          Control(MAudio1814ControlGroup::MixerSpdifGain, 0),
                          Control(MAudio1814ControlGroup::MixerSpdifBalance, 0),
                          Control(MAudio1814ControlGroup::AuxSpdifGain, 0),
                          0, Source::None, 2, 8}) ||
            !appendCrosspoint({9, 11, Bus::Main12, {},
                               Control(MAudio1814ControlGroup::PhysicalMixerSendMask, 0), 1U << 16U}) ||
            !appendCrosspoint({10, 11, Bus::Main34, {},
                               Control(MAudio1814ControlGroup::PhysicalMixerSendMask, 0), 1U << 17U})) {
            return false;
        }
    }

    for (uint32_t pair = 0; pair < 2; ++pair) {
        const uint32_t channel = pair * 2;
        const uint32_t stripId = 21 + pair;
        if (!appendStrip({stripId, StripKind::Playback, Signal::HostStream, 2, kOutputFlags,
                          channel + 1,
                          Control(MAudio1814ControlGroup::MixerStreamGain, channel), 0,
                          Control(MAudio1814ControlGroup::AuxStreamGain, channel),
                          0, Source::None, 0, 0}) ||
            !appendCrosspoint({17 + pair * 2, stripId, Bus::Main12, {},
                               Control(MAudio1814ControlGroup::StreamMixerSendMask, 0), 1U << pair}) ||
            !appendCrosspoint({18 + pair * 2, stripId, Bus::Main34, {},
                               Control(MAudio1814ControlGroup::StreamMixerSendMask, 0), 1U << (pair + 2U)})) {
            return false;
        }
    }

    constexpr uint32_t kAnalogOut12 = 31;
    constexpr uint32_t kAnalogOut34 = 32;
    if (!appendStrip({kAnalogOut12, StripKind::Output, Signal::AnalogLine, 2, kOutputFlags, 1,
                      Control(MAudio1814ControlGroup::AnalogOutputVolume, 0), 0, 0,
                      Control(MAudio1814ControlGroup::AnalogOutputSource, 0),
                      Source::MixerOrAux, 2, 18}) ||
        !appendStrip({kAnalogOut34, StripKind::Output, Signal::AnalogLine, 2, kOutputFlags, 3,
                      Control(MAudio1814ControlGroup::AnalogOutputVolume, 2), 0, 0,
                      Control(MAudio1814ControlGroup::AnalogOutputSource, 1),
                      Source::MixerOrAux, 2, 20}) ||
        !appendStrip({33, StripKind::Auxiliary, Signal::Auxiliary, 2, kOutputFlags, 1,
                      Control(MAudio1814ControlGroup::AuxOutputVolume, 0), 0, 0,
                      0, Source::None, 2, 36}) ||
        !appendStrip({34, StripKind::Headphone, Signal::Headphone, 2, kOutputFlags, 1,
                      Control(MAudio1814ControlGroup::HeadphoneVolume, 0), 0, 0,
                      Control(MAudio1814ControlGroup::HeadphoneSource, 0),
                      Source::Mixer12Mixer34OrAux, 2, 32}) ||
        !appendStrip({35, StripKind::Headphone, Signal::Headphone, 2, kOutputFlags, 3,
                      Control(MAudio1814ControlGroup::HeadphoneVolume, 2), 0, 0,
                      Control(MAudio1814ControlGroup::HeadphoneSource, 1),
                      Source::Mixer12Mixer34OrAux, 2, 34})) {
        return false;
    }

    return true;
}

} // namespace ASFW::Audio::BeBoB
