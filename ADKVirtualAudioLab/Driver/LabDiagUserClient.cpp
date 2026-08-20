#include <new> // first, before DriverKit headers (libc++ placement-new clash)
#include <DriverKit/IOLib.h>
#include <DriverKit/OSData.h>
#include <os/log.h>

#include "LabDiagUserClient.h"
#include "VirtualAudioDriver.h"
#include "VirtualAudioDevice.h"

#include "../Lab/ADKConfigChange.hpp"
#include "../Lab/PacketDumpBlob.hpp"

#define LAB_LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "[ADKLab] " fmt, ##__VA_ARGS__)

struct LabDiagUserClient_IVars
{
    VirtualAudioDriver* driver{nullptr}; // borrowed: our provider, retained by IOKit attach
};

bool LabDiagUserClient::init()
{
    if (!super::init()) {
        return false;
    }
    ivars = IONewZero(LabDiagUserClient_IVars, 1);
    return ivars != nullptr;
}

void LabDiagUserClient::free()
{
    IOSafeDeleteNULL(ivars, LabDiagUserClient_IVars, 1);
    super::free();
}

kern_return_t LabDiagUserClient::Start_Impl(IOService* provider)
{
    kern_return_t kr = Start(provider, SUPERDISPATCH);
    if (kr != kIOReturnSuccess) {
        return kr;
    }
    ivars->driver = OSDynamicCast(VirtualAudioDriver, provider);
    if (ivars->driver == nullptr) {
        LAB_LOG("LabDiagUserClient::Start - provider is not VirtualAudioDriver");
        Stop(provider, SUPERDISPATCH);
        return kIOReturnBadArgument;
    }
    LAB_LOG("LabDiagUserClient started");
    return kIOReturnSuccess;
}

kern_return_t LabDiagUserClient::Stop_Impl(IOService* provider)
{
    ivars->driver = nullptr;
    return Stop(provider, SUPERDISPATCH);
}

kern_return_t LabDiagUserClient::ExternalMethod(
    uint64_t selector, IOUserClientMethodArguments* arguments,
    const IOUserClientMethodDispatch* dispatch, OSObject* target,
    void* reference)
{
    if (arguments == nullptr || ivars == nullptr || ivars->driver == nullptr) {
        return kIOReturnNotReady;
    }

    switch (selector) {
    case ASFW::Lab::kLabDiagSelectorDumpPackets: {
        uint32_t count = ASFW::Lab::kPacketDumpDefaultRecords;
        uint64_t anchor = ASFW::Lab::kPacketDumpAnchorLatest;
        if (arguments->scalarInputCount >= 1) {
            count = static_cast<uint32_t>(arguments->scalarInput[0]);
        }
        if (arguments->scalarInputCount >= 2) {
            anchor = arguments->scalarInput[1];
        }

        VirtualAudioDevice* device = ivars->driver->GetVirtualAudioDevice();
        if (device == nullptr) {
            return kIOReturnNotReady;
        }

        OSData* blob = nullptr;
        kern_return_t kr = device->CopyPacketDump(count, anchor, &blob);
        if (kr != kIOReturnSuccess) {
            return kr;
        }
        arguments->structureOutput = blob; // ownership passes to the dispatcher
        return kIOReturnSuccess;
    }
    case ASFW::Lab::kLabDiagSelectorRequestSampleRate: {
        if (arguments->scalarInput == nullptr || arguments->scalarInputCount < 2) {
            return kIOReturnBadArgument;
        }
        const uint32_t slot = static_cast<uint32_t>(arguments->scalarInput[0]);
        const uint32_t sampleRate = static_cast<uint32_t>(arguments->scalarInput[1]);
        VirtualAudioDevice* device =
            ivars->driver->GetVirtualAudioDeviceForSlot(slot);
        if (device == nullptr) {
            return kIOReturnBadArgument;
        }
        LAB_LOG("request sample rate: slot=%{public}u rate=%{public}u", slot, sampleRate);
        const kern_return_t kr = device->RequestSampleRateChange(sampleRate);
        if (arguments->scalarOutput != nullptr &&
            arguments->scalarOutputCount >= 1) {
            arguments->scalarOutput[0] = static_cast<uint64_t>(kr);
            arguments->scalarOutputCount = 1;
        }
        return kr;
    }
    case ASFW::Lab::kLabDiagSelectorRequestConfiguration: {
        if (arguments->scalarInput == nullptr || arguments->scalarInputCount < 4) {
            return kIOReturnBadArgument;
        }
        const uint32_t slot = static_cast<uint32_t>(arguments->scalarInput[0]);
        const uint32_t sampleRate = static_cast<uint32_t>(arguments->scalarInput[1]);
        const uint32_t opticalInput = static_cast<uint32_t>(arguments->scalarInput[2]);
        const uint32_t opticalOutput = static_cast<uint32_t>(arguments->scalarInput[3]);
        VirtualAudioDevice* device =
            ivars->driver->GetVirtualAudioDeviceForSlot(slot);
        if (device == nullptr) {
            return kIOReturnBadArgument;
        }
        LAB_LOG("request configuration: slot=%{public}u rate=%{public}u optical_in=%{public}u optical_out=%{public}u",
                slot, sampleRate, opticalInput, opticalOutput);
        const kern_return_t kr = device->RequestConfigurationChange(
            sampleRate, opticalInput, opticalOutput);
        if (arguments->scalarOutput != nullptr &&
            arguments->scalarOutputCount >= 1) {
            arguments->scalarOutput[0] = static_cast<uint64_t>(kr);
            arguments->scalarOutputCount = 1;
        }
        return kr;
    }
    case ASFW::Lab::kLabDiagSelectorSetHardwareOutcome: {
        if (arguments->scalarInput == nullptr || arguments->scalarInputCount < 2) {
            return kIOReturnBadArgument;
        }
        const uint32_t slot = static_cast<uint32_t>(arguments->scalarInput[0]);
        const uint32_t outcome = static_cast<uint32_t>(arguments->scalarInput[1]);
        VirtualAudioDevice* device =
            ivars->driver->GetVirtualAudioDeviceForSlot(slot);
        if (device == nullptr) {
            return kIOReturnBadArgument;
        }
        LAB_LOG("set scripted hardware outcome: slot=%{public}u outcome=%{public}u",
                slot, outcome);
        return device->SetScriptedHardwareOutcome(outcome);
    }
    case ASFW::Lab::kLabDiagSelectorNotifyHardwareObserved: {
        if (arguments->scalarInput == nullptr || arguments->scalarInputCount < 4) {
            return kIOReturnBadArgument;
        }
        const uint32_t slot = static_cast<uint32_t>(arguments->scalarInput[0]);
        const uint32_t sampleRate = static_cast<uint32_t>(arguments->scalarInput[1]);
        const uint32_t opticalInput = static_cast<uint32_t>(arguments->scalarInput[2]);
        const uint32_t opticalOutput = static_cast<uint32_t>(arguments->scalarInput[3]);
        VirtualAudioDevice* device =
            ivars->driver->GetVirtualAudioDeviceForSlot(slot);
        if (device == nullptr) {
            return kIOReturnBadArgument;
        }
        LAB_LOG("notify observed configuration: slot=%{public}u rate=%{public}u optical_in=%{public}u optical_out=%{public}u",
                slot, sampleRate, opticalInput, opticalOutput);
        const kern_return_t kr = device->NotifyHardwareObserved(
            sampleRate, opticalInput, opticalOutput);
        if (arguments->scalarOutput != nullptr &&
            arguments->scalarOutputCount >= 1) {
            arguments->scalarOutput[0] = static_cast<uint64_t>(kr);
            arguments->scalarOutputCount = 1;
        }
        return kr;
    }
    case ASFW::Lab::kLabDiagSelectorCopyConfigLog: {
        if (arguments->scalarInput == nullptr || arguments->scalarInputCount < 1) {
            return kIOReturnBadArgument;
        }
        const uint32_t slot = static_cast<uint32_t>(arguments->scalarInput[0]);
        const uint32_t maxEvents = arguments->scalarInputCount >= 2
            ? static_cast<uint32_t>(arguments->scalarInput[1])
            : ASFW::Lab::kADKConfigLogDefaultEvents;
        VirtualAudioDevice* device =
            ivars->driver->GetVirtualAudioDeviceForSlot(slot);
        if (device == nullptr) {
            return kIOReturnBadArgument;
        }
        OSData* blob = nullptr;
        const kern_return_t kr = device->CopyADKConfigLog(maxEvents, &blob);
        if (kr != kIOReturnSuccess) {
            return kr;
        }
        arguments->structureOutput = blob;
        return kIOReturnSuccess;
    }
    case ASFW::Lab::kLabDiagSelectorCopyConfigState: {
        if (arguments->scalarInput == nullptr || arguments->scalarInputCount < 1) {
            return kIOReturnBadArgument;
        }
        const uint32_t slot = static_cast<uint32_t>(arguments->scalarInput[0]);
        VirtualAudioDevice* device =
            ivars->driver->GetVirtualAudioDeviceForSlot(slot);
        if (device == nullptr) {
            return kIOReturnBadArgument;
        }
        OSData* blob = nullptr;
        const kern_return_t kr = device->CopyADKConfigState(&blob);
        if (kr != kIOReturnSuccess) {
            return kr;
        }
        arguments->structureOutput = blob;
        return kIOReturnSuccess;
    }
    default:
        return kIOReturnBadArgument;
    }
}
