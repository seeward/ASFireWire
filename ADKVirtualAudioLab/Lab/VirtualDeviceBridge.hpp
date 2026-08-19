#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <TargetConditionals.h>

#ifdef __cplusplus
extern "C" {
#endif

// Device Identifiers
typedef enum {
    ASFW_VIRTUAL_DEVICE_DUET = 0,
    ASFW_VIRTUAL_DEVICE_PHASE88 = 1,
    ASFW_VIRTUAL_DEVICE_FW1814 = 2,
    ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP = 3,
} ASFWVirtualDeviceKind;

typedef enum {
    ASFW_OPTICAL_NONE = 0,
    ASFW_OPTICAL_ADAT = 1,
    ASFW_OPTICAL_SPDIF = 2,
} ASFWOpticalMode;

typedef enum {
    ASFW_PARAM_KIND_BOOLEAN = 0,
    ASFW_PARAM_KIND_SCALAR = 1,
    ASFW_PARAM_KIND_ENUM = 2,
} ASFWParamKind;

typedef struct {
    int64_t value;
    const char* name;
} ASFWEnumItemDTO;

typedef struct {
    uint32_t id;
    const char* name;
    ASFWParamKind kind;
    double scalarValue;
    double scalarMin;
    double scalarMax;
    double scalarStep;
    const char* unit;
    bool boolValue;
    int64_t enumValue;
    uint32_t enumItemCount;
    const ASFWEnumItemDTO* enumItems;
} ASFWParameterDTO;

typedef struct {
    uint32_t inputPortId;
    uint32_t outputPortId;
} ASFWRouteDTO;

typedef struct {
    uint32_t bundleId;
    uint32_t routeCount;
    const ASFWRouteDTO* routes;
} ASFWRouteBundleDTO;

typedef struct {
    uint32_t nodeId;
    const char* name;
    uint32_t legalBundleCount;
    const ASFWRouteBundleDTO* legalBundles;
    uint32_t activeBundleCount;
    const uint32_t* activeBundleIds;
} ASFWRouterDTO;

typedef struct {
    uint32_t id;
    const char* name;
    double value;
    double min;
    double max;
} ASFWMeterDTO;

typedef struct {
    uint64_t revision;
    ASFWVirtualDeviceKind deviceKind;
    const char* manufacturer;
    const char* model;

    // Configuration
    uint32_t currentSampleRate;
    ASFWOpticalMode opticalInput;
    ASFWOpticalMode opticalOutput;

    // Capabilities
    uint32_t supportedSampleRateCount;
    const uint32_t* supportedSampleRates;
    bool hasOptical;

    // Streams
    uint32_t totalCaptureChannels;
    uint32_t totalPlaybackChannels;

    // Structural summary
    uint32_t nodeCount;
    uint32_t portCount;
    uint32_t linkCount;

    // Controls & Routing
    uint32_t parameterCount;
    const ASFWParameterDTO* parameters;

    uint32_t routerCount;
    const ASFWRouterDTO* routers;

    uint32_t meterCount;
    const ASFWMeterDTO* meters;
} ASFWDeviceSnapshotDTO;

#if !TARGET_OS_DRIVERKIT

// Bridge Lifecycle & Control (Host only)
void asfw_lab_init(void);
ASFWDeviceSnapshotDTO asfw_lab_get_snapshot(void);

bool asfw_lab_select_device(ASFWVirtualDeviceKind kind);
bool asfw_lab_set_configuration(uint32_t sampleRate, ASFWOpticalMode opticalIn, ASFWOpticalMode opticalOut);

bool asfw_lab_set_parameter_scalar(uint32_t parameterId, double value);
bool asfw_lab_set_parameter_bool(uint32_t parameterId, bool value);
bool asfw_lab_set_parameter_enum(uint32_t parameterId, int64_t value);

bool asfw_lab_set_active_route_bundles(uint32_t routerNodeId, const uint32_t* bundleIds, uint32_t bundleCount);

#endif // !TARGET_OS_DRIVERKIT

#ifdef __cplusplus
}
#endif
