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

typedef enum {
    ASFW_SEMANTIC_UNKNOWN = 0,
    ASFW_SEMANTIC_LEVEL = 1,
    ASFW_SEMANTIC_MUTE = 2,
    ASFW_SEMANTIC_PHANTOM_POWER = 3,
    ASFW_SEMANTIC_PHASE_INVERT = 4,
    ASFW_SEMANTIC_PAN = 5,
    ASFW_SEMANTIC_BALANCE = 6,
    ASFW_SEMANTIC_SOLO = 7,
    ASFW_SEMANTIC_NOMINAL_LEVEL = 8,
    ASFW_SEMANTIC_CLOCK_SOURCE = 9,
    ASFW_SEMANTIC_DIM = 10,
} ASFWParameterSemantic;

typedef enum {
    ASFW_TARGET_NODE = 0,
    ASFW_TARGET_PORT = 1,
    ASFW_TARGET_CROSSPOINT = 2,
} ASFWTargetKind;

typedef enum {
    ASFW_NODE_ENDPOINT_PHYSICAL = 0,
    ASFW_NODE_ENDPOINT_HOST = 1,
    ASFW_NODE_ROUTER = 2,
    ASFW_NODE_MIXER = 3,
    ASFW_NODE_PROCESSOR = 4,
} ASFWNodeKind;

// Presentation Enums
typedef enum {
    ASFW_PRES_GROUP_INPUT_CHANNEL = 0,
    ASFW_PRES_GROUP_OUTPUT_CHANNEL = 1,
    ASFW_PRES_GROUP_MIXER = 2,
    ASFW_PRES_GROUP_MONITOR = 3,
    ASFW_PRES_GROUP_ROUTING = 4,
    ASFW_PRES_GROUP_PROCESSOR = 5,
    ASFW_PRES_GROUP_OTHER = 6,
} ASFWPresGroupKind;

typedef enum {
    ASFW_ROUTER_STYLE_AUTO = 0,
    ASFW_ROUTER_STYLE_SELECTOR = 1,
    ASFW_ROUTER_STYLE_PATCHBAY = 2,
    ASFW_ROUTER_STYLE_MATRIX = 3,
} ASFWRouterStyle;

typedef enum {
    ASFW_MIXER_STYLE_AUTO = 0,
    ASFW_MIXER_STYLE_CHANNEL_STRIPS = 1,
    ASFW_MIXER_STYLE_MATRIX = 2,
} ASFWMixerStyle;

typedef enum {
    ASFW_CONTROL_AUTO = 0,
    ASFW_CONTROL_FADER = 1,
    ASFW_CONTROL_ROTARY = 2,
    ASFW_CONTROL_TOGGLE = 3,
    ASFW_CONTROL_SELECTOR = 4,
} ASFWControlPresentation;

typedef enum {
    ASFW_BUS_SEMANTIC_UNKNOWN = 0,
    ASFW_BUS_SEMANTIC_MAIN = 1,
    ASFW_BUS_SEMANTIC_AUX = 2,
    ASFW_BUS_SEMANTIC_MONITOR = 3,
    ASFW_BUS_SEMANTIC_CUE = 4,
} ASFWBusSemantic;

typedef struct {
    uint32_t id;
    const char* name;
    uint32_t portCount;
    const uint32_t* portIds;
} ASFWChannelDTO;

typedef struct {
    uint32_t id;
    ASFWBusSemantic semantic;
    const char* name;
    uint32_t portCount;
    const uint32_t* portIds;
} ASFWBusDTO;

typedef enum {
    ASFW_PLACEMENT_AUTO = 0,
    ASFW_PLACEMENT_CHANNEL_HEADER = 1,
    ASFW_PLACEMENT_CHANNEL_STRIP = 2,
    ASFW_PLACEMENT_CHANNEL_FOOTER = 3,
    ASFW_PLACEMENT_CROSSPOINT = 4,
    ASFW_PLACEMENT_MASTER = 5,
    ASFW_PLACEMENT_ADVANCED = 6,
} ASFWControlPlacement;

typedef struct {
    const char* name;
    uint32_t portCount;
    const uint32_t* portIds;
} ASFWPortGroupDTO;

typedef struct {
    const char* name;
    uint32_t bundleCount;
    const uint32_t* bundleIds;
} ASFWBundleGroupDTO;

typedef struct {
    uint32_t id;
    const char* name;
    ASFWPresGroupKind kind;
    uint32_t nodeCount;
    const uint32_t* nodeIds;
    uint32_t portCount;
    const uint32_t* portIds;
    uint32_t parameterCount;
    const uint32_t* parameterIds;
    uint32_t meterCount;
    const uint32_t* meterIds;
} ASFWPresentationGroupDTO;

typedef struct {
    uint32_t routerNodeId;
    ASFWRouterStyle style;
    uint32_t inputGroupCount;
    const ASFWPortGroupDTO* inputGroups;
    uint32_t outputGroupCount;
    const ASFWPortGroupDTO* outputGroups;
    uint32_t bundleGroupCount;
    const ASFWBundleGroupDTO* bundleGroups;
} ASFWRouterHintDTO;

typedef struct {
    uint32_t mixerNodeId;
    ASFWMixerStyle style;
    uint32_t inputGroupCount;
    const ASFWPortGroupDTO* inputGroups;
    uint32_t outputGroupCount;
    const ASFWPortGroupDTO* outputGroups;
} ASFWMixerHintDTO;

typedef struct {
    uint32_t parameterId;
    ASFWControlPlacement placement;
    ASFWControlPresentation presentation;
    const char* section;
} ASFWParameterHintDTO;

typedef struct {
    uint32_t groupCount;
    const ASFWPresentationGroupDTO* groups;
    uint32_t routerHintCount;
    const ASFWRouterHintDTO* routerHints;
    uint32_t mixerHintCount;
    const ASFWMixerHintDTO* mixerHints;
    uint32_t parameterHintCount;
    const ASFWParameterHintDTO* parameterHints;
} ASFWDevicePresentationDTO;

typedef struct {
    int64_t value;
    const char* name;
} ASFWEnumItemDTO;

typedef struct {
    uint32_t id;
    const char* name;
    ASFWParamKind kind;
    ASFWParameterSemantic semantic;
    ASFWTargetKind targetKind;
    uint32_t targetId;
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

typedef enum {
    ASFW_SIGNAL_UNKNOWN = 0,
    ASFW_SIGNAL_ANALOG_LINE = 1,
    ASFW_SIGNAL_ANALOG_MIC_XLR = 2,
    ASFW_SIGNAL_ANALOG_INSTRUMENT = 3,
    ASFW_SIGNAL_HEADPHONE = 4,
    ASFW_SIGNAL_SPDIF_COAXIAL = 5,
    ASFW_SIGNAL_SPDIF_OPTICAL = 6,
    ASFW_SIGNAL_ADAT = 7,
    ASFW_SIGNAL_HOST_STREAM = 8,
} ASFWSignalKind;

typedef struct {
    uint32_t id;
    const char* name; // canonical for endpoint ports, authored for interior ones
    uint32_t ownerNodeId;
    uint8_t direction; // 0 = Input, 1 = Output
    uint32_t channels;
    ASFWSignalKind signalKind;
    uint32_t signalIndex;
} ASFWPortDTO;

typedef struct {
    uint32_t id;
    uint32_t inputPortId;
    uint32_t outputPortId;
} ASFWMixerCrosspointDTO;

typedef struct {
    uint32_t nodeId;
    const char* name;
    uint32_t inputPortCount;
    const uint32_t* inputPortIds;
    uint32_t outputPortCount;
    const uint32_t* outputPortIds;
    uint32_t crosspointCount;
    const ASFWMixerCrosspointDTO* crosspoints;
} ASFWMixerDTO;

typedef struct {
    uint32_t nodeId;
    const char* name;
    ASFWNodeKind kind;
    uint32_t inputPortCount;
    const uint32_t* inputPortIds;
    uint32_t outputPortCount;
    const uint32_t* outputPortIds;
} ASFWNodeDTO;

typedef struct {
    uint32_t inputPortId;
    uint32_t outputPortId;
} ASFWRouteDTO;

typedef struct {
    uint32_t bundleId;
    uint32_t routeCount;
    const ASFWRouteDTO* routes;
    /// Where this bundle's signal comes from, resolved by walking the topology:
    /// an endpoint connector, or the bus of a mixer/router output. Empty when
    /// neither applies, so the UI can fall back rather than guess.
    const char* sourceLabel;
} ASFWRouteBundleDTO;

typedef struct {
    uint32_t nodeId;
    const char* name;
    uint32_t inputPortCount;
    const uint32_t* inputPortIds;
    uint32_t outputPortCount;
    const uint32_t* outputPortIds;
    uint32_t legalBundleCount;
    const ASFWRouteBundleDTO* legalBundles;
    uint32_t activeBundleCount;
    const uint32_t* activeBundleIds;
} ASFWRouterDTO;

typedef struct {
    uint32_t id;
    const char* name;
    uint32_t targetPortId;
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

    // Structural summary & Nodes
    uint32_t nodeCount;
    const ASFWNodeDTO* nodes;

    uint32_t portCount;
    const ASFWPortDTO* ports;
    uint32_t linkCount;

    // Structural Nodes
    uint32_t mixerCount;
    const ASFWMixerDTO* mixers;

    uint32_t routerCount;
    const ASFWRouterDTO* routers;

    // Controls & Telemetry
    uint32_t parameterCount;
    const ASFWParameterDTO* parameters;

    uint32_t meterCount;
    const ASFWMeterDTO* meters;

    // Logical Channels & Busses (Audio Semantics)
    uint32_t channelCount;
    const ASFWChannelDTO* channels;

    uint32_t busCount;
    const ASFWBusDTO* buses;

    // Presentation Layer & Surface Semantics
    ASFWDevicePresentationDTO presentation;
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
