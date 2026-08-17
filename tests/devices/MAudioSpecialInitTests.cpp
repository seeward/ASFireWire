// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// Hardware-free wire test for the FireWire 1814 / ProjectMix clock-configuration
// sequence. It uses the real FCP transport and a deterministic target rather
// than replacing either command submission path with a mock.
//
// The invariant under test is that exactly one policy is applied. Two exist:
// the vendor kext's (source 0 plus selector FB 4, issued against a live output
// plug) and Linux's (source 3, no selector). ASFW follows Linux, and mixing the
// two is what the command-count assertions here are guarding against.

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Protocols/BeBoB/MAudioClockCommand.hpp"
#include "ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialProtocol.hpp"

#include "AvcTestRig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using ASFW::Audio::BeBoB::MAudioSpecialModel;
using ASFW::Audio::BeBoB::MAudioSpecialProtocol;
using ASFW::Protocols::AVC::FCPFrame;
using ASFW::Testing::AvcReply;
using ASFW::Testing::AvcTestRig;

constexpr uint64_t Milliseconds(uint64_t value) {
    return value * 1'000'000ULL;
}

template <size_t N>
void ExpectFrame(const FCPFrame& actual,
                 const std::array<uint8_t, N>& expected) {
    ASSERT_EQ(actual.length, expected.size());
    for (size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(actual.data[index], expected[index]) << "byte " << index;
    }
}

TEST(MAudioSpecialInitTests, SendsInternalClockSourceAndNoSelector) {
    AvcTestRig rig;
    ASSERT_TRUE(rig.IsReady());

    MAudioSpecialProtocol protocol(
        rig.Bus(), rig.Bus(), rig.Route(), nullptr, nullptr, &rig.Timers(),
        MAudioSpecialModel::FireWire1814);
    protocol.UpdateRuntimeContext(rig.Route(), rig.Transport());

    bool completed = false;
    IOReturn completionStatus = kIOReturnBusy;
    protocol.InitializeClock([&](IOReturn status) {
        completed = true;
        completionStatus = status;
    });

    // One vendor clock frame, with operand 6 == 0x03 ("Internal"). Linux sends
    // exactly this at discovery (bebob_maudio.c:276). Operand 0 would be the
    // vendor kext's "Internal with Digital Mute", which is only safe paired with
    // the selector that releases it.
    EXPECT_EQ(rig.Drain(), 1U);
    ASSERT_EQ(rig.Target().CommandCount(), 1U);
    constexpr std::array<uint8_t, 16> clockFrame{
        0x00, 0xFF, 0x00, 0x04, 0x00, 0x04, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ExpectFrame(rig.Target().Commands()[0], clockFrame);
    EXPECT_FALSE(completed);

    rig.Timers().Advance(Milliseconds(2499));
    EXPECT_EQ(rig.Drain(), 0U);
    EXPECT_FALSE(completed);

    // The settle expiring is not the end of initialisation: it releases
    // SendParameterBlock, whose 160-byte write to the parameter window is a real
    // asynchronous bus transaction and needs a drain before its completion runs.
    // Asserting on `completed` without that drain is what left this test red
    // from 5fbb6ad1 onwards.
    rig.Timers().Advance(Milliseconds(1));
    EXPECT_FALSE(completed);
    EXPECT_EQ(rig.Drain(), 1U);
    EXPECT_TRUE(completed);
    EXPECT_EQ(completionStatus, kIOReturnSuccess);

    // Still one AV/C command: no selector was ever submitted, before or after
    // the settle. This is the assertion that keeps the two policies from being
    // recombined into the hybrid that shipped previously.
    EXPECT_EQ(rig.Target().CommandCount(), 1U);
}

TEST(MAudioSpecialInitTests, ClockRefusalFailsInitializationWithoutSettle) {
    AvcTestRig rig;
    ASSERT_TRUE(rig.IsReady());
    rig.Target().Script(AvcReply::Rejected());

    MAudioSpecialProtocol protocol(
        rig.Bus(), rig.Bus(), rig.Route(), nullptr, nullptr, &rig.Timers(),
        MAudioSpecialModel::FireWire1814);
    protocol.UpdateRuntimeContext(rig.Route(), rig.Transport());

    bool completed = false;
    IOReturn completionStatus = kIOReturnSuccess;
    protocol.InitializeClock([&](IOReturn status) {
        completed = true;
        completionStatus = status;
    });

    // A device that refused its clock configuration is not one to publish, so
    // the refusal completes immediately rather than arming the settle timer.
    EXPECT_EQ(rig.Drain(), 1U);
    EXPECT_TRUE(completed);
    EXPECT_NE(completionStatus, kIOReturnSuccess);
    EXPECT_EQ(rig.Target().CommandCount(), 1U);
    EXPECT_EQ(rig.Timers().PendingCount(), 0U);
}

// The parameter window's last four quadlets are the device's signal routing.
// Zeroing them — which is what FFADO's Mixer::initialize does, because FFADO
// ships a mixer GUI to fill the matrix back in — leaves the 1814 reporting
// "There are no connections!" and metering silence on every physical output
// while a healthy stream arrives. That failure is invisible from the wire: the
// stream is accepted, the framer reports no mismatch, and the timing generator
// locks. Only the device's own mixer state shows it, so it gets a test.
//
// Expected values are the ALSA userspace BeBoB crate's parameter defaults,
// protocols/bebob/src/maudio/special.rs.
TEST(MAudioSpecialInitTests, AssertsMixerRoutingSoStreamsReachTheOutputs) {
    AvcTestRig rig;
    ASSERT_TRUE(rig.IsReady());

    MAudioSpecialProtocol protocol(
        rig.Bus(), rig.Bus(), rig.Route(), nullptr, nullptr, &rig.Timers(),
        MAudioSpecialModel::FireWire1814);
    protocol.UpdateRuntimeContext(rig.Route(), rig.Transport());

    protocol.InitializeClock([](IOReturn) {});
    EXPECT_EQ(rig.Drain(), 1U);
    rig.Timers().Advance(Milliseconds(2500));

    // The AV/C clock command is itself a block write to the FCP register, so
    // select the parameter window by address rather than by submission order.
    const std::vector<uint8_t>* window = nullptr;
    for (size_t index = 0; index < rig.Bus().WriteCount(); ++index) {
        const auto& candidate = rig.Bus().WriteAt(index);
        if (candidate.address.addressHi == 0xFFC7 &&
            candidate.address.addressLo == 0x0070'0000) {
            window = &candidate.data;
        }
    }
    ASSERT_NE(window, nullptr) << "no write to the parameter window";
    ASSERT_EQ(window->size(), 160U) << "parameter window is 40 quadlets";

    const auto quadletAt = [window](size_t offset) {
        return (static_cast<uint32_t>((*window)[offset]) << 24) |
               (static_cast<uint32_t>((*window)[offset + 1]) << 16) |
               (static_cast<uint32_t>((*window)[offset + 2]) << 8) |
               static_cast<uint32_t>((*window)[offset + 3]);
    };

    // No physical input is mixed in: playback must not arrive folded together
    // with the analog inputs the device is simultaneously capturing.
    EXPECT_EQ(quadletAt(0x90), 0x00000000U) << "analog/spdif/adat -> mixer";
    // Stream pair 0 -> mixer pair 0, stream pair 1 -> mixer pair 1, encoded as
    // 1 << (pair * 2 + mixer). This is the quadlet whose absence is silence.
    EXPECT_EQ(quadletAt(0x94), 0x00000009U) << "stream -> mixer";
    // Both headphone pairs follow those mixer pairs, encoded as flag << (pair * 16).
    EXPECT_EQ(quadletAt(0x98), 0x00020001U) << "headphone pair source";
    // Analog output pairs take the mixer output rather than the aux bus.
    EXPECT_EQ(quadletAt(0x9c), 0x00000000U) << "analog output pair source";
}

} // namespace
