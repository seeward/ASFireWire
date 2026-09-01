#pragma once

#include "ControllerConfig.hpp"
#include "../Bus/BusManager.hpp"

namespace ASFW::Driver {

// Host cycle-master bring-up configuration. Linux firewire_ohci and Apple
// IOFireWireController both make the local PHY contender-capable during init.
// Apple's IOFireWireController enables root delegation only when its provider
// carries the explicit "DelegateCycleMaster" property; mirror that opt-in here.
inline void ApplyBringupOverrides(ControllerConfig& config, BusManager* busManager) {
    config.allowCycleMasterEligibility = true;

    if (busManager != nullptr) {
        busManager->SetDelegateMode(config.delegateCycleMaster);
    }
}

} // namespace ASFW::Driver
