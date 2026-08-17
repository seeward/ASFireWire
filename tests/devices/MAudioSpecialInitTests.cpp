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

} // namespace
