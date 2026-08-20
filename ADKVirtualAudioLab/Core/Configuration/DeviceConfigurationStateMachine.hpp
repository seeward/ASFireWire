#pragma once

// The reducer belongs to the production-safe Audio shared layer. The lab keeps
// this forwarding header so its device-model and experiment code can exercise
// exactly the same state machine.
#include "../Device/Configuration.hpp"
#include "../../../ASFWDriver/Audio/Shared/Configuration/DeviceConfigurationStateMachine.hpp"
