#include <new> // first, before DriverKit headers (libc++ placement-new clash)
#include <AudioDriverKit/AudioDriverKit.h>
#include <DriverKit/IOLib.h>
#include <DriverKit/IODispatchQueue.h>
#include <DriverKit/OSString.h>
#include <os/log.h>
#include "VirtualAudioDriver.h"
#include "VirtualAudioDevice.h"

#include "../Lab/ADKConfigChange.hpp"
#include "../Lab/PacketDumpBlob.hpp"

#define LAB_LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "[ADKLab] " fmt, ##__VA_ARGS__)

constexpr uint32_t kVirtualAudioDeviceCount = ASFW::Lab::kADKConfigDeviceCount;

struct VirtualAudioDriver_IVars
{
    OSSharedPtr<IODispatchQueue> workQueue;
    OSSharedPtr<VirtualAudioDevice> audioDevices[kVirtualAudioDeviceCount];
};

struct LabDeviceSpec final
{
    const char* uid;
    const char* name;
};

constexpr LabDeviceSpec kLabDeviceSpecs[kVirtualAudioDeviceCount] = {
    {"VirtualADKAudioLab.Duet", "ADK Config Lab — Duet"},
    {"VirtualADKAudioLab.Phase88", "ADK Config Lab — PHASE 88"},
    {"VirtualADKAudioLab.FW1814", "ADK Config Lab — FireWire 1814"},
    {"VirtualADKAudioLab.Saffire", "ADK Config Lab — Saffire Pro 24 DSP"},
};

bool VirtualAudioDriver::init()
{
    if (!super::init()) {
        return false;
    }
    
    ivars = IONewZero(VirtualAudioDriver_IVars, 1);
    if (ivars == nullptr) {
        return false;
    }
    
    return true;
}

void VirtualAudioDriver::free()
{
    if (ivars != nullptr) {
        ivars->workQueue.reset();
        for (auto& device : ivars->audioDevices) {
            device.reset();
        }
    }
    IOSafeDeleteNULL(ivars, VirtualAudioDriver_IVars, 1);
    super::free();
}

kern_return_t VirtualAudioDriver::Start_Impl(IOService* provider)
{
    LAB_LOG("Start_Impl entering");
    kern_return_t kr = Start(provider, SUPERDISPATCH);
    if (kr != kIOReturnSuccess) {
        LAB_LOG("Start super failed with 0x%{public}08x", kr);
        return kr;
    }
    
    ivars->workQueue = GetWorkQueue();
    if (ivars->workQueue.get() == nullptr) {
        LAB_LOG("GetWorkQueue returned null");
        return kIOReturnInvalid;
    }
    
    LAB_LOG("Allocating %{public}u VirtualAudioDevice objects", kVirtualAudioDeviceCount);
    for (uint32_t slot = 0; slot < kVirtualAudioDeviceCount; ++slot) {
        const auto& spec = kLabDeviceSpecs[slot];
        auto& device = ivars->audioDevices[slot];

        device = OSSharedPtr(OSTypeAlloc(VirtualAudioDevice), OSNoRetain);
        if (!device) {
            LAB_LOG("Failed to allocate VirtualAudioDevice slot %{public}u", slot);
            return kIOReturnNoMemory;
        }

        auto deviceUID = OSSharedPtr(OSString::withCString(spec.uid), OSNoRetain);
        auto modelUID = OSSharedPtr(OSString::withCString("VirtualADKAudioLabModel"), OSNoRetain);
        auto manufacturerUID = OSSharedPtr(OSString::withCString("Alexander Shabelnikov"), OSNoRetain);
        if (!deviceUID || !modelUID || !manufacturerUID) {
            LAB_LOG("Failed to allocate UID strings for slot %{public}u", slot);
            return kIOReturnNoMemory;
        }

        LAB_LOG("Initializing VirtualAudioDevice slot %{public}u (%{public}s)", slot, spec.name);
        if (!device->init(this, false, deviceUID.get(), modelUID.get(),
                          manufacturerUID.get(), 512)) {
            LAB_LOG("VirtualAudioDevice::init failed for slot %{public}u", slot);
            return kIOReturnInternalError;
        }

        device->SetLabSlot(slot);
        auto deviceName = OSSharedPtr(OSString::withCString(spec.name), OSNoRetain);
        if (!deviceName) {
            LAB_LOG("Failed to allocate name for slot %{public}u", slot);
            return kIOReturnNoMemory;
        }
        kr = device->SetName(deviceName.get());
        if (kr != kIOReturnSuccess) {
            LAB_LOG("SetName failed for slot %{public}u with 0x%{public}08x", slot, kr);
            return kr;
        }

        // Keep the experiment devices from competing to become the system
        // default. Slot 0 remains eligible so the HAL can still exercise the
        // ordinary default-device path when desired.
        if (slot != 0) {
            device->SetCanBeDefaultOutputDevice(false);
            device->SetCanBeDefaultSystemOutputDevice(false);
        }

        LAB_LOG("Adding device object slot %{public}u", slot);
        kr = AddObject(device.get());
        if (kr != kIOReturnSuccess) {
            LAB_LOG("AddObject failed for slot %{public}u with 0x%{public}08x", slot, kr);
            return kr;
        }
    }
    
    LAB_LOG("Registering service");
    kr = RegisterService();
    if (kr != kIOReturnSuccess) {
        LAB_LOG("RegisterService failed with 0x%{public}08x", kr);
        return kr;
    }
    
    LAB_LOG("Start_Impl completed successfully");
    return kIOReturnSuccess;
}

kern_return_t VirtualAudioDriver::Stop_Impl(IOService* provider)
{
    LAB_LOG("Stop_Impl");
    
    for (auto& device : ivars->audioDevices) {
        if (device) {
            RemoveObject(device.get());
            device.reset();
        }
    }
    
    ivars->workQueue.reset();
    return Stop(provider, SUPERDISPATCH);
}

kern_return_t VirtualAudioDriver::NewUserClient_Impl(uint32_t in_type, IOUserClient** out_user_client)
{
    if (in_type == kIOUserAudioDriverUserClientType) {
        return super::NewUserClient(in_type, out_user_client, SUPERDISPATCH);
    }

    // Lab inspector connection ('LDBG'): instantiate LabDiagUserClient via
    // the personality dictionary in Info.plist.
    if (in_type == ASFW::Lab::kLabDiagUserClientType) {
        IOService* service = nullptr;
        kern_return_t kr = Create(this, "LabDiagUserClientProperties", &service);
        if (kr != kIOReturnSuccess) {
            LAB_LOG("NewUserClient - Create(LabDiagUserClientProperties) failed 0x%{public}08x", kr);
            return kr;
        }
        IOUserClient* client = OSDynamicCast(IOUserClient, service);
        if (client == nullptr) {
            service->release();
            return kIOReturnError;
        }
        *out_user_client = client;
        return kIOReturnSuccess;
    }

    return kIOReturnBadArgument;
}

VirtualAudioDevice* VirtualAudioDriver::GetVirtualAudioDevice()
{
    return GetVirtualAudioDeviceForSlot(0);
}

VirtualAudioDevice* VirtualAudioDriver::GetVirtualAudioDeviceForSlot(uint32_t in_slot)
{
    if (ivars == nullptr || in_slot >= kVirtualAudioDeviceCount) {
        return nullptr;
    }
    return ivars->audioDevices[in_slot].get();
}

kern_return_t VirtualAudioDriver::StartDevice(IOUserAudioObjectID in_object_id,
                                              IOUserAudioStartStopFlags in_flags)
{
    LAB_LOG("StartDevice 0x%{public}x", (uint32_t)in_object_id);
    
    VirtualAudioDevice* device = nullptr;
    for (auto& candidate : ivars->audioDevices) {
        if (candidate && candidate->GetObjectID() == in_object_id) {
            device = candidate.get();
            break;
        }
    }
    if (device == nullptr) {
        LAB_LOG("StartDevice - unknown object id 0x%{public}x", (uint32_t)in_object_id);
        return kIOReturnBadArgument;
    }
    
    __block kern_return_t kr = kIOReturnSuccess;
    ivars->workQueue->DispatchSync(^(){
        kr = super::StartDevice(in_object_id, in_flags);
    });
    
    if (kr != kIOReturnSuccess) {
        LAB_LOG("StartDevice - super::StartDevice failed with 0x%{public}08x", kr);
    }
    return kr;
}

kern_return_t VirtualAudioDriver::StopDevice(IOUserAudioObjectID in_object_id,
                                             IOUserAudioStartStopFlags in_flags)
{
    LAB_LOG("StopDevice 0x%{public}x", (uint32_t)in_object_id);
    
    VirtualAudioDevice* device = nullptr;
    for (auto& candidate : ivars->audioDevices) {
        if (candidate && candidate->GetObjectID() == in_object_id) {
            device = candidate.get();
            break;
        }
    }
    if (device == nullptr) {
        LAB_LOG("StopDevice - unknown object id 0x%{public}x", (uint32_t)in_object_id);
        return kIOReturnBadArgument;
    }
    
    __block kern_return_t kr = kIOReturnSuccess;
    ivars->workQueue->DispatchSync(^(){
        kr = super::StopDevice(in_object_id, in_flags);
    });
    
    if (kr != kIOReturnSuccess) {
        LAB_LOG("StopDevice - super::StopDevice failed with 0x%{public}08x", kr);
    }
    return kr;
}
