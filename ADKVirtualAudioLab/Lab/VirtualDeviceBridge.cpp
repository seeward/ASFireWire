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
using namespace ASFW::Runtime;

namespace {

std::mutex gMutex;
std::unique_ptr<VirtualDeviceRuntime> gRuntime;

// Storage backing the DTO snapshot strings and arrays
struct SnapshotStorage {
    std::string manufacturer;
    std::string model;
    std::vector<uint32_t> supportedRates;

    std::vector<std::string> paramNames;
    std::vector<std::string> unitStrings;
    std::vector<std::vector<std::string>> enumNames;
    std::vector<std::vector<ASFWEnumItemDTO>> enumItemArrays;
    std::vector<ASFWParameterDTO> paramDTOs;

    std::vector<std::string> routerNames;
    std::vector<std::vector<std::vector<ASFWRouteDTO>>> routerRouteArrays;
    std::vector<std::vector<ASFWRouteBundleDTO>> routerBundleArrays;
    std::vector<std::vector<uint32_t>> activeBundleArrays;
    std::vector<ASFWRouterDTO> routerDTOs;

    std::vector<std::string> meterNames;
    std::vector<ASFWMeterDTO> meterDTOs;
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

std::optional<OpticalMode> fromBridgeOptical(ASFWOpticalMode mode) {
    switch (mode) {
        case ASFW_OPTICAL_ADAT: return OpticalMode::Adat;
        case ASFW_OPTICAL_SPDIF: return OpticalMode::Spdif;
        case ASFW_OPTICAL_NONE: return std::nullopt;
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

    ASFWDeviceSnapshotDTO snapshot{};
    if (!gRuntime) return snapshot;

    snapshot.revision = gRuntime->revision();

    switch (gRuntime->kind()) {
        case VirtualDeviceKind::Duet: snapshot.deviceKind = ASFW_VIRTUAL_DEVICE_DUET; break;
        case VirtualDeviceKind::Phase88: snapshot.deviceKind = ASFW_VIRTUAL_DEVICE_PHASE88; break;
        case VirtualDeviceKind::FW1814: snapshot.deviceKind = ASFW_VIRTUAL_DEVICE_FW1814; break;
        case VirtualDeviceKind::SaffirePro24DSP: snapshot.deviceKind = ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP; break;
    }

    const auto& caps = gRuntime->capabilities();
    const auto& cfg = gRuntime->configuration();
    const auto& res = gRuntime->resolved();
    const auto& state = gRuntime->state();

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

    snapshot.nodeCount = static_cast<uint32_t>(res.topology.nodes.size());
    snapshot.portCount = static_cast<uint32_t>(res.topology.ports.size());
    snapshot.linkCount = static_cast<uint32_t>(res.topology.fixedLinks.size());

    // Parameters
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
        dto.id = p.id.value;
        dto.name = gStorage.paramNames[i].c_str();

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
                dto.unit = dom.unit == ScalarUnit::Decibels ? "dB" : "";
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

    // Routers
    std::vector<const Node*> routerNodes;
    for (const auto& n : res.topology.nodes) {
        if (std::holds_alternative<RouterNode>(n.body)) {
            routerNodes.push_back(&n);
        }
    }
    const size_t rCount = routerNodes.size();
    gStorage.routerNames.resize(rCount);
    gStorage.routerRouteArrays.resize(rCount);
    gStorage.routerBundleArrays.resize(rCount);
    gStorage.activeBundleArrays.resize(rCount);
    gStorage.routerDTOs.resize(rCount);

    for (size_t i = 0; i < rCount; ++i) {
        const auto* n = routerNodes[i];
        const auto& r = std::get<RouterNode>(n->body);
        gStorage.routerNames[i] = n->name;

        ASFWRouterDTO& rDto = gStorage.routerDTOs[i];
        rDto.nodeId = n->id.value;
        rDto.name = gStorage.routerNames[i].c_str();

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

    // Meters
    const size_t mCount = res.topology.meters.size();
    gStorage.meterNames.resize(mCount);
    gStorage.meterDTOs.resize(mCount);
    for (size_t i = 0; i < mCount; ++i) {
        const auto& m = res.topology.meters[i];
        gStorage.meterNames[i] = m.name;
        auto mIt = state.meters.find(m.id);
        double val = mIt != state.meters.end() ? mIt->second : m.domain.min;

        gStorage.meterDTOs[i] = ASFWMeterDTO{
            .id = m.id.value,
            .name = gStorage.meterNames[i].c_str(),
            .value = val,
            .min = m.domain.min,
            .max = m.domain.max,
        };
    }
    snapshot.meterCount = static_cast<uint32_t>(gStorage.meterDTOs.size());
    snapshot.meters = gStorage.meterDTOs.data();

    return snapshot;
}

#endif // !TARGET_OS_DRIVERKIT
