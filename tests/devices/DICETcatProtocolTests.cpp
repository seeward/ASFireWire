#include <gtest/gtest.h>

#include "Testing/HostDriverKitStubs.hpp"
#include "Async/Interfaces/IFireWireBus.hpp"
#include "Common/WireFormat.hpp"
#include "Discovery/DeviceRegistry.hpp"
#include "Audio/Protocols/DICE/Core/DICETypes.hpp"
#include "Audio/Protocols/DICE/Core/DICENotificationMailbox.hpp"
#include "Audio/Protocols/DICE/Core/DICETransaction.hpp"
#include "Audio/Protocols/DICE/Focusrite/SPro24DspProtocol.hpp"
#include "Audio/Protocols/DICE/TCAT/DICETcatProtocol.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ASFW::Audio::DICE::TCAT {

class DICETcatProtocolTestPeer {
public:
    static void CacheRuntimeCaps(DICETcatProtocol& protocol,
                                 const GlobalState& global,
                                 const StreamConfig& tx,
                                 const StreamConfig& rx) {
        protocol.CacheRuntimeCaps(global, tx, rx);
    }

    static bool MakeDiceClockConfiguration(const AudioClockConfig& requested,
                                           DiceClockConfiguration& out) {
        return DICETcatProtocol::MakeDiceClockConfiguration(requested, out);
    }
};

} // namespace ASFW::Audio::DICE::TCAT

namespace ASFW::Audio::DICE::Focusrite {

class SPro24DspProtocolTestPeer {
public:
    static void LoadRouterStreamConfigForRate(SPro24DspProtocol& protocol,
                                               uint32_t rateHz,
                                               SPro24DspProtocol::VoidCallback callback) {
        protocol.LoadRouterStreamConfigForRate(rateHz, std::move(callback));
    }
};

} // namespace ASFW::Audio::DICE::Focusrite

namespace {

using ASFW::Async::AsyncHandle;
using ASFW::Async::AsyncStatus;
using ASFW::Async::FWAddress;
using ASFW::Async::IFireWireBus;
using ASFW::Audio::AudioStreamRuntimeCaps;
using ASFW::Audio::AudioClockConfig;
using ASFW::Audio::AudioDuplexChannels;
using ASFW::Audio::DICE::ClockSource;
using ASFW::Audio::DICE::DecodeDiceNickname;
using ASFW::Audio::DICE::SplitDiceLabels;
using ASFW::Audio::DICE::ExtensionSections;
using ASFW::Audio::DICE::Focusrite::EffectGeneralParams;
using ASFW::Audio::DICE::GeneralSections;
using ASFW::Audio::DICE::DiceClockConfiguration;
using ASFW::Audio::DICE::Focusrite::SPro24DspProtocol;
using ASFW::Audio::DICE::Focusrite::kEffectGeneralOffset;
using ASFW::Audio::DICE::TCAT::DICETcatProtocol;
using ASFW::Audio::DICE::TCAT::DICETcatRuntimePolicy;
using ASFW::FW::FwSpeed;
using ASFW::FW::Generation;
using ASFW::FW::LockOp;
using ASFW::FW::NodeId;

struct RouteState {
    ASFW::Discovery::DeviceRegistry registry;
    ASFW::Discovery::DeviceRouteToken route{};

    RouteState() {
        ASFW::Discovery::ConfigROM rom{};
        rom.bib.guid = 0xD1CE000000000002ULL;
        rom.gen = Generation{1};
        rom.nodeId = 2;
        const auto record =
            registry.UpsertFromROM(rom, ASFW::Discovery::LinkPolicy{});
        route = *registry.CurrentRoute(record.instanceId);
    }
};

constexpr uint32_t kExtensionBaseLo = static_cast<uint32_t>(
    ASFW::Audio::DICE::DICEAbsoluteAddress(ASFW::Audio::DICE::kDICEExtensionOffset) & 0xFFFFFFFFULL);
constexpr uint32_t kDiceBaseLo = static_cast<uint32_t>(
    ASFW::Audio::DICE::DICEAbsoluteAddress(0) & 0xFFFFFFFFULL);
constexpr uint32_t kGlobalBaseLo = static_cast<uint32_t>(
    ASFW::Audio::DICE::DICEAbsoluteAddress(0x28) & 0xFFFFFFFFULL);
constexpr uint32_t kTxSectionBaseLo = kDiceBaseLo + 420U;
constexpr uint32_t kRxSectionBaseLo = kDiceBaseLo + 988U;
// A product stopped hook can atomically replace the stream image. Keep its
// stream records deliberately disjoint from the probe-time image so this fake
// catches a controller that retains stale section pointers after the hook.
constexpr uint32_t kReloadedTxSectionQuadletOffset = 0x280U;
constexpr uint32_t kReloadedRxSectionQuadletOffset = 0x3D0U;
constexpr uint32_t kReloadedTxSectionBaseLo =
    kDiceBaseLo + (kReloadedTxSectionQuadletOffset * sizeof(uint32_t));
constexpr uint32_t kReloadedRxSectionBaseLo =
    kDiceBaseLo + (kReloadedRxSectionQuadletOffset * sizeof(uint32_t));
// Synthetic pointer-table layout: sections have to be physically disjoint.
// In particular, the fixed 1152-byte mixer payload cannot be followed by the
// peak section one quadlet later.
constexpr uint32_t kAppSectionQuadletOffset = 0x3A0U;
constexpr uint32_t kCapsSectionQuadletOffset = 0x13U;
constexpr uint32_t kCommandSectionQuadletOffset = 0x17U;
constexpr uint32_t kMixerSectionQuadletOffset = 0x20U;
constexpr uint32_t kPeakSectionQuadletOffset = 0x150U;
constexpr uint32_t kRouterSectionQuadletOffset = 0x1E0U;
constexpr uint32_t kAppSectionBaseLo = kExtensionBaseLo + (kAppSectionQuadletOffset * 4U);
constexpr uint32_t kCapsSectionBaseLo = kExtensionBaseLo + (kCapsSectionQuadletOffset * 4U);
constexpr uint32_t kCommandSectionBaseLo =
    kExtensionBaseLo + (kCommandSectionQuadletOffset * 4U);
constexpr uint32_t kMixerSectionBaseLo = kExtensionBaseLo + (kMixerSectionQuadletOffset * 4U);
constexpr uint32_t kPeakSectionBaseLo = kExtensionBaseLo + (kPeakSectionQuadletOffset * 4U);
constexpr uint32_t kRouterSectionBaseLo = kExtensionBaseLo + (kRouterSectionQuadletOffset * 4U);
constexpr uint32_t kCurrentConfigSectionQuadletOffset = 0x2B0U;
constexpr uint32_t kCurrentConfigSectionBaseLo =
    kExtensionBaseLo + (kCurrentConfigSectionQuadletOffset * 4U);
constexpr uint32_t kGlobalReadBytes = 104U;
constexpr uint32_t kClockSelect48kInternal =
    (ASFW::Audio::DICE::ClockRateIndex::k48000 << ASFW::Audio::DICE::ClockSelect::kRateShift) |
    static_cast<uint32_t>(ClockSource::Internal);
constexpr uint32_t kLocked48kStatus =
    ASFW::Audio::DICE::StatusBits::kSourceLocked |
    (ASFW::Audio::DICE::ClockRateIndex::k48000 << ASFW::Audio::DICE::StatusBits::kNominalRateShift);

void PutBe32(uint8_t* dst, uint32_t value) {
    ASFW::FW::WriteBE32(dst, value);
}

void PutBe64(uint8_t* dst, uint64_t value) {
    ASFW::FW::WriteBE64(dst, value);
}

std::array<uint8_t, ExtensionSections::kWireSize> MakeExtensionSectionsWire() {
    std::array<uint8_t, ExtensionSections::kWireSize> bytes{};
    const std::array<uint32_t, 18> quadlets{
        0x13, 0x04,  // caps
        0x17, 0x02,  // command
        kMixerSectionQuadletOffset, 0x121,  // mixer: header + fixed 16 x 18 records
        kPeakSectionQuadletOffset, 0x080,   // 128 peak records
        kRouterSectionQuadletOffset, 0x081, // count header + 128 router records
        0x270, 0x40,  // stream format
        0x2B0, 0x1800,  // current config: router + low/mid/high stream images
        0x330, 0x40,  // standalone
        kAppSectionQuadletOffset, 0x100,  // application
    };

    for (size_t index = 0; index < quadlets.size(); ++index) {
        PutBe32(bytes.data() + (index * sizeof(uint32_t)), quadlets[index]);
    }
    return bytes;
}

std::array<uint8_t, GeneralSections::kWireSize> MakeGeneralSectionsWire(
    bool useReloadedStreamImage = false) {
    std::array<uint8_t, GeneralSections::kWireSize> bytes{};
    PutBe32(bytes.data() + 0x00, 0x0000000A);
    PutBe32(bytes.data() + 0x04, 0x0000005F);
    PutBe32(bytes.data() + 0x08, useReloadedStreamImage
                                    ? kReloadedTxSectionQuadletOffset
                                    : 0x00000069U);
    PutBe32(bytes.data() + 0x0C, 0x00000046);
    PutBe32(bytes.data() + 0x10, useReloadedStreamImage
                                    ? kReloadedRxSectionQuadletOffset
                                    : 0x000000F7U);
    PutBe32(bytes.data() + 0x14, 0x00000046);
    return bytes;
}

std::array<uint8_t, kGlobalReadBytes> MakeGlobalStateWire(uint32_t clockSelect,
                                                          uint32_t status,
                                                          uint32_t extStatus,
                                                          uint32_t sampleRate,
                                                          uint32_t notification) {
    std::array<uint8_t, kGlobalReadBytes> bytes{};
    PutBe32(bytes.data() + ASFW::Audio::DICE::GlobalOffset::kNotification, notification);
    PutBe32(bytes.data() + ASFW::Audio::DICE::GlobalOffset::kClockSelect, clockSelect);
    PutBe32(bytes.data() + ASFW::Audio::DICE::GlobalOffset::kStatus, status);
    PutBe32(bytes.data() + ASFW::Audio::DICE::GlobalOffset::kExtStatus, extStatus);
    PutBe32(bytes.data() + ASFW::Audio::DICE::GlobalOffset::kSampleRate, sampleRate);
    PutBe32(bytes.data() + ASFW::Audio::DICE::GlobalOffset::kVersion, 0x01000C00U);
    PutBe32(bytes.data() + ASFW::Audio::DICE::GlobalOffset::kClockCaps, 0x00001E06U);
    return bytes;
}

std::vector<uint8_t> CapturedSPro24CurrentConfigRouterWire() {
    // Read-only capture from the active low-rate CURRENT_CONFIG router on a
    // Saffire Pro 24 DSP, 2026-08-24. Keep this binary fixture at the
    // protocol boundary: a staging-router response is not representative.
    constexpr std::array<uint32_t, 49> words = {
        0x00000030, 0x00004248, 0x00004349, 0x000040b2, 0x000041b3, 0x000006b4,
        0x000007b5, 0x000010b6, 0x000011b7, 0x000012b8, 0x000013b9, 0x000014ba,
        0x000015bb, 0x000016bc, 0x000017bd, 0x0000b040, 0x0000b141, 0x0000b042,
        0x0000b143, 0x0000b044, 0x0000b145, 0x00002006, 0x00002107, 0x000042be,
        0x000043bf, 0x00004820, 0x00004921, 0x00004022, 0x00004123, 0x00001024,
        0x00001125, 0x00001226, 0x00001327, 0x00001428, 0x00001529, 0x0000162a,
        0x0000172b, 0x0000062c, 0x0000072d, 0x0000b02e, 0x0000b12f, 0x00004e30,
        0x00004f31, 0x000048b0, 0x000049b1, 0x0000284e, 0x0000294f, 0x000020f0,
        0x000021f0,
    };
    std::vector<uint8_t> wire(words.size() * sizeof(uint32_t));
    for (size_t index = 0; index < words.size(); ++index) {
        PutBe32(wire.data() + index * sizeof(uint32_t), words[index]);
    }
    return wire;
}

std::vector<uint8_t> LoadedSPro24LowRateStreamWire() {
    // The Focusrite load command populates CURRENT_CONFIG with this low-rate
    // geometry. Keep enough space for the driver's bounded four-TX/four-RX
    // read; only the two reported entries are semantically present.
    constexpr size_t kReadBytes =
        ASFW::Audio::DICE::CurrentConfigStream::kEntries +
        8U * ASFW::Audio::DICE::CurrentConfigStream::kEntryStride;
    std::vector<uint8_t> wire(kReadBytes, 0);
    PutBe32(wire.data() + ASFW::Audio::DICE::CurrentConfigStream::kTxNumber, 1U);
    PutBe32(wire.data() + ASFW::Audio::DICE::CurrentConfigStream::kRxNumber, 1U);

    const size_t tx = ASFW::Audio::DICE::CurrentConfigStream::kEntries;
    PutBe32(wire.data() + tx + ASFW::Audio::DICE::CurrentConfigStream::kEntryPcmChannels, 16U);
    PutBe32(wire.data() + tx + ASFW::Audio::DICE::CurrentConfigStream::kEntryMidiPorts, 1U);

    const size_t rx = tx + ASFW::Audio::DICE::CurrentConfigStream::kEntryStride;
    PutBe32(wire.data() + rx + ASFW::Audio::DICE::CurrentConfigStream::kEntryPcmChannels, 8U);
    PutBe32(wire.data() + rx + ASFW::Audio::DICE::CurrentConfigStream::kEntryMidiPorts, 1U);
    return wire;
}

std::vector<uint8_t> StandardStreamWire(bool rx, uint32_t reportedCount) {
    const size_t sectionBytes = rx ? 1128U : 568U;
    std::vector<uint8_t> wire(sectionBytes, 0);
    PutBe32(wire.data(), reportedCount);
    PutBe32(wire.data() + 4, 70U);
    const size_t entry = 8U;
    PutBe32(wire.data() + entry, rx ? 0U : 1U);
    if (rx) {
        PutBe32(wire.data() + entry + 0x08U, 8U);
        PutBe32(wire.data() + entry + 0x0CU, 1U);
    } else {
        PutBe32(wire.data() + entry + 0x04U, 16U);
        PutBe32(wire.data() + entry + 0x08U, 1U);
        PutBe32(wire.data() + entry + 0x0CU, 2U);
    }
    return wire;
}

class CountingFireWireBus final : public IFireWireBus {
public:
    AsyncHandle ReadBlock(Generation generation,
                          NodeId nodeId,
                          FWAddress address,
                          uint32_t length,
                          FwSpeed speed,
                          ASFW::Async::InterfaceCompletionCallback callback) override {
        (void)nodeId;
        (void)speed;
        ++readCount;
        if (generation != generation_) {
            callback(AsyncStatus::kStaleGeneration, {});
            return NextHandle();
        }

        std::vector<uint8_t> payload(length, 0);
        if (address.addressHi == 0xFFFFU && address.addressLo == kDiceBaseLo &&
            length >= GeneralSections::kWireSize) {
            ++generalReadCount;
            const auto bytes = MakeGeneralSectionsWire(hookReplacedStreamImage_);
            payload.assign(bytes.begin(), bytes.end());
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kGlobalBaseLo &&
                   length == sizeof(uint64_t)) {
            PutBe64(payload.data(), owner_);
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kGlobalBaseLo &&
                   length >= kGlobalReadBytes) {
            ++globalReadCount;
            const auto bytes = MakeGlobalStateWire(clockSelect_, status_, extStatus_, sampleRate_, notification_);
            std::copy(bytes.begin(), bytes.end(), payload.begin());
            PutBe64(payload.data() + ASFW::Audio::DICE::GlobalOffset::kOwnerHi, owner_);
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo >= ActiveTxSectionBase() &&
                   static_cast<uint64_t>(address.addressLo) + length <=
                       static_cast<uint64_t>(ActiveTxSectionBase()) + 568U) {
            const auto wire = StandardStreamWire(
                false,
                (!coldStreamImageRequiresClockReselect_ || clockSelectWriteCount != 0)
                    ? 1U : coldReportedStreamCount_);
            const size_t offset = address.addressLo - ActiveTxSectionBase();
            payload.assign(wire.begin() + offset, wire.begin() + offset + length);
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo >= ActiveRxSectionBase() &&
                   static_cast<uint64_t>(address.addressLo) + length <=
                       static_cast<uint64_t>(ActiveRxSectionBase()) + 1128U) {
            const auto wire = StandardStreamWire(
                true,
                (!coldStreamImageRequiresClockReselect_ || clockSelectWriteCount != 0)
                    ? 1U : coldReportedStreamCount_);
            const size_t offset = address.addressLo - ActiveRxSectionBase();
            payload.assign(wire.begin() + offset, wire.begin() + offset + length);
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kExtensionBaseLo &&
            length >= ExtensionSections::kWireSize) {
            ++extensionReadCount;
            const auto bytes = MakeExtensionSectionsWire();
            payload.assign(bytes.begin(), bytes.end());
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kCapsSectionBaseLo &&
                   length >= ASFW::Audio::DICE::DiceExtensionCaps::kWireSize) {
            ++extensionCapsReadCount;
            payload.resize(ASFW::Audio::DICE::DiceExtensionCaps::kWireSize);
            PutBe32(payload.data(), 0x00800001U);
            PutBe32(payload.data() + 4, 0x10121115U);
            PutBe32(payload.data() + 8, 0x00001017U);
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo == kCommandSectionBaseLo && length == sizeof(uint32_t)) {
            ++commandOpcodeReadCount;
            PutBe32(payload.data(), commandOpcodeReadback_);
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo == kCommandSectionBaseLo + sizeof(uint32_t) &&
                   length == sizeof(uint32_t)) {
            ++commandReturnReadCount;
            PutBe32(payload.data(), commandReturn_);
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kRouterSectionBaseLo &&
                   length == sizeof(uint32_t)) {
            ++routerHeaderReadCount;
            PutBe32(payload.data(), 2U);
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kRouterSectionBaseLo + 4U &&
                   length == 2U * ASFW::Audio::DICE::DiceRouterEntry::kWireSize) {
            ++routerEntriesReadCount;
            PutBe32(payload.data(), 0x7F00B142U);
            PutBe32(payload.data() + 4, 0x3C00A350U);
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kCurrentConfigSectionBaseLo &&
                   length == sizeof(uint32_t)) {
            ++currentConfigRouterHeaderReadCount;
            PutBe32(payload.data(), 48U);
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo == kCurrentConfigSectionBaseLo + sizeof(uint32_t) &&
                   length == 48U * ASFW::Audio::DICE::DiceRouterEntry::kWireSize) {
            ++currentConfigRouterEntriesReadCount;
            auto wire = CapturedSPro24CurrentConfigRouterWire();
            payload.assign(wire.begin() + sizeof(uint32_t), wire.end());
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo >=
                       kCurrentConfigSectionBaseLo +
                           ASFW::Audio::DICE::CurrentConfigStreamBlockOffset(
                               ASFW::Audio::DICE::DiceRateMode::Low) &&
                   static_cast<uint64_t>(address.addressLo) + length <=
                       static_cast<uint64_t>(kCurrentConfigSectionBaseLo) +
                           ASFW::Audio::DICE::CurrentConfigStreamBlockOffset(
                               ASFW::Audio::DICE::DiceRateMode::Low) +
                           LoadedSPro24LowRateStreamWire().size()) {
            ++currentConfigStreamReadCount;
            const uint32_t streamBase =
                kCurrentConfigSectionBaseLo +
                ASFW::Audio::DICE::CurrentConfigStreamBlockOffset(
                    ASFW::Audio::DICE::DiceRateMode::Low);
            if (!coldStreamImageRequiresClockReselect_ || clockSelectWriteCount != 0) {
                const auto wire = LoadedSPro24LowRateStreamWire();
                const size_t offset = address.addressLo - streamBase;
                payload.assign(wire.begin() + offset, wire.begin() + offset + length);
            }
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo >= kMixerSectionBaseLo + 4U &&
                   address.addressLo < kMixerSectionBaseLo + 4U +
                       ASFW::Audio::DICE::kDiceMixerCoefficientWireBytes) {
            ++mixerReadCount;
            const uint32_t firstCoefficient =
                (address.addressLo - (kMixerSectionBaseLo + 4U)) / sizeof(uint32_t);
            for (uint32_t index = 0; index < length / sizeof(uint32_t); ++index) {
                const uint32_t coefficientIndex = firstCoefficient + index;
                const uint16_t coefficient = mixerCoefficients_[coefficientIndex].value_or(
                    static_cast<uint16_t>(coefficientIndex + 1U));
                PutBe32(payload.data() + (index * sizeof(uint32_t)), coefficient);
            }
        } else if (address.addressHi == 0xFFFFU && address.addressLo == kPeakSectionBaseLo &&
                   length == 128U * ASFW::Audio::DICE::DiceRouterEntry::kWireSize) {
            ++peakReadCount;
            PutBe32(payload.data(), 0x2100B142U);
            PutBe32(payload.data() + 4, 0x2200B143U);
        } else if (address.addressHi == 0xFFFFU &&
                   address.addressLo >= kAppSectionBaseLo &&
                   static_cast<uint64_t>(address.addressLo) + length <=
                       static_cast<uint64_t>(kAppSectionBaseLo) + application_.size()) {
            if (address.addressLo == kAppSectionBaseLo + kEffectGeneralOffset &&
                length >= sizeof(uint32_t)) {
                ++appQuadReadCount;
            }
            const size_t offset = address.addressLo - kAppSectionBaseLo;
            std::copy_n(application_.begin() + offset, length, payload.begin());
        }

        callback(AsyncStatus::kSuccess, std::span<const uint8_t>(payload.data(), payload.size()));
        return NextHandle();
    }

    AsyncHandle WriteBlock(Generation generation,
                           NodeId nodeId,
                           FWAddress address,
                           std::span<const uint8_t> data,
                           FwSpeed speed,
                           ASFW::Async::InterfaceCompletionCallback callback) override {
        (void)generation;
        (void)nodeId;
        (void)speed;
        ++writeCount;
        if (address.addressHi == 0xFFFFU) {
            writeAddresses.push_back(address.addressLo);
        }
        if (address.addressHi == 0xFFFFU &&
            address.addressLo >= kMixerSectionBaseLo + sizeof(uint32_t) &&
            address.addressLo < kMixerSectionBaseLo + sizeof(uint32_t) +
                ASFW::Audio::DICE::kDiceMixerCoefficientWireBytes &&
            data.size() == sizeof(uint32_t)) {
            mixerCoefficientWrites.push_back({
                .address = address.addressLo,
                .value = ASFW::FW::ReadBE32(data.data()),
            });
            const uint32_t coefficientIndex =
                (address.addressLo - (kMixerSectionBaseLo + sizeof(uint32_t))) /
                sizeof(uint32_t);
            mixerCoefficients_[coefficientIndex] = static_cast<uint16_t>(
                ASFW::FW::ReadBE32(data.data()) & 0xffffU);
        }
        if (address.addressHi == 0xFFFFU &&
            address.addressLo >= kAppSectionBaseLo &&
            static_cast<uint64_t>(address.addressLo) + data.size() <=
                static_cast<uint64_t>(kAppSectionBaseLo) + application_.size()) {
            const size_t offset = address.addressLo - kAppSectionBaseLo;
            std::copy(data.begin(), data.end(), application_.begin() + offset);
        }
        if (address.addressHi == 0xFFFFU &&
            address.addressLo ==
                kGlobalBaseLo + ASFW::Audio::DICE::GlobalOffset::kClockSelect &&
            data.size() == sizeof(uint32_t)) {
            ++clockSelectWriteCount;
            clockSelect_ = ASFW::FW::ReadBE32(data.data());
            // The generic cold-geometry path now correctly requires the
            // device's async CLOCK_ACCEPTED acknowledgement. Model the event
            // at the protocol boundary rather than letting a locked status
            // impersonate it.
            ASFW::Audio::DICE::NotificationMailbox::Publish(
                ASFW::Audio::DICE::Notify::kClockAccepted);
        }
        if (address.addressHi == 0xFFFFU && address.addressLo == kCommandSectionBaseLo &&
            data.size() == sizeof(uint32_t)) {
            ++commandWriteCount;
            commandOpcodeWritten_ = ASFW::FW::ReadBE32(data.data());
            // The synchronous host fake models a command which has completed
            // by the first poll: hardware clears only the execute bit.
            commandOpcodeReadback_ =
                commandOpcodeWritten_ & ~ASFW::Audio::DICE::ExtensionCommandOpcode::kExecute;
            // The real stopped-state command asynchronously writes this to
            // the host notification address before its return word is read.
            // Model that protocol edge here; a completed command alone is
            // deliberately insufficient to unlock generic stream writes.
            if (emitCommandConfigNotice_) {
                ASFW::Audio::DICE::NotificationMailbox::Publish(
                    ASFW::Audio::DICE::Notify::kRxConfigChange |
                    ASFW::Audio::DICE::Notify::kTxConfigChange);
            }
        }
        callback(AsyncStatus::kSuccess, {});
        return NextHandle();
    }

    AsyncHandle Lock(Generation generation,
                     NodeId nodeId,
                     FWAddress address,
                     LockOp lockOp,
                     std::span<const uint8_t> operand,
                     uint32_t responseLength,
                     FwSpeed speed,
                     ASFW::Async::InterfaceCompletionCallback callback) override {
        (void)generation;
        (void)nodeId;
        (void)lockOp;
        (void)speed;
        ++lockCount;
        std::vector<uint8_t> payload(responseLength, 0);
        if (address.addressHi == 0xFFFFU && address.addressLo == kGlobalBaseLo &&
            operand.size() == 16U && responseLength == 8U) {
            const uint64_t expected = ASFW::FW::ReadBE64(operand.data());
            const uint64_t desired = ASFW::FW::ReadBE64(operand.data() + 8U);
            const uint64_t previous = owner_;
            if (owner_ == expected) {
                owner_ = desired;
            }
            PutBe64(payload.data(), previous);
        }
        callback(AsyncStatus::kSuccess, std::span<const uint8_t>(payload.data(), payload.size()));
        return NextHandle();
    }

    bool Cancel(AsyncHandle handle) override {
        (void)handle;
        return false;
    }

    FwSpeed GetSpeed(NodeId nodeId) const override {
        (void)nodeId;
        return FwSpeed::S400;
    }

    uint32_t HopCount(NodeId nodeA, NodeId nodeB) const override {
        (void)nodeA;
        (void)nodeB;
        return 1;
    }

    Generation GetGeneration() const override { return generation_; }
    NodeId GetLocalNodeID() const override { return localNodeId_; }
    uint64_t Owner() const noexcept { return owner_; }

    int readCount{0};
    int writeCount{0};
    int lockCount{0};
    int generalReadCount{0};
    int globalReadCount{0};
    int extensionReadCount{0};
    int extensionCapsReadCount{0};
    int routerHeaderReadCount{0};
    int routerEntriesReadCount{0};
    int currentConfigRouterHeaderReadCount{0};
    int currentConfigRouterEntriesReadCount{0};
    int currentConfigStreamReadCount{0};
    int mixerReadCount{0};
    int peakReadCount{0};
    int appQuadReadCount{0};
    int commandWriteCount{0};
    int commandOpcodeReadCount{0};
    int commandReturnReadCount{0};
    int clockSelectWriteCount{0};
    uint32_t commandOpcodeWritten_{0};
    uint32_t commandOpcodeReadback_{0};
    uint32_t commandReturn_{0};
    uint32_t clockSelect_{kClockSelect48kInternal};
    uint32_t status_{kLocked48kStatus};
    uint32_t extStatus_{0};
    uint32_t sampleRate_{48000};
    uint32_t notification_{0x20};
    struct MixerCoefficientWrite final {
        uint32_t address{0};
        uint32_t value{0};
    };
    std::vector<MixerCoefficientWrite> mixerCoefficientWrites;
    std::array<uint8_t, 0x600> application_{};
    bool coldStreamImageRequiresClockReselect_{false};
    uint32_t coldReportedStreamCount_{0};
    bool hookReplacedStreamImage_{false};
    bool emitCommandConfigNotice_{true};
    std::vector<uint32_t> writeAddresses;

private:
    [[nodiscard]] uint32_t ActiveTxSectionBase() const noexcept {
        return hookReplacedStreamImage_ ? kReloadedTxSectionBaseLo : kTxSectionBaseLo;
    }

    [[nodiscard]] uint32_t ActiveRxSectionBase() const noexcept {
        return hookReplacedStreamImage_ ? kReloadedRxSectionBaseLo : kRxSectionBaseLo;
    }

    AsyncHandle NextHandle() {
        return AsyncHandle{static_cast<uint32_t>(nextHandle_++)};
    }

    Generation generation_{1};
    NodeId localNodeId_{0};
    uint64_t nextHandle_{1};
    uint64_t owner_{ASFW::Audio::DICE::kOwnerNoOwner};
    std::array<std::optional<uint16_t>, 16U * 18U> mixerCoefficients_{};
};

TEST(DICETcatProtocolTests, InitializeIsSideEffectFree) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);

    EXPECT_EQ(protocol.Initialize(), kIOReturnSuccess);

    AudioStreamRuntimeCaps caps{};
    EXPECT_FALSE(protocol.GetRuntimeAudioStreamCaps(caps));
    EXPECT_EQ(bus.readCount, 0);
    EXPECT_EQ(bus.writeCount, 0);
    EXPECT_EQ(bus.lockCount, 0);
}

TEST(DICETcatProtocolTests, NeutralClockRequestMapsToDiceClockSelectInsideAdapter) {
    DiceClockConfiguration mapped{};
    EXPECT_TRUE(ASFW::Audio::DICE::TCAT::DICETcatProtocolTestPeer::MakeDiceClockConfiguration(
        AudioClockConfig{.sampleRateHz = 48000U}, mapped));
    EXPECT_EQ(mapped.sampleRateHz, 48000U);
    EXPECT_EQ(mapped.clockSelect, kClockSelect48kInternal);

    // Validated 1x rates map to their CLOCK_SELECT encoding (rate index << 8 |
    // internal source).
    EXPECT_TRUE(ASFW::Audio::DICE::TCAT::DICETcatProtocolTestPeer::MakeDiceClockConfiguration(
        AudioClockConfig{.sampleRateHz = 44100U}, mapped));
    EXPECT_EQ(mapped.sampleRateHz, 44100U);
    EXPECT_EQ(mapped.clockSelect,
              (ASFW::Audio::DICE::ClockRateIndex::k44100
               << ASFW::Audio::DICE::ClockSelect::kRateShift) |
                  static_cast<uint32_t>(ClockSource::Internal));

    // 2x/4x rates stay rejected until the stream geometry is HW-validated.
    EXPECT_FALSE(ASFW::Audio::DICE::TCAT::DICETcatProtocolTestPeer::MakeDiceClockConfiguration(
        AudioClockConfig{.sampleRateHz = 96000U}, mapped));
}

TEST(DICETcatProtocolTests, RuntimeCapsAggregateTotalConfiguredStreams) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);

    ASFW::Audio::DICE::GlobalState global{};
    global.sampleRate = 48000;

    ASFW::Audio::DICE::StreamConfig tx{};
    tx.numStreams = 2;
    tx.streams[0].isoChannel = 1;
    tx.streams[0].pcmChannels = 10;
    tx.streams[0].midiPorts = 1;
    tx.streams[1].isoChannel = -1;
    tx.streams[1].pcmChannels = 6;

    ASFW::Audio::DICE::StreamConfig rx{};
    rx.numStreams = 2;
    rx.streams[0].isoChannel = 0;
    rx.streams[0].pcmChannels = 8;
    rx.streams[0].midiPorts = 1;
    rx.streams[1].isoChannel = -1;
    rx.streams[1].pcmChannels = 4;

    ASFW::Audio::DICE::TCAT::DICETcatProtocolTestPeer::CacheRuntimeCaps(protocol, global, tx, rx);

    AudioStreamRuntimeCaps caps{};
    ASSERT_TRUE(protocol.GetRuntimeAudioStreamCaps(caps));
    EXPECT_EQ(caps.sampleRateHz, 48000U);
    EXPECT_EQ(caps.hostInputPcmChannels, 16U);
    EXPECT_EQ(caps.deviceToHostAm824Slots, 17U);
    EXPECT_EQ(caps.hostOutputPcmChannels, 12U);
    EXPECT_EQ(caps.hostToDeviceAm824Slots, 13U);
    EXPECT_EQ(caps.deviceToHostIsoChannel, 1U);
    EXPECT_EQ(caps.hostToDeviceIsoChannel, 0U);
}

TEST(DICETcatProtocolTests, RuntimePolicyHidesCoreAudioInputWithoutChangingWireGeometry) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus,
                              bus,
                              routeState.registry,
                              routeState.route,
                              nullptr,
                              nullptr,
                              DICETcatRuntimePolicy{.exposeDeviceToHostToCoreAudio = false});

    ASFW::Audio::DICE::GlobalState global{};
    global.sampleRate = 48000;
    ASFW::Audio::DICE::StreamConfig tx{};
    tx.numStreams = 1;
    tx.streams[0].isoChannel = 0;
    tx.streams[0].pcmChannels = 2;
    strlcpy(tx.streams[0].labels, "Return L\\Return R\\\\", sizeof(tx.streams[0].labels));
    ASFW::Audio::DICE::StreamConfig rx{};
    rx.numStreams = 1;
    rx.streams[0].isoChannel = 1;
    rx.streams[0].pcmChannels = 2;
    strlcpy(rx.streams[0].labels, "Out L\\Out R\\\\", sizeof(rx.streams[0].labels));

    ASFW::Audio::DICE::TCAT::DICETcatProtocolTestPeer::CacheRuntimeCaps(protocol, global, tx, rx);

    AudioStreamRuntimeCaps caps{};
    ASSERT_TRUE(protocol.GetRuntimeAudioStreamCaps(caps));
    EXPECT_EQ(caps.hostInputPcmChannels, 0U);
    EXPECT_EQ(caps.hostOutputPcmChannels, 2U);
    EXPECT_EQ(caps.deviceToHostAm824Slots, 2U);
    EXPECT_EQ(caps.hostToDeviceAm824Slots, 2U);
    ASSERT_EQ(caps.deviceToHostStreamCount, 1U);
    ASSERT_EQ(caps.hostToDeviceStreamCount, 1U);
    EXPECT_EQ(caps.deviceToHostStreams[0].pcmChannels, 2U);
    EXPECT_EQ(caps.hostToDeviceStreams[0].pcmChannels, 2U);

    std::vector<std::string> inNames;
    std::vector<std::string> outNames;
    ASSERT_TRUE(protocol.GetChannelLabels(inNames, outNames));
    EXPECT_TRUE(inNames.empty());
    ASSERT_EQ(outNames.size(), 2U);
    EXPECT_EQ(outNames[0], "Out L");
}

TEST(DICETcatProtocolTests, ChannelLabelsFlattenAcrossStreamsInChannelOrder) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);

    // No labels before caps are cached.
    std::vector<std::string> inNames;
    std::vector<std::string> outNames;
    EXPECT_FALSE(protocol.GetChannelLabels(inNames, outNames));

    ASFW::Audio::DICE::GlobalState global{};
    global.sampleRate = 48000;

    // Host input == device TX; two streams, names concatenated in stream order.
    ASFW::Audio::DICE::StreamConfig tx{};
    tx.numStreams = 2;
    strlcpy(tx.streams[0].labels, "Mic 1\\Mic 2\\\\", sizeof(tx.streams[0].labels));
    strlcpy(tx.streams[1].labels, "Line 3\\Line 4\\\\", sizeof(tx.streams[1].labels));

    // Host output == device RX.
    ASFW::Audio::DICE::StreamConfig rx{};
    rx.numStreams = 1;
    strlcpy(rx.streams[0].labels, "Main L\\Main R\\\\", sizeof(rx.streams[0].labels));

    ASFW::Audio::DICE::TCAT::DICETcatProtocolTestPeer::CacheRuntimeCaps(protocol, global, tx, rx);

    ASSERT_TRUE(protocol.GetChannelLabels(inNames, outNames));
    ASSERT_EQ(inNames.size(), 4u);
    EXPECT_EQ(inNames[0], "Mic 1");
    EXPECT_EQ(inNames[1], "Mic 2");
    EXPECT_EQ(inNames[2], "Line 3");
    EXPECT_EQ(inNames[3], "Line 4");
    ASSERT_EQ(outNames.size(), 2u);
    EXPECT_EQ(outNames[0], "Main L");
    EXPECT_EQ(outNames[1], "Main R");
}

TEST(DICETcatProtocolTests, ReadDuplexHealthReturnsCurrentGlobalLockState) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    ASFW::Audio::DICE::GlobalState global{};
    global.sampleRate = 48000;

    ASFW::Audio::DICE::StreamConfig tx{};
    tx.numStreams = 1;
    tx.streams[0].isoChannel = 1;
    tx.streams[0].pcmChannels = 16;

    ASFW::Audio::DICE::StreamConfig rx{};
    rx.numStreams = 1;
    rx.streams[0].isoChannel = 0;
    rx.streams[0].pcmChannels = 8;

    ASFW::Audio::DICE::TCAT::DICETcatProtocolTestPeer::CacheRuntimeCaps(protocol, global, tx, rx);

    bus.notification_ = ASFW::Audio::DICE::Notify::kLockChange;
    bus.status_ = kLocked48kStatus;
    bus.extStatus_ = 0x40U;

    std::optional<ASFW::Audio::DICE::DiceDuplexHealthResult> health;
    IOReturn healthStatus = kIOReturnError;
    protocol.ReadDuplexHealth([&](IOReturn status, ASFW::Audio::DICE::DiceDuplexHealthResult result) {
        healthStatus = status;
        health = result;
    });

    ASSERT_EQ(healthStatus, kIOReturnSuccess);
    ASSERT_TRUE(health.has_value());
    EXPECT_EQ(health->appliedClock.sampleRateHz, 48000U);
    EXPECT_TRUE(health->sourceLocked);
    EXPECT_EQ(health->nominalRateHz, 48000U);
    EXPECT_EQ(health->notification, ASFW::Audio::DICE::Notify::kLockChange);
    EXPECT_EQ(health->status, kLocked48kStatus);
    EXPECT_EQ(health->extStatus, 0x40U);
    EXPECT_EQ(health->runtimeCaps.sampleRateHz, 48000U);
    EXPECT_EQ(health->runtimeCaps.hostInputPcmChannels, 16U);
    EXPECT_EQ(health->runtimeCaps.hostOutputPcmChannels, 8U);
    EXPECT_EQ(bus.generalReadCount, 1);
    EXPECT_EQ(bus.globalReadCount, 1);
}

TEST(SPro24DspProtocolTests, InitializationPrimesSemanticMatrixAndControlReadback) {
    CountingFireWireBus bus;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);
    EXPECT_EQ(bus.extensionReadCount, 1);
    EXPECT_EQ(bus.extensionCapsReadCount, 1);
    EXPECT_EQ(bus.routerHeaderReadCount, 0);
    EXPECT_EQ(bus.routerEntriesReadCount, 0);
    EXPECT_EQ(bus.currentConfigRouterHeaderReadCount, 1);
    EXPECT_EQ(bus.currentConfigRouterEntriesReadCount, 1);
    EXPECT_EQ(bus.mixerReadCount, 3);
    // Channel-strip flags are one application quadlet read. The InSitu mode
    // read is served by the fake's dedicated DICE path; remaining control
    // state is block-read.
    EXPECT_EQ(bus.appQuadReadCount, 1);

    ASFW::Audio::AudioSemanticMatrixSnapshot matrix{};
    ASSERT_TRUE(protocol.CopyAudioSemanticMatrix(matrix));
    EXPECT_EQ(matrix.inputCount, 18);
    // The active router consumes only raw mixer rows 0/1 (monitor) and 8/9
    // (reverb send). Computed-but-unrouted rows do not become fake buses.
    ASSERT_EQ(matrix.outputCount, 4);
    EXPECT_EQ(matrix.Coefficient(0, 0), 1);
    EXPECT_EQ(matrix.Coefficient(2, 0), 145); // raw row 8, fixed 18-cell stride.
    EXPECT_EQ(matrix.outputs[0].outputRole,
              ASFW::Audio::AudioSemanticMatrixOutputRole::MonitorMix);
    EXPECT_EQ(matrix.outputs[2].outputRole,
              ASFW::Audio::AudioSemanticMatrixOutputRole::EffectSend);
    EXPECT_EQ(matrix.inputs[0].signalKind, ASFW::Audio::AudioSemanticSignalKind::Auxiliary);
    EXPECT_EQ(matrix.inputs[14].signalKind, ASFW::Audio::AudioSemanticSignalKind::HostStream);

    ASFW::Audio::AudioControlSurfaceSnapshot controls{};
    ASSERT_TRUE(protocol.CopyAudioControlSurfaceSnapshot(controls));
    EXPECT_EQ(controls.kind, ASFW::Audio::AudioControlSurfaceKind::FocusriteSPro24Dsp);
    EXPECT_EQ(controls.valueCount, 29U);
    for (uint32_t pair = 0; pair < 3; ++pair) {
        const auto routeIt = std::find_if(controls.values.begin(),
                                          controls.values.begin() + controls.valueCount,
                                          [pair](const auto& control) {
                                              return control.id == 0x5350'0130U + pair;
                                          });
        ASSERT_NE(routeIt, controls.values.begin() + controls.valueCount);
        EXPECT_EQ(routeIt->value, 1); // Active physical pair <- DAW 1/2.
    }

    std::optional<IOReturn> callbackStatus;
    protocol.GetEffectParams([&](IOReturn status, EffectGeneralParams /*params*/) {
        callbackStatus = status;
    });

    ASSERT_TRUE(callbackStatus.has_value());
    EXPECT_EQ(*callbackStatus, kIOReturnSuccess);
    EXPECT_EQ(bus.extensionReadCount, 1);
    EXPECT_EQ(bus.appQuadReadCount, 2);
}

TEST(SPro24DspProtocolTests, InputAndOutputControlsRoundTripThroughVendorBlocks) {
    CountingFireWireBus bus;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    std::optional<IOReturn> status;
    const size_t inputWriteStart = bus.writeAddresses.size();
    protocol.ApplyAudioControlValue(0x5350'0002U, 1, [&](IOReturn result) { status = result; });
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, kIOReturnSuccess);
    EXPECT_EQ(bus.application_[ASFW::Audio::DICE::Focusrite::kInputOffset + 1U], 0x02U);
    EXPECT_EQ(bus.writeAddresses[inputWriteStart],
              kAppSectionBaseLo + ASFW::Audio::DICE::Focusrite::kInputOffset);
    EXPECT_EQ(bus.writeAddresses[inputWriteStart + 1U],
              kAppSectionBaseLo + ASFW::Audio::DICE::Focusrite::kSwNoticeOffset);

    ASFW::Audio::AudioControlSurfaceSnapshot controls{};
    ASSERT_TRUE(protocol.CopyAudioControlSurfaceSnapshot(controls));
    const auto inputIt = std::find_if(controls.values.begin(),
                                      controls.values.begin() + controls.valueCount,
                                      [](const auto& control) { return control.id == 0x5350'0002U; });
    ASSERT_NE(inputIt, controls.values.begin() + controls.valueCount);
    EXPECT_EQ(inputIt->value, 1);

    status.reset();
    const size_t outputWriteStart = bus.writeAddresses.size();
    protocol.ApplyAudioControlValue(0x5350'0120U, 1, [&](IOReturn result) { status = result; });
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, kIOReturnSuccess);
    EXPECT_EQ(ASFW::FW::ReadBE32(bus.application_.data() +
                                 ASFW::Audio::DICE::Focusrite::kOutputGroupOffset), 1U);
    EXPECT_EQ(bus.writeAddresses[outputWriteStart],
              kAppSectionBaseLo + ASFW::Audio::DICE::Focusrite::kOutputGroupOffset);
    EXPECT_EQ(bus.writeAddresses[outputWriteStart + 1U],
              kAppSectionBaseLo + ASFW::Audio::DICE::Focusrite::kSwNoticeOffset);

    ASSERT_TRUE(protocol.CopyAudioControlSurfaceSnapshot(controls));
    const auto muteIt = std::find_if(controls.values.begin(),
                                     controls.values.begin() + controls.valueCount,
                                     [](const auto& control) { return control.id == 0x5350'0120U; });
    ASSERT_NE(muteIt, controls.values.begin() + controls.valueCount);
    EXPECT_EQ(muteIt->value, 1);

    status.reset();
    const size_t laneWriteStart = bus.writeAddresses.size();
    protocol.ApplyAudioControlValue(0x5350'0100U, 25,
                                    [&](IOReturn result) { status = result; });
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, kIOReturnSuccess);
    EXPECT_EQ(ASFW::FW::ReadBE32(bus.application_.data() +
                                 ASFW::Audio::DICE::Focusrite::kOutputGroupOffset + 0x08U),
              102U);
    EXPECT_EQ(bus.writeAddresses[laneWriteStart],
              kAppSectionBaseLo + ASFW::Audio::DICE::Focusrite::kOutputGroupOffset);
    EXPECT_EQ(bus.writeAddresses[laneWriteStart + 1U],
              kAppSectionBaseLo + ASFW::Audio::DICE::Focusrite::kSwNoticeOffset);

    const int writesBeforeInvalid = bus.writeCount;
    status.reset();
    protocol.ApplyAudioControlValue(0x5350'0100U, 128,
                                    [&](IOReturn result) { status = result; });
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, kIOReturnBadArgument);
    EXPECT_EQ(bus.writeCount, writesBeforeInvalid);
}

void ExpectColdCountRefinement(uint32_t coldCount) {
    SCOPED_TRACE(testing::Message() << "cold stream count=" << coldCount);
    CountingFireWireBus bus;
    bus.coldStreamImageRequiresClockReselect_ = true;
    bus.coldReportedStreamCount_ = coldCount;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    std::optional<IOReturn> geometryStatus;
    protocol.AsDuplexDeviceControl()->EnsureRuntimeStreamGeometry(
        [&](IOReturn status) { geometryStatus = status; });

    ASSERT_TRUE(geometryStatus.has_value());
    EXPECT_EQ(*geometryStatus, kIOReturnSuccess);
    EXPECT_EQ(bus.clockSelectWriteCount, 1);
    EXPECT_EQ(bus.commandWriteCount, 0);
    EXPECT_EQ(bus.Owner(), ASFW::Audio::DICE::kOwnerNoOwner);

    AudioStreamRuntimeCaps caps{};
    ASSERT_TRUE(protocol.GetRuntimeAudioStreamCaps(caps));
    EXPECT_EQ(caps.sampleRateHz, 48000U);
    EXPECT_EQ(caps.hostInputPcmChannels, 16U);
    EXPECT_EQ(caps.deviceToHostAm824Slots, 17U);
    EXPECT_EQ(caps.hostOutputPcmChannels, 8U);
    EXPECT_EQ(caps.hostToDeviceAm824Slots, 9U);
}

TEST(DICETcatProtocolTests, ColdZeroCountsClaimOwnerReselectClockAndPublishGeometry) {
    ExpectColdCountRefinement(0U);
}

TEST(DICETcatProtocolTests, ColdMinusOneCountsClaimOwnerReselectClockAndPublishGeometry) {
    ExpectColdCountRefinement(UINT32_MAX);
}

TEST(SPro24DspProtocolTests, StoppedPrepareCommandLoadsLowRateRouterAndStreamImage) {
    CountingFireWireBus bus;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    std::optional<IOReturn> callbackStatus;
    ASFW::Audio::DICE::Focusrite::SPro24DspProtocolTestPeer::
        LoadRouterStreamConfigForRate(
            protocol, 48000,
            [&](IOReturn status) { callbackStatus = status; });

    ASSERT_TRUE(callbackStatus.has_value());
    EXPECT_EQ(*callbackStatus, kIOReturnSuccess);
    EXPECT_EQ(bus.commandWriteCount, 1);
    EXPECT_EQ(bus.commandOpcodeWritten_,
              ASFW::Audio::DICE::ExtensionCommandOpcode::kExecute |
                  ASFW::Audio::DICE::ExtensionCommandOpcode::kRateLow |
                  ASFW::Audio::DICE::ExtensionCommandOpcode::kLoadRouterStreamConfig);
    EXPECT_EQ(bus.commandOpcodeReadCount, 1);
    EXPECT_EQ(bus.commandReturnReadCount, 1);
}

TEST(SPro24DspProtocolTests, SemanticMatrixRejectsScalarCrosspointWritesWithoutBusSemantics) {
    CountingFireWireBus bus;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    ASFW::Audio::AudioSemanticMatrixSnapshot before{};
    ASSERT_TRUE(protocol.CopyAudioSemanticMatrix(before));
    ASSERT_GT(before.inputCount, 5U);
    ASSERT_GT(before.outputCount, 0U);
    const uint32_t inputPort = before.inputs[5].portId;
    const uint32_t outputPort = before.outputs[0].portId;
    const size_t writesBefore = bus.mixerCoefficientWrites.size();

    std::optional<IOReturn> completion;
    protocol.ApplyAudioSemanticMatrixCrosspoint(outputPort, inputPort, 0x4000,
                                                [&](IOReturn status) { completion = status; });
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(*completion, kIOReturnUnsupported);
    ASSERT_EQ(bus.mixerCoefficientWrites.size(), writesBefore);

    const size_t writesBeforeBadPort = bus.mixerCoefficientWrites.size();
    completion.reset();
    protocol.ApplyAudioSemanticMatrixCrosspoint(outputPort, 0xBAD0'0000U, 0x4000,
                                                [&](IOReturn status) { completion = status; });
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(*completion, kIOReturnUnsupported);
    EXPECT_EQ(bus.mixerCoefficientWrites.size(), writesBeforeBadPort);

    completion.reset();
    protocol.ApplyAudioSemanticMatrixCrosspoint(0x5353'0010U, inputPort, 0x4000,
                                                [&](IOReturn status) { completion = status; });
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(*completion, kIOReturnUnsupported);
    EXPECT_EQ(bus.mixerCoefficientWrites.size(), writesBeforeBadPort);
}

TEST(SPro24DspProtocolTests, SemanticMatrixStereoStripWritesItsTwoVerifiedCellsAndReadbacks) {
    CountingFireWireBus bus;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    ASFW::Audio::AudioSemanticMatrixSnapshot before{};
    ASSERT_TRUE(protocol.CopyAudioSemanticMatrix(before));
    ASSERT_GT(before.inputCount, 15U);
    const auto outputGroup = before.outputs[0].presentationGroupId;
    const auto inputGroup = before.inputs[14].presentationGroupId; // active DAW L/R pair.
    ASSERT_EQ(before.inputs[14].channelRole, ASFW::Audio::AudioSemanticMatrixChannelRole::Left);
    ASSERT_EQ(before.inputs[15].channelRole, ASFW::Audio::AudioSemanticMatrixChannelRole::Right);
    const size_t writesBefore = bus.mixerCoefficientWrites.size();

    std::optional<IOReturn> completion;
    protocol.ApplyAudioSemanticMatrixStereoStrip(
        {.outputPresentationGroupId = outputGroup, .inputPresentationGroupId = inputGroup,
         .levelMilliDb = 0, .balanceMilli = -1000},
        [&](IOReturn status) { completion = status; });

    ASSERT_TRUE(completion.has_value());
    ASSERT_EQ(*completion, kIOReturnSuccess);
    ASSERT_EQ(bus.mixerCoefficientWrites.size(), writesBefore + 2U);
    EXPECT_EQ(bus.mixerCoefficientWrites[writesBefore].value, 0x4000U);
    EXPECT_EQ(bus.mixerCoefficientWrites[writesBefore + 1U].value, 0U);

    ASFW::Audio::AudioSemanticMatrixSnapshot after{};
    ASSERT_TRUE(protocol.CopyAudioSemanticMatrix(after));
    EXPECT_EQ(after.Coefficient(0, 14), 0x4000U);
    EXPECT_EQ(after.Coefficient(1, 15), 0U);
}

TEST(SPro24DspProtocolTests, SemanticMatrixMonoStripWritesOneInputToBothBusRows) {
    CountingFireWireBus bus;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    ASFW::Audio::AudioSemanticMatrixSnapshot before{};
    ASSERT_TRUE(protocol.CopyAudioSemanticMatrix(before));
    ASSERT_GT(before.inputCount, 4U);
    const auto outputGroup = before.outputs[0].presentationGroupId;
    const auto inputGroup = before.inputs[4].presentationGroupId; // active ADAT 1 mono row.
    ASSERT_EQ(before.inputs[4].channelRole, ASFW::Audio::AudioSemanticMatrixChannelRole::Mono);
    const size_t writesBefore = bus.mixerCoefficientWrites.size();

    std::optional<IOReturn> completion;
    protocol.ApplyAudioSemanticMatrixStereoStrip(
        {.outputPresentationGroupId = outputGroup, .inputPresentationGroupId = inputGroup,
         .levelMilliDb = 0, .balanceMilli = 0},
        [&](IOReturn status) { completion = status; });

    ASSERT_TRUE(completion.has_value());
    ASSERT_EQ(*completion, kIOReturnSuccess);
    ASSERT_EQ(bus.mixerCoefficientWrites.size(), writesBefore + 2U);
    EXPECT_NEAR(bus.mixerCoefficientWrites[writesBefore].value, 11585U, 1U);
    EXPECT_NEAR(bus.mixerCoefficientWrites[writesBefore + 1U].value, 11585U, 1U);

    ASFW::Audio::AudioSemanticMatrixSnapshot after{};
    ASSERT_TRUE(protocol.CopyAudioSemanticMatrix(after));
    EXPECT_NEAR(after.Coefficient(0, 4), 11585U, 1U);
    EXPECT_NEAR(after.Coefficient(1, 4), 11585U, 1U);
}

TEST(SPro24DspProtocolTests, StoppedPrepareCommandFailsClosedWithoutConfigNotification) {
    CountingFireWireBus bus;
    bus.emitCommandConfigNotice_ = false;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    std::optional<IOReturn> callbackStatus;
    ASFW::Audio::DICE::Focusrite::SPro24DspProtocolTestPeer::
        LoadRouterStreamConfigForRate(
            protocol, 48000,
            [&](IOReturn status) { callbackStatus = status; });

    ASSERT_TRUE(callbackStatus.has_value());
    EXPECT_EQ(*callbackStatus, kIOReturnNotReady);
}

TEST(SPro24DspProtocolTests, PrepareSkipsStoppedExtensionLoadWhenGenericImageIsUsable) {
    CountingFireWireBus bus;
    RouteState routeState;
    SPro24DspProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    const AudioDuplexChannels channels{
        .deviceToHostIsoChannel = 1U,
        .hostToDeviceIsoChannel = 0U,
    };
    std::optional<IOReturn> prepareStatus;
    ASSERT_NE(protocol.AsDuplexDeviceControl(), nullptr);
    protocol.AsDuplexDeviceControl()->PrepareDuplex(
        channels, AudioClockConfig{.sampleRateHz = 48000U},
        [&prepareStatus](IOReturn status, auto) { prepareStatus = status; });

    ASSERT_TRUE(prepareStatus.has_value());
    EXPECT_EQ(*prepareStatus, kIOReturnSuccess);
    EXPECT_EQ(bus.commandWriteCount, 0);
}

TEST(DICETcatProtocolTests, StoppedPrepareHookRefreshesReplacedStreamPointersBeforeProgramRx) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);
    ASSERT_EQ(protocol.Initialize(), kIOReturnSuccess);

    protocol.SetStoppedPrepareHook(
        [&bus](const AudioClockConfig&, DICETcatProtocol::VoidCallback callback) {
            // Model the Saffire's successful load-router-stream-image command:
            // it replaces the normal DICE stream sections while the engine is
            // stopped, not merely their coefficients.
            bus.hookReplacedStreamImage_ = true;
            callback(kIOReturnSuccess);
        });

    const AudioDuplexChannels channels{
        .deviceToHostIsoChannel = 1U,
        .hostToDeviceIsoChannel = 0U,
    };
    std::optional<IOReturn> prepareStatus;
    protocol.PrepareDuplex(channels, AudioClockConfig{.sampleRateHz = 48000U},
                           [&prepareStatus](IOReturn status, auto) {
                               prepareStatus = status;
                           });

    ASSERT_TRUE(prepareStatus.has_value());
    ASSERT_EQ(*prepareStatus, kIOReturnSuccess);
    EXPECT_GE(bus.generalReadCount, 2);

    std::optional<IOReturn> programRxStatus;
    protocol.ProgramRx([&programRxStatus](IOReturn status, auto) {
        programRxStatus = status;
    });

    ASSERT_TRUE(programRxStatus.has_value());
    EXPECT_EQ(*programRxStatus, kIOReturnSuccess);
    EXPECT_NE(std::find(bus.writeAddresses.begin(), bus.writeAddresses.end(),
                        kReloadedRxSectionBaseLo +
                            ASFW::Audio::DICE::RxOffset::kIsochronous),
              bus.writeAddresses.end());
    EXPECT_EQ(std::find(bus.writeAddresses.begin(), bus.writeAddresses.end(),
                        kRxSectionBaseLo + ASFW::Audio::DICE::RxOffset::kIsochronous),
              bus.writeAddresses.end());
}

TEST(DICETcatProtocolTests, ExtensionCapsUseTheDriverDiscoveredSectionAddress) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);

    std::optional<ExtensionSections> sections;
    protocol.Transaction().ReadExtensionSections([&](IOReturn status, ExtensionSections value) {
        ASSERT_EQ(status, kIOReturnSuccess);
        sections = value;
    });
    ASSERT_TRUE(sections.has_value());

    std::optional<ASFW::Audio::DICE::DiceExtensionCaps> caps;
    protocol.Transaction().ReadExtensionCaps(*sections,
        [&](IOReturn status, ASFW::Audio::DICE::DiceExtensionCaps value) {
            ASSERT_EQ(status, kIOReturnSuccess);
            caps = value;
        });
    ASSERT_TRUE(caps.has_value());
    EXPECT_TRUE(caps->router.exposed);
    EXPECT_EQ(caps->router.maximumEntryCount, 128);
    EXPECT_TRUE(caps->mixer.exposed);
    EXPECT_EQ(caps->mixer.inputCount, 18);
    EXPECT_EQ(caps->mixer.outputCount, 16);
    EXPECT_TRUE(caps->general.peakAvailable);
    EXPECT_EQ(bus.extensionCapsReadCount, 1);
}

TEST(DICETcatProtocolTests, ExtensionStateReadsAreExactAndUseDiscoveredSections) {
    CountingFireWireBus bus;
    RouteState routeState;
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr);

    std::optional<ExtensionSections> sections;
    protocol.Transaction().ReadExtensionSections([&](IOReturn status, ExtensionSections value) {
        ASSERT_EQ(status, kIOReturnSuccess);
        sections = value;
    });
    ASSERT_TRUE(sections.has_value());

    std::optional<ASFW::Audio::DICE::DiceExtensionCaps> caps;
    protocol.Transaction().ReadExtensionCaps(*sections,
        [&](IOReturn status, ASFW::Audio::DICE::DiceExtensionCaps value) {
            ASSERT_EQ(status, kIOReturnSuccess);
            caps = value;
        });
    ASSERT_TRUE(caps.has_value());

    std::optional<ASFW::Audio::DICE::DiceRouterEntries> routes;
    protocol.Transaction().ReadRouterEntries(*sections, *caps,
        [&](IOReturn status, ASFW::Audio::DICE::DiceRouterEntries value) {
            ASSERT_EQ(status, kIOReturnSuccess);
            routes = value;
        });
    ASSERT_TRUE(routes.has_value());
    ASSERT_EQ(routes->count, 2);
    EXPECT_EQ(routes->At(0).destinationBlock, 4);
    EXPECT_EQ(routes->At(0).sourceBlock, 11);
    EXPECT_EQ(routes->At(1).destinationChannel, 0);
    EXPECT_EQ(routes->At(1).sourceChannel, 3);
    EXPECT_EQ(bus.routerHeaderReadCount, 1);
    EXPECT_EQ(bus.routerEntriesReadCount, 1);

    std::optional<ASFW::Audio::DICE::DiceMixerCoefficients> mixer;
    protocol.Transaction().ReadMixerCoefficients(*sections, *caps,
        [&](IOReturn status, ASFW::Audio::DICE::DiceMixerCoefficients value) {
            ASSERT_EQ(status, kIOReturnSuccess);
            mixer = value;
        });
    ASSERT_TRUE(mixer.has_value());
    EXPECT_EQ(mixer->inputCount, 18);
    EXPECT_EQ(mixer->outputCount, 16);
    EXPECT_EQ(mixer->At(0, 0), 1);
    EXPECT_EQ(mixer->At(0, 17), 18);
    EXPECT_EQ(mixer->At(15, 17), 288);
    EXPECT_EQ(bus.mixerReadCount, 3);

    std::optional<IOReturn> mixerWriteStatus;
    protocol.Transaction().WriteMixerCoefficient(*sections, *caps, 3, 5, 0x4000,
        [&](IOReturn status) { mixerWriteStatus = status; });
    ASSERT_TRUE(mixerWriteStatus.has_value());
    EXPECT_EQ(*mixerWriteStatus, kIOReturnSuccess);
    ASSERT_FALSE(bus.mixerCoefficientWrites.empty());
    const auto& mixerWrite = bus.mixerCoefficientWrites.back();
    EXPECT_EQ(mixerWrite.address,
              kMixerSectionBaseLo + sizeof(uint32_t) +
                  sizeof(uint32_t) * (3U * ASFW::Audio::DICE::kDiceMaximumMixerInputs + 5U));
    EXPECT_EQ(mixerWrite.value, 0x4000U);

    const size_t writesBeforeBadIndex = bus.mixerCoefficientWrites.size();
    std::optional<IOReturn> invalidMixerWriteStatus;
    protocol.Transaction().WriteMixerCoefficient(*sections, *caps, 16, 0, 0x4000,
        [&](IOReturn status) { invalidMixerWriteStatus = status; });
    ASSERT_TRUE(invalidMixerWriteStatus.has_value());
    EXPECT_EQ(*invalidMixerWriteStatus, kIOReturnUnsupported);
    EXPECT_EQ(bus.mixerCoefficientWrites.size(), writesBeforeBadIndex);

    auto readOnlyCaps = *caps;
    readOnlyCaps.mixer.readOnly = true;
    std::optional<IOReturn> readOnlyMixerWriteStatus;
    protocol.Transaction().WriteMixerCoefficient(*sections, readOnlyCaps, 0, 0, 0x4000,
        [&](IOReturn status) { readOnlyMixerWriteStatus = status; });
    ASSERT_TRUE(readOnlyMixerWriteStatus.has_value());
    EXPECT_EQ(*readOnlyMixerWriteStatus, kIOReturnUnsupported);
    EXPECT_EQ(bus.mixerCoefficientWrites.size(), writesBeforeBadIndex);

    std::optional<ASFW::Audio::DICE::DiceRouterEntries> peaks;
    protocol.Transaction().ReadPeakEntries(*sections, *caps,
        [&](IOReturn status, ASFW::Audio::DICE::DiceRouterEntries value) {
            ASSERT_EQ(status, kIOReturnSuccess);
            peaks = value;
        });
    ASSERT_TRUE(peaks.has_value());
    ASSERT_EQ(peaks->count, 128);
    EXPECT_EQ(peaks->At(0).peak, 0x2100);
    EXPECT_EQ(peaks->At(1).peak, 0x2200);
    EXPECT_EQ(bus.peakReadCount, 1);
}

// ---------------------------------------------------------------------------
// DICE nickname decode (little-endian within each big-endian wire quadlet).
// cross-validated with FFADO dice_avdevice.cpp:696.
// ---------------------------------------------------------------------------

// Encode a string the way a DICE device stores it: the first character of each
// quadlet sits in the least-significant byte, and the quadlet is transmitted
// big-endian on the wire — so the wire bytes are the characters reversed within
// each 4-byte group. `out` is the global-section payload; nickname starts at
// GlobalOffset::kNickname (0x0C).
std::vector<uint8_t> MakeNicknamePayload(const std::string& name) {
    std::vector<uint8_t> payload(0x0C + 64, 0);
    for (size_t q = 0; q * 4 < name.size() && q < 16; ++q) {
        uint8_t chars[4] = {0, 0, 0, 0};
        for (size_t b = 0; b < 4; ++b) {
            const size_t idx = q * 4 + b;
            chars[b] = (idx < name.size()) ? static_cast<uint8_t>(name[idx]) : 0;
        }
        const size_t base = 0x0C + q * 4;
        payload[base + 0] = chars[3];  // MSB on the wire = last char of group
        payload[base + 1] = chars[2];
        payload[base + 2] = chars[1];
        payload[base + 3] = chars[0];  // LSB on the wire = first char of group
    }
    return payload;
}

TEST(DiceNicknameTests, DecodesLittleEndianStringNotByteReversed) {
    // The Midas Venice regression: "Veni" must not decode as "ineV".
    const auto payload = MakeNicknamePayload("Venice F32");
    char out[64]{};
    DecodeDiceNickname(payload.data(), payload.size(), out);
    EXPECT_STREQ(out, "Venice F32");
}

TEST(DiceNicknameTests, ShortNameWithinFirstQuadletTerminates) {
    const auto payload = MakeNicknamePayload("Hi");
    char out[64]{};
    DecodeDiceNickname(payload.data(), payload.size(), out);
    EXPECT_STREQ(out, "Hi");
}

TEST(DiceNicknameTests, StopsAtPayloadBoundaryWithoutOverrun) {
    // Only one full quadlet of nickname present after the 0x0C offset.
    std::vector<uint8_t> payload(0x0C + 4, 0);
    payload[0x0C + 0] = 'i';  // wire bytes for "Veni" -> first 4 chars only
    payload[0x0C + 1] = 'n';
    payload[0x0C + 2] = 'e';
    payload[0x0C + 3] = 'V';
    char out[64]{};
    DecodeDiceNickname(payload.data(), payload.size(), out);
    EXPECT_STREQ(out, "Veni");
}

TEST(DiceNicknameTests, EmptyNicknameYieldsEmptyString) {
    const std::vector<uint8_t> payload(0x0C + 64, 0);
    char out[64]{};
    DecodeDiceNickname(payload.data(), payload.size(), out);
    EXPECT_STREQ(out, "");
}

// ---------------------------------------------------------------------------
// DICE channel-label splitting (FFADO splitNameString).
// ---------------------------------------------------------------------------

TEST(DiceLabelTests, SplitsSingleBackslashSeparatedNames) {
    const auto names = SplitDiceLabels("Mic 1\\Mic 2\\Line 3\\\\");
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "Mic 1");
    EXPECT_EQ(names[1], "Mic 2");
    EXPECT_EQ(names[2], "Line 3");
}

TEST(DiceLabelTests, StopsAtDoubleBackslashTerminator) {
    // Padding after the "\\\\" terminator must be ignored.
    const auto names = SplitDiceLabels("A\\B\\\\garbage\\more");
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "A");
    EXPECT_EQ(names[1], "B");
}

TEST(DiceLabelTests, PreservesLeadingEmptyTokenForChannelAlignment) {
    // A leading separator yields an empty first token (channel 0 unnamed).
    // (Two consecutive separators would form the "\\\\" terminator, so an
    // interior empty token cannot occur.)
    const auto names = SplitDiceLabels("\\A\\B\\\\");
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "");
    EXPECT_EQ(names[1], "A");
    EXPECT_EQ(names[2], "B");
}

TEST(DiceLabelTests, NullAndEmptyYieldNoNames) {
    EXPECT_TRUE(SplitDiceLabels(nullptr).empty());
    EXPECT_TRUE(SplitDiceLabels("").empty());
    EXPECT_TRUE(SplitDiceLabels("\\\\").empty());
}

TEST(DiceLabelTests, SingleNameWithoutTerminator) {
    const auto names = SplitDiceLabels("Solo");
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "Solo");
}

} // namespace
