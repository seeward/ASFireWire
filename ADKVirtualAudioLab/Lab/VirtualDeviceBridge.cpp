#include <TargetConditionals.h>

#if !TARGET_OS_DRIVERKIT

#include "VirtualDeviceBridge.hpp"
#include "../Runtime/VirtualDeviceRuntime.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace ASFW::AudioModel;
using namespace ASFW::Device;
using namespace ASFW::Presentation;
using namespace ASFW::Runtime;

namespace {

std::mutex gMutex;
std::unique_ptr<VirtualDeviceRuntime> gRuntime;

// Storage backing the DTO snapshot strings and arrays
struct SnapshotStorage {
    std::string manufacturer;
    std::string model;
    std::vector<uint32_t> supportedRates;

    std::vector<std::string> nodeNames;
    std::vector<std::vector<uint32_t>> nodeInputPortArrays;
    std::vector<std::vector<uint32_t>> nodeOutputPortArrays;
    std::vector<ASFWNodeDTO> nodeDTOs;

    std::vector<std::string> portNames;
    std::vector<ASFWPortDTO> portDTOs;

    std::vector<std::string> mixerNames;
    std::vector<std::vector<uint32_t>> mixerInputPortArrays;
    std::vector<std::vector<uint32_t>> mixerOutputPortArrays;
    std::vector<std::vector<ASFWMixerCrosspointDTO>> mixerCrosspointArrays;
    std::vector<ASFWMixerDTO> mixerDTOs;

    std::vector<std::string> paramNames;
    std::vector<std::string> unitStrings;
    std::vector<std::vector<std::string>> enumNames;
    std::vector<std::vector<ASFWEnumItemDTO>> enumItemArrays;
    std::vector<ASFWParameterDTO> paramDTOs;

    std::vector<std::string> routerNames;
    std::vector<std::vector<uint32_t>> routerInputPortArrays;
    std::vector<std::vector<uint32_t>> routerOutputPortArrays;
    std::vector<std::vector<std::vector<ASFWRouteDTO>>> routerRouteArrays;
    std::vector<std::vector<ASFWRouteBundleDTO>> routerBundleArrays;
    std::vector<std::vector<uint32_t>> activeBundleArrays;
    std::vector<ASFWRouterDTO> routerDTOs;

    std::vector<std::string> meterNames;
    std::vector<ASFWMeterDTO> meterDTOs;

    // Presentation Storage
    std::vector<std::string> presGroupNames;
    std::vector<std::vector<uint32_t>> presGroupNodeArrays;
    std::vector<std::vector<uint32_t>> presGroupPortArrays;
    std::vector<std::vector<uint32_t>> presGroupParamArrays;
    std::vector<std::vector<uint32_t>> presGroupMeterArrays;
    std::vector<ASFWPresentationGroupDTO> presGroupDTOs;

    std::vector<std::vector<std::string>> routerInGroupNames;
    std::vector<std::vector<std::vector<uint32_t>>> routerInGroupPortArrays;
    std::vector<std::vector<ASFWPortGroupDTO>> routerInGroupDTOs;

    std::vector<std::vector<std::string>> routerOutGroupNames;
    std::vector<std::vector<std::vector<uint32_t>>> routerOutGroupPortArrays;
    std::vector<std::vector<ASFWPortGroupDTO>> routerOutGroupDTOs;

    std::vector<std::vector<std::string>> routerBundleGroupNames;
    std::vector<std::vector<std::vector<uint32_t>>> routerBundleGroupBundleArrays;
    std::vector<std::vector<ASFWBundleGroupDTO>> routerBundleGroupDTOs;
    std::vector<ASFWRouterHintDTO> routerHintDTOs;

    std::vector<std::vector<std::string>> mixerInGroupNames;
    std::vector<std::vector<std::vector<uint32_t>>> mixerInGroupPortArrays;
    std::vector<std::vector<ASFWPortGroupDTO>> mixerInGroupDTOs;

    std::vector<std::vector<std::string>> mixerOutGroupNames;
    std::vector<std::vector<std::vector<uint32_t>>> mixerOutGroupPortArrays;
    std::vector<std::vector<ASFWPortGroupDTO>> mixerOutGroupDTOs;
    std::vector<ASFWMixerHintDTO> mixerHintDTOs;

    std::vector<std::string> paramSectionNames;
    std::vector<ASFWParameterHintDTO> parameterHintDTOs;
};

SnapshotStorage gStorage;

void ensureInitialized() {
    if (!gRuntime) {
        auto rt = VirtualDeviceRuntime::create(VirtualDeviceKind::Duet);
        if (rt.has_value()) {
            gRuntime = std::make_unique<VirtualDeviceRuntime>(std::move(*rt));
        }
    }
}

ASFWOpticalMode toBridgeOptical(std::optional<OpticalMode> opt) {
    if (!opt.has_value()) return ASFW_OPTICAL_NONE;
    switch (*opt) {
        case OpticalMode::Adat: return ASFW_OPTICAL_ADAT;
        case OpticalMode::Spdif: return ASFW_OPTICAL_SPDIF;
    }
}

ASFWParameterSemantic toBridgeSemantic(ParameterSemantic sem) {
    switch (sem) {
        case ParameterSemantic::Unknown: return ASFW_SEMANTIC_UNKNOWN;
        case ParameterSemantic::Level: return ASFW_SEMANTIC_LEVEL;
        case ParameterSemantic::Mute: return ASFW_SEMANTIC_MUTE;
        case ParameterSemantic::PhantomPower: return ASFW_SEMANTIC_PHANTOM_POWER;
        case ParameterSemantic::PhaseInvert: return ASFW_SEMANTIC_PHASE_INVERT;
        case ParameterSemantic::Balance: return ASFW_SEMANTIC_BALANCE;
        case ParameterSemantic::NominalLevel: return ASFW_SEMANTIC_NOMINAL_LEVEL;
        case ParameterSemantic::ClockSource: return ASFW_SEMANTIC_CLOCK_SOURCE;
        case ParameterSemantic::Dim: return ASFW_SEMANTIC_DIM;
    }
}

std::optional<OpticalMode> fromBridgeOptical(ASFWOpticalMode mode) {
    switch (mode) {
        case ASFW_OPTICAL_ADAT: return OpticalMode::Adat;
        case ASFW_OPTICAL_SPDIF: return OpticalMode::Spdif;
        case ASFW_OPTICAL_NONE: return std::nullopt;
    }
}

ASFWPresGroupKind toBridgePresGroupKind(PresentationGroupKind k) {
    switch (k) {
        case PresentationGroupKind::InputChannel: return ASFW_PRES_GROUP_INPUT_CHANNEL;
        case PresentationGroupKind::OutputChannel: return ASFW_PRES_GROUP_OUTPUT_CHANNEL;
        case PresentationGroupKind::Mixer: return ASFW_PRES_GROUP_MIXER;
        case PresentationGroupKind::Monitor: return ASFW_PRES_GROUP_MONITOR;
        case PresentationGroupKind::Routing: return ASFW_PRES_GROUP_ROUTING;
        case PresentationGroupKind::Processor: return ASFW_PRES_GROUP_PROCESSOR;
        case PresentationGroupKind::Other: return ASFW_PRES_GROUP_OTHER;
    }
}

ASFWRouterStyle toBridgeRouterStyle(RouterPresentationStyle s) {
    switch (s) {
        case RouterPresentationStyle::Auto: return ASFW_ROUTER_STYLE_AUTO;
        case RouterPresentationStyle::Selector: return ASFW_ROUTER_STYLE_SELECTOR;
        case RouterPresentationStyle::Patchbay: return ASFW_ROUTER_STYLE_PATCHBAY;
        case RouterPresentationStyle::Matrix: return ASFW_ROUTER_STYLE_MATRIX;
    }
}

ASFWMixerStyle toBridgeMixerStyle(MixerPresentationStyle s) {
    switch (s) {
        case MixerPresentationStyle::Auto: return ASFW_MIXER_STYLE_AUTO;
        case MixerPresentationStyle::ChannelStrips: return ASFW_MIXER_STYLE_CHANNEL_STRIPS;
        case MixerPresentationStyle::Matrix: return ASFW_MIXER_STYLE_MATRIX;
    }
}

ASFWControlPlacement toBridgeControlPlacement(ControlPlacement p) {
    switch (p) {
        case ControlPlacement::Auto: return ASFW_PLACEMENT_AUTO;
        case ControlPlacement::ChannelHeader: return ASFW_PLACEMENT_CHANNEL_HEADER;
        case ControlPlacement::ChannelStrip: return ASFW_PLACEMENT_CHANNEL_STRIP;
        case ControlPlacement::ChannelFooter: return ASFW_PLACEMENT_CHANNEL_FOOTER;
        case ControlPlacement::Crosspoint: return ASFW_PLACEMENT_CROSSPOINT;
        case ControlPlacement::Master: return ASFW_PLACEMENT_MASTER;
        case ControlPlacement::Advanced: return ASFW_PLACEMENT_ADVANCED;
    }
}

} // namespace

void asfw_lab_init(void) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureInitialized();
}

bool asfw_lab_select_device(ASFWVirtualDeviceKind kind) {
    std::lock_guard<std::mutex> lock(gMutex);
    VirtualDeviceKind k = VirtualDeviceKind::Duet;
    switch (kind) {
        case ASFW_VIRTUAL_DEVICE_DUET: k = VirtualDeviceKind::Duet; break;
        case ASFW_VIRTUAL_DEVICE_PHASE88: k = VirtualDeviceKind::Phase88; break;
        case ASFW_VIRTUAL_DEVICE_FW1814: k = VirtualDeviceKind::FW1814; break;
        case ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP: k = VirtualDeviceKind::SaffirePro24DSP; break;
    }

    auto rt = VirtualDeviceRuntime::create(k);
    if (!rt.has_value()) {
        return false;
    }
    gRuntime = std::make_unique<VirtualDeviceRuntime>(std::move(*rt));
    return true;
}

bool asfw_lab_set_configuration(uint32_t sampleRate, ASFWOpticalMode opticalIn, ASFWOpticalMode opticalOut) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureInitialized();
    if (!gRuntime) return false;

    DeviceConfiguration config{
        .sampleRate = sampleRate,
        .opticalInput = fromBridgeOptical(opticalIn),
        .opticalOutput = fromBridgeOptical(opticalOut),
    };

    auto res = gRuntime->setConfiguration(config);
    return res.has_value();
}

bool asfw_lab_set_parameter_scalar(uint32_t parameterId, double value) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureInitialized();
    if (!gRuntime) return false;
    auto res = gRuntime->setParameter(ParameterId{parameterId}, value);
    return res.has_value();
}

bool asfw_lab_set_parameter_bool(uint32_t parameterId, bool value) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureInitialized();
    if (!gRuntime) return false;
    auto res = gRuntime->setParameter(ParameterId{parameterId}, value);
    return res.has_value();
}

bool asfw_lab_set_parameter_enum(uint32_t parameterId, int64_t value) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureInitialized();
    if (!gRuntime) return false;
    auto res = gRuntime->setParameter(ParameterId{parameterId}, value);
    return res.has_value();
}

bool asfw_lab_set_active_route_bundles(uint32_t routerNodeId, const uint32_t* bundleIds, uint32_t bundleCount) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureInitialized();
    if (!gRuntime) return false;

    std::vector<RouteBundleId> bundles;
    bundles.reserve(bundleCount);
    for (uint32_t i = 0; i < bundleCount; ++i) {
        bundles.push_back(RouteBundleId{bundleIds[i]});
    }

    auto res = gRuntime->setActiveRouteBundles(NodeId{routerNodeId}, bundles);
    return res.has_value();
}

ASFWDeviceSnapshotDTO asfw_lab_get_snapshot(void) {
    std::lock_guard<std::mutex> lock(gMutex);
    ensureInitialized();

    ASFWDeviceSnapshotDTO snapshot = {};
    if (!gRuntime) return snapshot;

    snapshot.revision = gRuntime->revision();
    snapshot.deviceKind = static_cast<ASFWVirtualDeviceKind>(gRuntime->kind());

    const auto& caps = gRuntime->capabilities();
    const auto& cfg = gRuntime->configuration();
    const auto& res = gRuntime->resolved();
    const auto& state = gRuntime->state();
    const auto& pres = res.presentation;

    gStorage.manufacturer = caps.identity.manufacturer;
    gStorage.model = caps.identity.model;
    snapshot.manufacturer = gStorage.manufacturer.c_str();
    snapshot.model = gStorage.model.c_str();

    snapshot.currentSampleRate = cfg.sampleRate;
    snapshot.opticalInput = toBridgeOptical(cfg.opticalInput);
    snapshot.opticalOutput = toBridgeOptical(cfg.opticalOutput);

    gStorage.supportedRates = caps.sampleRates;
    snapshot.supportedSampleRateCount = static_cast<uint32_t>(gStorage.supportedRates.size());
    snapshot.supportedSampleRates = gStorage.supportedRates.data();
    snapshot.hasOptical = caps.optical.has_value();

    uint32_t capCh = 0;
    uint32_t playCh = 0;
    for (const auto& s : res.streams.streams) {
        if (s.direction == StreamDirection::Capture) capCh += s.channels;
        else playCh += s.channels;
    }
    snapshot.totalCaptureChannels = capCh;
    snapshot.totalPlaybackChannels = playCh;

    snapshot.linkCount = static_cast<uint32_t>(res.topology.fixedLinks.size());

    // 1. Nodes
    const size_t nCount = res.topology.nodes.size();
    gStorage.nodeNames.resize(nCount);
    gStorage.nodeInputPortArrays.resize(nCount);
    gStorage.nodeOutputPortArrays.resize(nCount);
    gStorage.nodeDTOs.resize(nCount);

    for (size_t i = 0; i < nCount; ++i) {
        const auto& n = res.topology.nodes[i];
        gStorage.nodeNames[i] = n.name;
        ASFWNodeDTO& nDto = gStorage.nodeDTOs[i];
        nDto = {};
        nDto.nodeId = n.id.value;
        nDto.name = gStorage.nodeNames[i].c_str();

        gStorage.nodeInputPortArrays[i].clear();
        gStorage.nodeOutputPortArrays[i].clear();

        std::visit([&](const auto& body) {
            using B = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<B, EndpointNode>) {
                nDto.kind = body.kind == EndpointKind::Physical ? ASFW_NODE_ENDPOINT_PHYSICAL : ASFW_NODE_ENDPOINT_HOST;
            } else if constexpr (std::is_same_v<B, RouterNode>) {
                nDto.kind = ASFW_NODE_ROUTER;
                for (auto pid : body.inputs) gStorage.nodeInputPortArrays[i].push_back(pid.value);
                for (auto pid : body.outputs) gStorage.nodeOutputPortArrays[i].push_back(pid.value);
            } else if constexpr (std::is_same_v<B, MixerNode>) {
                nDto.kind = ASFW_NODE_MIXER;
                for (auto pid : body.inputs) gStorage.nodeInputPortArrays[i].push_back(pid.value);
                for (auto pid : body.outputs) gStorage.nodeOutputPortArrays[i].push_back(pid.value);
            } else if constexpr (std::is_same_v<B, ProcessorNode>) {
                nDto.kind = ASFW_NODE_PROCESSOR;
                for (auto pid : body.inputs) gStorage.nodeInputPortArrays[i].push_back(pid.value);
                for (auto pid : body.outputs) gStorage.nodeOutputPortArrays[i].push_back(pid.value);
            }
        }, n.body);

        nDto.inputPortCount = static_cast<uint32_t>(gStorage.nodeInputPortArrays[i].size());
        nDto.inputPortIds = gStorage.nodeInputPortArrays[i].data();
        nDto.outputPortCount = static_cast<uint32_t>(gStorage.nodeOutputPortArrays[i].size());
        nDto.outputPortIds = gStorage.nodeOutputPortArrays[i].data();
    }
    snapshot.nodeCount = static_cast<uint32_t>(gStorage.nodeDTOs.size());
    snapshot.nodes = gStorage.nodeDTOs.data();

    // 2. Ports
    const size_t ptCount = res.topology.ports.size();
    gStorage.portNames.resize(ptCount);
    gStorage.portDTOs.resize(ptCount);
    for (size_t i = 0; i < ptCount; ++i) {
        const auto& pt = res.topology.ports[i];
        gStorage.portNames[i] = pt.name;
        gStorage.portDTOs[i] = ASFWPortDTO{
            .id = pt.id.value,
            .name = gStorage.portNames[i].c_str(),
            .ownerNodeId = pt.owner.value,
            .direction = static_cast<uint8_t>(pt.direction == PortDirection::Output ? 1 : 0),
            .channels = pt.channels,
        };
    }
    snapshot.portCount = static_cast<uint32_t>(gStorage.portDTOs.size());
    snapshot.ports = gStorage.portDTOs.data();

    // 3. Mixers
    std::vector<const Node*> mixerNodes;
    for (const auto& n : res.topology.nodes) {
        if (std::holds_alternative<MixerNode>(n.body)) {
            mixerNodes.push_back(&n);
        }
    }
    const size_t mxCount = mixerNodes.size();
    gStorage.mixerNames.resize(mxCount);
    gStorage.mixerInputPortArrays.resize(mxCount);
    gStorage.mixerOutputPortArrays.resize(mxCount);
    gStorage.mixerCrosspointArrays.resize(mxCount);
    gStorage.mixerDTOs.resize(mxCount);

    for (size_t i = 0; i < mxCount; ++i) {
        const auto* n = mixerNodes[i];
        const auto& mx = std::get<MixerNode>(n->body);
        gStorage.mixerNames[i] = n->name;
        ASFWMixerDTO& mDto = gStorage.mixerDTOs[i];
        mDto = {};
        mDto.nodeId = n->id.value;
        mDto.name = gStorage.mixerNames[i].c_str();

        gStorage.mixerInputPortArrays[i].clear();
        for (auto pid : mx.inputs) gStorage.mixerInputPortArrays[i].push_back(pid.value);
        mDto.inputPortCount = static_cast<uint32_t>(gStorage.mixerInputPortArrays[i].size());
        mDto.inputPortIds = gStorage.mixerInputPortArrays[i].data();

        gStorage.mixerOutputPortArrays[i].clear();
        for (auto pid : mx.outputs) gStorage.mixerOutputPortArrays[i].push_back(pid.value);
        mDto.outputPortCount = static_cast<uint32_t>(gStorage.mixerOutputPortArrays[i].size());
        mDto.outputPortIds = gStorage.mixerOutputPortArrays[i].data();

        gStorage.mixerCrosspointArrays[i].clear();
        for (const auto& cp : mx.crosspoints) {
            gStorage.mixerCrosspointArrays[i].push_back(ASFWMixerCrosspointDTO{
                .id = cp.id.value,
                .inputPortId = cp.input.value,
                .outputPortId = cp.output.value,
            });
        }
        mDto.crosspointCount = static_cast<uint32_t>(gStorage.mixerCrosspointArrays[i].size());
        mDto.crosspoints = gStorage.mixerCrosspointArrays[i].data();
    }
    snapshot.mixerCount = static_cast<uint32_t>(gStorage.mixerDTOs.size());
    snapshot.mixers = gStorage.mixerDTOs.data();

    // 4. Routers
    std::vector<const Node*> routerNodes;
    for (const auto& n : res.topology.nodes) {
        if (std::holds_alternative<RouterNode>(n.body)) {
            routerNodes.push_back(&n);
        }
    }
    const size_t rCount = routerNodes.size();
    gStorage.routerNames.resize(rCount);
    gStorage.routerInputPortArrays.resize(rCount);
    gStorage.routerOutputPortArrays.resize(rCount);
    gStorage.routerRouteArrays.resize(rCount);
    gStorage.routerBundleArrays.resize(rCount);
    gStorage.activeBundleArrays.resize(rCount);
    gStorage.routerDTOs.resize(rCount);

    for (size_t i = 0; i < rCount; ++i) {
        const auto* n = routerNodes[i];
        const auto& r = std::get<RouterNode>(n->body);
        gStorage.routerNames[i] = n->name;

        ASFWRouterDTO& rDto = gStorage.routerDTOs[i];
        rDto = {};
        rDto.nodeId = n->id.value;
        rDto.name = gStorage.routerNames[i].c_str();

        gStorage.routerInputPortArrays[i].clear();
        for (auto pid : r.inputs) gStorage.routerInputPortArrays[i].push_back(pid.value);
        rDto.inputPortCount = static_cast<uint32_t>(gStorage.routerInputPortArrays[i].size());
        rDto.inputPortIds = gStorage.routerInputPortArrays[i].data();

        gStorage.routerOutputPortArrays[i].clear();
        for (auto pid : r.outputs) gStorage.routerOutputPortArrays[i].push_back(pid.value);
        rDto.outputPortCount = static_cast<uint32_t>(gStorage.routerOutputPortArrays[i].size());
        rDto.outputPortIds = gStorage.routerOutputPortArrays[i].data();

        const size_t bCount = r.legalBundles.size();
        gStorage.routerRouteArrays[i].resize(bCount);
        gStorage.routerBundleArrays[i].resize(bCount);

        for (size_t b = 0; b < bCount; ++b) {
            const auto& bundle = r.legalBundles[b];
            const size_t routeCount = bundle.routes.size();
            gStorage.routerRouteArrays[i][b].resize(routeCount);
            for (size_t rt = 0; rt < routeCount; ++rt) {
                gStorage.routerRouteArrays[i][b][rt] = ASFWRouteDTO{
                    .inputPortId = bundle.routes[rt].input.value,
                    .outputPortId = bundle.routes[rt].output.value,
                };
            }
            gStorage.routerBundleArrays[i][b] = ASFWRouteBundleDTO{
                .bundleId = bundle.id.value,
                .routeCount = static_cast<uint32_t>(routeCount),
                .routes = gStorage.routerRouteArrays[i][b].data(),
            };
        }
        rDto.legalBundleCount = static_cast<uint32_t>(gStorage.routerBundleArrays[i].size());
        rDto.legalBundles = gStorage.routerBundleArrays[i].data();

        // Active bundles from state
        auto rIt = state.routers.find(n->id);
        if (rIt != state.routers.end()) {
            gStorage.activeBundleArrays[i].clear();
            for (const auto& aId : rIt->second.activeBundles) {
                gStorage.activeBundleArrays[i].push_back(aId.value);
            }
        } else {
            gStorage.activeBundleArrays[i].clear();
        }
        rDto.activeBundleCount = static_cast<uint32_t>(gStorage.activeBundleArrays[i].size());
        rDto.activeBundleIds = gStorage.activeBundleArrays[i].data();
    }
    snapshot.routerCount = static_cast<uint32_t>(gStorage.routerDTOs.size());
    snapshot.routers = gStorage.routerDTOs.data();

    // 5. Parameters
    const size_t pCount = res.topology.parameters.size();
    gStorage.paramNames.resize(pCount);
    gStorage.unitStrings.resize(pCount);
    gStorage.enumNames.resize(pCount);
    gStorage.enumItemArrays.resize(pCount);
    gStorage.paramDTOs.resize(pCount);

    for (size_t i = 0; i < pCount; ++i) {
        const auto& p = res.topology.parameters[i];
        gStorage.paramNames[i] = p.name;
        ASFWParameterDTO& dto = gStorage.paramDTOs[i];
        dto = {}; // Zero-initialize all fields to prevent stale pointers/data
        dto.id = p.id.value;
        dto.name = gStorage.paramNames[i].c_str();
        dto.semantic = toBridgeSemantic(p.semantic);
        dto.unit = "";

        std::visit([&](const auto& target) {
            using T = std::decay_t<decltype(target)>;
            if constexpr (std::is_same_v<T, NodeId>) {
                dto.targetKind = ASFW_TARGET_NODE;
                dto.targetId = target.value;
            } else if constexpr (std::is_same_v<T, PortId>) {
                dto.targetKind = ASFW_TARGET_PORT;
                dto.targetId = target.value;
            } else if constexpr (std::is_same_v<T, CrosspointId>) {
                dto.targetKind = ASFW_TARGET_CROSSPOINT;
                dto.targetId = target.value;
            }
        }, p.target);

        auto it = state.parameters.find(p.id);

        std::visit([&](const auto& dom) {
            using D = std::decay_t<decltype(dom)>;
            if constexpr (std::is_same_v<D, BooleanDomain>) {
                dto.kind = ASFW_PARAM_KIND_BOOLEAN;
                dto.boolValue = (it != state.parameters.end() && std::holds_alternative<bool>(it->second))
                    ? std::get<bool>(it->second) : false;
            } else if constexpr (std::is_same_v<D, ScalarDomain>) {
                dto.kind = ASFW_PARAM_KIND_SCALAR;
                dto.scalarMin = dom.min;
                dto.scalarMax = dom.max;
                dto.scalarStep = dom.step.value_or(1.0);
                dto.unit = dom.unit == ScalarUnit::Decibels ? "dB" : (dom.unit == ScalarUnit::Percent ? "%" : "");
                dto.scalarValue = (it != state.parameters.end() && std::holds_alternative<double>(it->second))
                    ? std::get<double>(it->second) : dom.min;
            } else if constexpr (std::is_same_v<D, EnumDomain>) {
                dto.kind = ASFW_PARAM_KIND_ENUM;
                dto.enumValue = (it != state.parameters.end() && std::holds_alternative<int64_t>(it->second))
                    ? std::get<int64_t>(it->second) : (dom.values.empty() ? 0 : dom.values.front().value);

                gStorage.enumNames[i].resize(dom.values.size());
                gStorage.enumItemArrays[i].resize(dom.values.size());
                for (size_t j = 0; j < dom.values.size(); ++j) {
                    gStorage.enumNames[i][j] = dom.values[j].name;
                    gStorage.enumItemArrays[i][j] = ASFWEnumItemDTO{
                        .value = dom.values[j].value,
                        .name = gStorage.enumNames[i][j].c_str(),
                    };
                }
                dto.enumItemCount = static_cast<uint32_t>(gStorage.enumItemArrays[i].size());
                dto.enumItems = gStorage.enumItemArrays[i].data();
            }
        }, p.domain);
    }
    snapshot.parameterCount = static_cast<uint32_t>(gStorage.paramDTOs.size());
    snapshot.parameters = gStorage.paramDTOs.data();

    // 6. Meters
    const size_t mCount = res.topology.meters.size();
    gStorage.meterNames.resize(mCount);
    gStorage.meterDTOs.resize(mCount);
    for (size_t i = 0; i < mCount; ++i) {
        const auto& m = res.topology.meters[i];
        gStorage.meterNames[i] = m.name;
        auto mIt = state.meters.find(m.id);
        double val = mIt != state.meters.end() ? mIt->second : m.domain.min;

        uint32_t targetPortId = 0;
        if (std::holds_alternative<PortId>(m.target)) {
            targetPortId = std::get<PortId>(m.target).value;
        } else if (std::holds_alternative<NodeId>(m.target)) {
            targetPortId = std::get<NodeId>(m.target).value;
        }

        gStorage.meterDTOs[i] = ASFWMeterDTO{
            .id = m.id.value,
            .name = gStorage.meterNames[i].c_str(),
            .targetPortId = targetPortId,
            .value = val,
            .min = m.domain.min,
            .max = m.domain.max,
        };
    }
    snapshot.meterCount = static_cast<uint32_t>(gStorage.meterDTOs.size());
    snapshot.meters = gStorage.meterDTOs.data();

    // 7. Presentation Groups
    const size_t gCount = pres.groups.size();
    gStorage.presGroupNames.resize(gCount);
    gStorage.presGroupNodeArrays.resize(gCount);
    gStorage.presGroupPortArrays.resize(gCount);
    gStorage.presGroupParamArrays.resize(gCount);
    gStorage.presGroupMeterArrays.resize(gCount);
    gStorage.presGroupDTOs.resize(gCount);

    for (size_t i = 0; i < gCount; ++i) {
        const auto& grp = pres.groups[i];
        gStorage.presGroupNames[i] = grp.name;
        ASFWPresentationGroupDTO& gDto = gStorage.presGroupDTOs[i];
        gDto = {};
        gDto.id = grp.id.value;
        gDto.name = gStorage.presGroupNames[i].c_str();
        gDto.kind = toBridgePresGroupKind(grp.kind);

        gStorage.presGroupNodeArrays[i].clear();
        for (auto nid : grp.nodes) gStorage.presGroupNodeArrays[i].push_back(nid.value);
        gDto.nodeCount = static_cast<uint32_t>(gStorage.presGroupNodeArrays[i].size());
        gDto.nodeIds = gStorage.presGroupNodeArrays[i].data();

        gStorage.presGroupPortArrays[i].clear();
        for (auto pid : grp.ports) gStorage.presGroupPortArrays[i].push_back(pid.value);
        gDto.portCount = static_cast<uint32_t>(gStorage.presGroupPortArrays[i].size());
        gDto.portIds = gStorage.presGroupPortArrays[i].data();

        gStorage.presGroupParamArrays[i].clear();
        for (auto pid : grp.parameters) gStorage.presGroupParamArrays[i].push_back(pid.value);
        gDto.parameterCount = static_cast<uint32_t>(gStorage.presGroupParamArrays[i].size());
        gDto.parameterIds = gStorage.presGroupParamArrays[i].data();

        gStorage.presGroupMeterArrays[i].clear();
        for (auto mid : grp.meters) gStorage.presGroupMeterArrays[i].push_back(mid.value);
        gDto.meterCount = static_cast<uint32_t>(gStorage.presGroupMeterArrays[i].size());
        gDto.meterIds = gStorage.presGroupMeterArrays[i].data();
    }
    snapshot.presentation.groupCount = static_cast<uint32_t>(gStorage.presGroupDTOs.size());
    snapshot.presentation.groups = gStorage.presGroupDTOs.data();

    // 8. Router Presentation Hints
    const size_t rhCount = pres.routers.size();
    gStorage.routerInGroupNames.resize(rhCount);
    gStorage.routerInGroupPortArrays.resize(rhCount);
    gStorage.routerInGroupDTOs.resize(rhCount);
    gStorage.routerOutGroupNames.resize(rhCount);
    gStorage.routerOutGroupPortArrays.resize(rhCount);
    gStorage.routerOutGroupDTOs.resize(rhCount);
    gStorage.routerBundleGroupNames.resize(rhCount);
    gStorage.routerBundleGroupBundleArrays.resize(rhCount);
    gStorage.routerBundleGroupDTOs.resize(rhCount);
    gStorage.routerHintDTOs.resize(rhCount);

    for (size_t i = 0; i < rhCount; ++i) {
        const auto& rh = pres.routers[i];
        ASFWRouterHintDTO& rhDto = gStorage.routerHintDTOs[i];
        rhDto = {};
        rhDto.routerNodeId = rh.router.value;
        rhDto.style = toBridgeRouterStyle(rh.style);

        // Input Groups
        const size_t igCount = rh.inputGroups.size();
        gStorage.routerInGroupNames[i].resize(igCount);
        gStorage.routerInGroupPortArrays[i].resize(igCount);
        gStorage.routerInGroupDTOs[i].resize(igCount);
        for (size_t g = 0; g < igCount; ++g) {
            gStorage.routerInGroupNames[i][g] = rh.inputGroups[g].name;
            gStorage.routerInGroupPortArrays[i][g].clear();
            for (auto pid : rh.inputGroups[g].ports) gStorage.routerInGroupPortArrays[i][g].push_back(pid.value);
            gStorage.routerInGroupDTOs[i][g] = ASFWPortGroupDTO{
                .name = gStorage.routerInGroupNames[i][g].c_str(),
                .portCount = static_cast<uint32_t>(gStorage.routerInGroupPortArrays[i][g].size()),
                .portIds = gStorage.routerInGroupPortArrays[i][g].data(),
            };
        }
        rhDto.inputGroupCount = static_cast<uint32_t>(gStorage.routerInGroupDTOs[i].size());
        rhDto.inputGroups = gStorage.routerInGroupDTOs[i].data();

        // Output Groups
        const size_t ogCount = rh.outputGroups.size();
        gStorage.routerOutGroupNames[i].resize(ogCount);
        gStorage.routerOutGroupPortArrays[i].resize(ogCount);
        gStorage.routerOutGroupDTOs[i].resize(ogCount);
        for (size_t g = 0; g < ogCount; ++g) {
            gStorage.routerOutGroupNames[i][g] = rh.outputGroups[g].name;
            gStorage.routerOutGroupPortArrays[i][g].clear();
            for (auto pid : rh.outputGroups[g].ports) gStorage.routerOutGroupPortArrays[i][g].push_back(pid.value);
            gStorage.routerOutGroupDTOs[i][g] = ASFWPortGroupDTO{
                .name = gStorage.routerOutGroupNames[i][g].c_str(),
                .portCount = static_cast<uint32_t>(gStorage.routerOutGroupPortArrays[i][g].size()),
                .portIds = gStorage.routerOutGroupPortArrays[i][g].data(),
            };
        }
        rhDto.outputGroupCount = static_cast<uint32_t>(gStorage.routerOutGroupDTOs[i].size());
        rhDto.outputGroups = gStorage.routerOutGroupDTOs[i].data();

        // Bundle Groups
        const size_t bgCount = rh.bundleGroups.size();
        gStorage.routerBundleGroupNames[i].resize(bgCount);
        gStorage.routerBundleGroupBundleArrays[i].resize(bgCount);
        gStorage.routerBundleGroupDTOs[i].resize(bgCount);
        for (size_t g = 0; g < bgCount; ++g) {
            gStorage.routerBundleGroupNames[i][g] = rh.bundleGroups[g].name;
            gStorage.routerBundleGroupBundleArrays[i][g].clear();
            for (auto bid : rh.bundleGroups[g].bundles) gStorage.routerBundleGroupBundleArrays[i][g].push_back(bid.value);
            gStorage.routerBundleGroupDTOs[i][g] = ASFWBundleGroupDTO{
                .name = gStorage.routerBundleGroupNames[i][g].c_str(),
                .bundleCount = static_cast<uint32_t>(gStorage.routerBundleGroupBundleArrays[i][g].size()),
                .bundleIds = gStorage.routerBundleGroupBundleArrays[i][g].data(),
            };
        }
        rhDto.bundleGroupCount = static_cast<uint32_t>(gStorage.routerBundleGroupDTOs[i].size());
        rhDto.bundleGroups = gStorage.routerBundleGroupDTOs[i].data();
    }
    snapshot.presentation.routerHintCount = static_cast<uint32_t>(gStorage.routerHintDTOs.size());
    snapshot.presentation.routerHints = gStorage.routerHintDTOs.data();

    // 9. Mixer Presentation Hints
    const size_t mhCount = pres.mixers.size();
    gStorage.mixerInGroupNames.resize(mhCount);
    gStorage.mixerInGroupPortArrays.resize(mhCount);
    gStorage.mixerInGroupDTOs.resize(mhCount);
    gStorage.mixerOutGroupNames.resize(mhCount);
    gStorage.mixerOutGroupPortArrays.resize(mhCount);
    gStorage.mixerOutGroupDTOs.resize(mhCount);
    gStorage.mixerHintDTOs.resize(mhCount);

    for (size_t i = 0; i < mhCount; ++i) {
        const auto& mh = pres.mixers[i];
        ASFWMixerHintDTO& mhDto = gStorage.mixerHintDTOs[i];
        mhDto = {};
        mhDto.mixerNodeId = mh.mixer.value;
        mhDto.style = toBridgeMixerStyle(mh.style);

        // Input Groups
        const size_t igCount = mh.inputGroups.size();
        gStorage.mixerInGroupNames[i].resize(igCount);
        gStorage.mixerInGroupPortArrays[i].resize(igCount);
        gStorage.mixerInGroupDTOs[i].resize(igCount);
        for (size_t g = 0; g < igCount; ++g) {
            gStorage.mixerInGroupNames[i][g] = mh.inputGroups[g].name;
            gStorage.mixerInGroupPortArrays[i][g].clear();
            for (auto pid : mh.inputGroups[g].ports) gStorage.mixerInGroupPortArrays[i][g].push_back(pid.value);
            gStorage.mixerInGroupDTOs[i][g] = ASFWPortGroupDTO{
                .name = gStorage.mixerInGroupNames[i][g].c_str(),
                .portCount = static_cast<uint32_t>(gStorage.mixerInGroupPortArrays[i][g].size()),
                .portIds = gStorage.mixerInGroupPortArrays[i][g].data(),
            };
        }
        mhDto.inputGroupCount = static_cast<uint32_t>(gStorage.mixerInGroupDTOs[i].size());
        mhDto.inputGroups = gStorage.mixerInGroupDTOs[i].data();

        // Output Groups
        const size_t ogCount = mh.outputGroups.size();
        gStorage.mixerOutGroupNames[i].resize(ogCount);
        gStorage.mixerOutGroupPortArrays[i].resize(ogCount);
        gStorage.mixerOutGroupDTOs[i].resize(ogCount);
        for (size_t g = 0; g < ogCount; ++g) {
            gStorage.mixerOutGroupNames[i][g] = mh.outputGroups[g].name;
            gStorage.mixerOutGroupPortArrays[i][g].clear();
            for (auto pid : mh.outputGroups[g].ports) gStorage.mixerOutGroupPortArrays[i][g].push_back(pid.value);
            gStorage.mixerOutGroupDTOs[i][g] = ASFWPortGroupDTO{
                .name = gStorage.mixerOutGroupNames[i][g].c_str(),
                .portCount = static_cast<uint32_t>(gStorage.mixerOutGroupPortArrays[i][g].size()),
                .portIds = gStorage.mixerOutGroupPortArrays[i][g].data(),
            };
        }
        mhDto.outputGroupCount = static_cast<uint32_t>(gStorage.mixerOutGroupDTOs[i].size());
        mhDto.outputGroups = gStorage.mixerOutGroupDTOs[i].data();
    }
    snapshot.presentation.mixerHintCount = static_cast<uint32_t>(gStorage.mixerHintDTOs.size());
    snapshot.presentation.mixerHints = gStorage.mixerHintDTOs.data();

    // 10. Parameter Presentation Hints
    const size_t phCount = pres.parameters.size();
    gStorage.paramSectionNames.resize(phCount);
    gStorage.parameterHintDTOs.resize(phCount);
    for (size_t i = 0; i < phCount; ++i) {
        const auto& ph = pres.parameters[i];
        gStorage.paramSectionNames[i] = ph.section;
        gStorage.parameterHintDTOs[i] = ASFWParameterHintDTO{
            .parameterId = ph.parameter.value,
            .placement = toBridgeControlPlacement(ph.placement),
            .section = gStorage.paramSectionNames[i].c_str(),
        };
    }
    snapshot.presentation.parameterHintCount = static_cast<uint32_t>(gStorage.parameterHintDTOs.size());
    snapshot.presentation.parameterHints = gStorage.parameterHintDTOs.data();

    return snapshot;
}

#endif // !TARGET_OS_DRIVERKIT
