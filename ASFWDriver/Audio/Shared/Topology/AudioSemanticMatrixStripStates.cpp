// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "AudioSemanticMatrixStripStates.hpp"

namespace ASFW::Audio {

namespace {

struct AxisPair final {
    bool valid{false};
    uint32_t left{0};
    uint32_t right{0};
    bool mono{false};
};

/// Collects the members of one source presentation group. A group is usable
/// only as a single mono member or as exactly one left/right pair; anything
/// else is ambiguous and is refused rather than guessed.
[[nodiscard]] AxisPair CollectInputGroup(
    const std::array<AudioSemanticMatrixAxis, kMaxAudioSemanticMatrixInputs>& axes,
    uint32_t count, uint32_t groupId) noexcept {
    AxisPair pair{};
    bool haveLeft = false;
    bool haveRight = false;
    for (uint32_t index = 0; index < count; ++index) {
        const auto& axis = axes[index];
        if (axis.presentationGroupId != groupId) continue;
        switch (axis.channelRole) {
        case AudioSemanticMatrixChannelRole::Mono:
            if (pair.mono || haveLeft || haveRight) return {};
            pair.mono = true;
            pair.left = index;
            pair.right = index;
            break;
        case AudioSemanticMatrixChannelRole::Left:
            if (haveLeft || pair.mono) return {};
            haveLeft = true;
            pair.left = index;
            break;
        case AudioSemanticMatrixChannelRole::Right:
            if (haveRight || pair.mono) return {};
            haveRight = true;
            pair.right = index;
            break;
        default:
            return {};
        }
    }
    pair.valid = pair.mono || (haveLeft && haveRight);
    return pair;
}

/// Output axes live in a differently sized array than input axes, so the group
/// collection is repeated rather than templated across the two bounds.
[[nodiscard]] AxisPair CollectOutputGroup(const AudioSemanticMatrixSnapshot& snapshot,
                                          uint32_t groupId) noexcept {
    AxisPair pair{};
    bool haveLeft = false;
    bool haveRight = false;
    for (uint32_t index = 0; index < snapshot.outputCount; ++index) {
        const auto& axis = snapshot.outputs[index];
        if (axis.presentationGroupId != groupId) continue;
        if (axis.channelRole == AudioSemanticMatrixChannelRole::Left) {
            if (haveLeft) return {};
            haveLeft = true;
            pair.left = index;
        } else if (axis.channelRole == AudioSemanticMatrixChannelRole::Right) {
            if (haveRight) return {};
            haveRight = true;
            pair.right = index;
        } else {
            // A mono destination has no left/right cells to hold a pan, so it
            // is not a strip destination.
            return {};
        }
    }
    pair.valid = haveLeft && haveRight;
    return pair;
}

[[nodiscard]] bool CellsAreWritable(const AudioSemanticMatrixSnapshot& snapshot,
                                    const AudioSemanticMatrixStripCells& strip) noexcept {
    const auto expected = strip.IsMono()
        ? AudioSemanticMatrixCrosspointPresentation::MonoLevelPan
        : AudioSemanticMatrixCrosspointPresentation::StereoLevelBalance;
    return snapshot.CrosspointPresentation(strip.outputLeft, strip.inputLeft) == expected &&
           snapshot.CrosspointPresentation(strip.outputRight, strip.inputRight) == expected;
}

} // namespace

bool ResolveAudioSemanticMatrixStrip(const AudioSemanticMatrixSnapshot& snapshot,
                                     uint32_t outputPresentationGroupId,
                                     uint32_t inputPresentationGroupId,
                                     AudioSemanticMatrixStripCells& outStrip) noexcept {
    outStrip = {};
    if (outputPresentationGroupId == 0 || inputPresentationGroupId == 0) return false;

    const auto outputs = CollectOutputGroup(snapshot, outputPresentationGroupId);
    if (!outputs.valid) return false;
    const auto inputs = CollectInputGroup(snapshot.inputs, snapshot.inputCount,
                                          inputPresentationGroupId);
    if (!inputs.valid) return false;

    AudioSemanticMatrixStripCells strip{
        .outputPresentationGroupId = outputPresentationGroupId,
        .inputPresentationGroupId = inputPresentationGroupId,
        .outputLeft = outputs.left,
        .outputRight = outputs.right,
        .inputLeft = inputs.left,
        .inputRight = inputs.right,
    };
    if (!CellsAreWritable(snapshot, strip)) return false;
    outStrip = strip;
    return true;
}

bool EnumerateAudioSemanticMatrixBusStrips(
    const AudioSemanticMatrixSnapshot& snapshot,
    uint32_t outputPresentationGroupId,
    std::array<AudioSemanticMatrixStripCells,
               kMaxAudioSemanticMatrixStripsPerBus>& outStrips,
    uint32_t& outCount) noexcept {
    outStrips = {};
    outCount = 0;
    if (!CollectOutputGroup(snapshot, outputPresentationGroupId).valid) return false;

    for (uint32_t index = 0; index < snapshot.inputCount; ++index) {
        const uint32_t groupId = snapshot.inputs[index].presentationGroupId;
        bool alreadySeen = false;
        for (uint32_t seen = 0; seen < outCount; ++seen) {
            if (outStrips[seen].inputPresentationGroupId == groupId) {
                alreadySeen = true;
                break;
            }
        }
        if (alreadySeen) continue;

        AudioSemanticMatrixStripCells strip{};
        if (!ResolveAudioSemanticMatrixStrip(snapshot, outputPresentationGroupId,
                                             groupId, strip)) {
            continue;
        }
        if (outCount >= kMaxAudioSemanticMatrixStripsPerBus) return false;
        outStrips[outCount++] = strip;
    }
    return true;
}

const AudioSemanticMatrixStripState* AudioSemanticMatrixStripStateSet::Find(
    uint32_t outputPresentationGroupId,
    uint32_t inputPresentationGroupId) const noexcept {
    for (uint32_t index = 0; index < count_; ++index) {
        if (states_[index].outputPresentationGroupId == outputPresentationGroupId &&
            states_[index].inputPresentationGroupId == inputPresentationGroupId) {
            return &states_[index];
        }
    }
    return nullptr;
}

bool AudioSemanticMatrixStripStateSet::Upsert(
    const AudioSemanticMatrixStripState& state) noexcept {
    if (state.outputPresentationGroupId == 0 || state.inputPresentationGroupId == 0) {
        return false;
    }
    for (uint32_t index = 0; index < count_; ++index) {
        if (states_[index].outputPresentationGroupId == state.outputPresentationGroupId &&
            states_[index].inputPresentationGroupId == state.inputPresentationGroupId) {
            states_[index] = state;
            return true;
        }
    }
    if (count_ >= kMaxAudioSemanticMatrixStripStates) return false;
    states_[count_++] = state;
    return true;
}

void AudioSemanticMatrixStripStateSet::Remove(
    uint32_t outputPresentationGroupId,
    uint32_t inputPresentationGroupId) noexcept {
    for (uint32_t index = 0; index < count_; ++index) {
        if (states_[index].outputPresentationGroupId != outputPresentationGroupId ||
            states_[index].inputPresentationGroupId != inputPresentationGroupId) {
            continue;
        }
        states_[index] = states_[count_ - 1];
        states_[count_ - 1] = {};
        --count_;
        return;
    }
}

void AudioSemanticMatrixStripStateSet::Prune() noexcept {
    uint32_t index = 0;
    while (index < count_) {
        const auto& state = states_[index];
        const bool carriesNothing = state.muted == 0 && state.soloed == 0 &&
            !BusHasSolo(state.outputPresentationGroupId);
        if (carriesNothing) {
            states_[index] = states_[count_ - 1];
            states_[count_ - 1] = {};
            --count_;
            continue;
        }
        ++index;
    }
}

bool AudioSemanticMatrixStripStateSet::BusHasSolo(
    uint32_t outputPresentationGroupId) const noexcept {
    for (uint32_t index = 0; index < count_; ++index) {
        if (states_[index].outputPresentationGroupId == outputPresentationGroupId &&
            states_[index].soloed != 0) {
            return true;
        }
    }
    return false;
}

bool AudioSemanticMatrixStripStateSet::IsSuppressed(
    uint32_t outputPresentationGroupId,
    uint32_t inputPresentationGroupId) const noexcept {
    const auto* state = Find(outputPresentationGroupId, inputPresentationGroupId);
    if (state != nullptr && state->muted != 0) return true;
    if (!BusHasSolo(outputPresentationGroupId)) return false;
    return state == nullptr || state->soloed == 0;
}

void AudioSemanticMatrixStripStateSet::CopyInto(
    AudioSemanticMatrixSnapshot& snapshot) const noexcept {
    snapshot.stripStateCount = count_;
    snapshot.stripStates = states_;
}

AudioSemanticMatrixStripCoefficients AudioSemanticMatrixStripNominal(
    const AudioSemanticMatrixSnapshot& snapshot,
    const AudioSemanticMatrixStripStateSet& states,
    const AudioSemanticMatrixStripCells& strip) noexcept {
    if (const auto* state = states.Find(strip.outputPresentationGroupId,
                                        strip.inputPresentationGroupId)) {
        return {.left = state->nominalLeft, .right = state->nominalRight};
    }
    return {
        .left = snapshot.Coefficient(strip.outputLeft, strip.inputLeft),
        .right = snapshot.Coefficient(strip.outputRight, strip.inputRight),
    };
}

AudioSemanticMatrixStripCoefficients AudioSemanticMatrixStripEffective(
    const AudioSemanticMatrixSnapshot& snapshot,
    const AudioSemanticMatrixStripStateSet& states,
    const AudioSemanticMatrixStripCells& strip) noexcept {
    if (states.IsSuppressed(strip.outputPresentationGroupId,
                            strip.inputPresentationGroupId)) {
        return {};
    }
    return AudioSemanticMatrixStripNominal(snapshot, states, strip);
}

} // namespace ASFW::Audio
