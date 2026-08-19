import SwiftUI

// MARK: - Swift Snapshot DTOs

struct EnumItemModel: Identifiable {
    let value: Int64
    let name: String
    var id: Int64 { value }
}

struct ParameterModel: Identifiable {
    let id: UInt32
    let name: String
    let kind: ASFWParamKind
    var scalarValue: Double
    let scalarMin: Double
    let scalarMax: Double
    let scalarStep: Double
    let unit: String
    var boolValue: Bool
    var enumValue: Int64
    let enumItems: [EnumItemModel]
}

struct RouteModel: Identifiable {
    var id: String { "\(inputPortId)->\(outputPortId)" }
    let inputPortId: UInt32
    let outputPortId: UInt32
}

struct RouteBundleModel: Identifiable {
    let id: UInt32
    let routes: [RouteModel]
}

struct RouterModel: Identifiable {
    let id: UInt32
    let name: String
    let legalBundles: [RouteBundleModel]
    var activeBundleIds: Set<UInt32>
}

struct MeterModel: Identifiable {
    let id: UInt32
    let name: String
    let value: Double
    let min: Double
    let max: Double
}

struct LabDeviceSnapshot {
    let revision: UInt64
    let deviceKind: ASFWVirtualDeviceKind
    let manufacturer: String
    let model: String
    let currentSampleRate: UInt32
    let opticalInput: ASFWOpticalMode
    let opticalOutput: ASFWOpticalMode
    let supportedSampleRates: [UInt32]
    let hasOptical: Bool
    let totalCaptureChannels: UInt32
    let totalPlaybackChannels: UInt32
    let nodeCount: UInt32
    let portCount: UInt32
    let linkCount: UInt32
    let parameters: [ParameterModel]
    let routers: [RouterModel]
    let meters: [MeterModel]
}

// MARK: - Observable Lab State

final class VirtualLabState: ObservableObject {
    @Published var snapshot: LabDeviceSnapshot?

    init() {
        asfw_lab_init()
        refresh()
    }

    func refresh() {
        let dto = asfw_lab_get_snapshot()

        var rates: [UInt32] = []
        if let ratePtr = dto.supportedSampleRates {
            for i in 0..<Int(dto.supportedSampleRateCount) {
                rates.append(ratePtr[i])
            }
        }

        var params: [ParameterModel] = []
        if let pPtr = dto.parameters {
            for i in 0..<Int(dto.parameterCount) {
                let p = pPtr[i]
                var items: [EnumItemModel] = []
                if let iPtr = p.enumItems {
                    for j in 0..<Int(p.enumItemCount) {
                        items.append(EnumItemModel(value: iPtr[j].value, name: String(cString: iPtr[j].name)))
                    }
                }
                params.append(ParameterModel(
                    id: p.id,
                    name: String(cString: p.name),
                    kind: p.kind,
                    scalarValue: p.scalarValue,
                    scalarMin: p.scalarMin,
                    scalarMax: p.scalarMax,
                    scalarStep: p.scalarStep,
                    unit: String(cString: p.unit),
                    boolValue: p.boolValue,
                    enumValue: p.enumValue,
                    enumItems: items
                ))
            }
        }

        var routers: [RouterModel] = []
        if let rPtr = dto.routers {
            for i in 0..<Int(dto.routerCount) {
                let r = rPtr[i]
                var bundles: [RouteBundleModel] = []
                if let bPtr = r.legalBundles {
                    for j in 0..<Int(r.legalBundleCount) {
                        let b = bPtr[j]
                        var routes: [RouteModel] = []
                        if let rtPtr = b.routes {
                            for k in 0..<Int(b.routeCount) {
                                routes.append(RouteModel(inputPortId: rtPtr[k].inputPortId, outputPortId: rtPtr[k].outputPortId))
                            }
                        }
                        bundles.append(RouteBundleModel(id: b.bundleId, routes: routes))
                    }
                }
                var activeSet = Set<UInt32>()
                if let actPtr = r.activeBundleIds {
                    for j in 0..<Int(r.activeBundleCount) {
                        activeSet.insert(actPtr[j])
                    }
                }
                routers.append(RouterModel(
                    id: r.nodeId,
                    name: String(cString: r.name),
                    legalBundles: bundles,
                    activeBundleIds: activeSet
                ))
            }
        }

        var meters: [MeterModel] = []
        if let mPtr = dto.meters {
            for i in 0..<Int(dto.meterCount) {
                let m = mPtr[i]
                meters.append(MeterModel(
                    id: m.id,
                    name: String(cString: m.name),
                    value: m.value,
                    min: m.min,
                    max: m.max
                ))
            }
        }

        snapshot = LabDeviceSnapshot(
            revision: dto.revision,
            deviceKind: dto.deviceKind,
            manufacturer: dto.manufacturer != nil ? String(cString: dto.manufacturer) : "Unknown",
            model: dto.model != nil ? String(cString: dto.model) : "Unknown",
            currentSampleRate: dto.currentSampleRate,
            opticalInput: dto.opticalInput,
            opticalOutput: dto.opticalOutput,
            supportedSampleRates: rates,
            hasOptical: dto.hasOptical,
            totalCaptureChannels: dto.totalCaptureChannels,
            totalPlaybackChannels: dto.totalPlaybackChannels,
            nodeCount: dto.nodeCount,
            portCount: dto.portCount,
            linkCount: dto.linkCount,
            parameters: params,
            routers: routers,
            meters: meters
        )
    }

    func selectDevice(_ kind: ASFWVirtualDeviceKind) {
        if asfw_lab_select_device(kind) {
            refresh()
        }
    }

    func setSampleRate(_ rate: UInt32) {
        guard let snap = snapshot else { return }
        if asfw_lab_set_configuration(rate, snap.opticalInput, snap.opticalOutput) {
            refresh()
        }
    }

    func setOpticalMode(input: ASFWOpticalMode, output: ASFWOpticalMode) {
        guard let snap = snapshot else { return }
        if asfw_lab_set_configuration(snap.currentSampleRate, input, output) {
            refresh()
        }
    }

    func setParameterScalar(id: UInt32, value: Double) {
        if asfw_lab_set_parameter_scalar(id, value) {
            refresh()
        }
    }

    func setParameterBool(id: UInt32, value: Bool) {
        if asfw_lab_set_parameter_bool(id, value) {
            refresh()
        }
    }

    func setParameterEnum(id: UInt32, value: Int64) {
        if asfw_lab_set_parameter_enum(id, value) {
            refresh()
        }
    }

    func toggleRouterBundle(routerNodeId: UInt32, bundleId: UInt32) {
        guard let snap = snapshot, let router = snap.routers.first(where: { $0.id == routerNodeId }) else { return }
        var active = router.activeBundleIds
        if active.contains(bundleId) {
            active.remove(bundleId)
        } else {
            active.insert(bundleId)
        }
        let array = Array(active)
        array.withUnsafeBufferPointer { ptr in
            _ = asfw_lab_set_active_route_bundles(routerNodeId, ptr.baseAddress, UInt32(array.count))
        }
        refresh()
    }
}

// MARK: - Generic UI Views (Zero device-model branching!)

struct GenericLabDashboardView: View {
    @StateObject private var state = VirtualLabState()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                if let snap = state.snapshot {
                    // 1. Device Header & Selector
                    DeviceSelectorSection(snap: snap, state: state)

                    Divider()

                    // 2. Configuration & Streams
                    DeviceConfigurationSection(snap: snap, state: state)

                    Divider()

                    // 3. Signal Flow Summary
                    SignalFlowSummarySection(snap: snap)

                    Divider()

                    // 4. Controls
                    if !snap.parameters.isEmpty {
                        ControlsSection(parameters: snap.parameters, state: state)
                        Divider()
                    }

                    // 5. Routers
                    if !snap.routers.isEmpty {
                        RoutersSection(routers: snap.routers, state: state)
                        Divider()
                    }

                    // 6. Meters
                    if !snap.meters.isEmpty {
                        MetersSection(meters: snap.meters)
                    }
                } else {
                    ProgressView("Loading virtual audio device…")
                }
            }
            .padding()
        }
    }
}

struct DeviceSelectorSection: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        HStack {
            VStack(alignment: .leading) {
                Text("\(snap.manufacturer) \(snap.model)")
                    .font(.title)
                    .bold()
                Text("Revision: \(snap.revision) • Pure Generic Projection")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Picker("Device", selection: Binding(
                get: { snap.deviceKind },
                set: { state.selectDevice($0) }
            )) {
                Text("Apogee Duet").tag(ASFW_VIRTUAL_DEVICE_DUET)
                Text("TerraTec PHASE 88").tag(ASFW_VIRTUAL_DEVICE_PHASE88)
                Text("M-Audio FW1814").tag(ASFW_VIRTUAL_DEVICE_FW1814)
                Text("Saffire Pro 24 DSP").tag(ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP)
            }
            .pickerStyle(.segmented)
            .frame(width: 480)
        }
    }
}

struct DeviceConfigurationSection: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Configuration & Stream Waists")
                .font(.headline)

            HStack(spacing: 24) {
                Picker("Sample Rate", selection: Binding(
                    get: { snap.currentSampleRate },
                    set: { state.setSampleRate($0) }
                )) {
                    ForEach(snap.supportedSampleRates, id: \.self) { rate in
                        Text("\(rate) Hz").tag(rate)
                    }
                }
                .frame(width: 200)

                if snap.hasOptical {
                    Picker("Optical In", selection: Binding(
                        get: { snap.opticalInput },
                        set: { state.setOpticalMode(input: $0, output: snap.opticalOutput) }
                    )) {
                        Text("ADAT (8 ch)").tag(ASFW_OPTICAL_ADAT)
                        Text("S/PDIF (2 ch)").tag(ASFW_OPTICAL_SPDIF)
                    }
                    .frame(width: 180)

                    Picker("Optical Out", selection: Binding(
                        get: { snap.opticalOutput },
                        set: { state.setOpticalMode(input: snap.opticalInput, output: $0) }
                    )) {
                        Text("ADAT (8 ch)").tag(ASFW_OPTICAL_ADAT)
                        Text("S/PDIF (2 ch)").tag(ASFW_OPTICAL_SPDIF)
                    }
                    .frame(width: 180)
                }

                Spacer()

                VStack(alignment: .trailing) {
                    Text("Capture: \(snap.totalCaptureChannels) ch • Playback: \(snap.totalPlaybackChannels) ch")
                        .font(.callout)
                        .bold()
                }
            }
        }
    }
}

struct SignalFlowSummarySection: View {
    let snap: LabDeviceSnapshot

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Topology Structural Summary")
                .font(.headline)
            HStack(spacing: 32) {
                Label("\(snap.nodeCount) Nodes", systemImage: "square.grid.2x2")
                Label("\(snap.portCount) Ports", systemImage: "circle.circle")
                Label("\(snap.linkCount) Fixed Links", systemImage: "arrow.right.circle")
                Label("\(snap.parameters.count) Parameters", systemImage: "slider.horizontal.3")
                Label("\(snap.routers.count) Routers", systemImage: "point.3.filled.connected.trianglepath.dotted")
            }
            .font(.callout)
            .foregroundStyle(.secondary)
        }
    }
}

struct ControlsSection: View {
    let parameters: [ParameterModel]
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Controls & Parameters")
                .font(.headline)

            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 16) {
                ForEach(parameters) { param in
                    VStack(alignment: .leading, spacing: 4) {
                        Text(param.name)
                            .font(.caption)
                            .foregroundStyle(.secondary)

                        switch param.kind {
                        case ASFW_PARAM_KIND_BOOLEAN:
                            Toggle(param.name, isOn: Binding(
                                get: { param.boolValue },
                                set: { state.setParameterBool(id: param.id, value: $0) }
                            ))
                            .labelsHidden()

                        case ASFW_PARAM_KIND_SCALAR:
                            HStack {
                                Slider(
                                    value: Binding(
                                        get: { param.scalarValue },
                                        set: { state.setParameterScalar(id: param.id, value: $0) }
                                    ),
                                    in: param.scalarMin...param.scalarMax,
                                    step: param.scalarStep
                                )
                                Text(String(format: "%.1f %@", param.scalarValue, param.unit))
                                    .font(.caption.monospacedDigit())
                                    .frame(width: 70, alignment: .trailing)
                            }

                        case ASFW_PARAM_KIND_ENUM:
                            Picker(param.name, selection: Binding(
                                get: { param.enumValue },
                                set: { state.setParameterEnum(id: param.id, value: $0) }
                            )) {
                                ForEach(param.enumItems) { item in
                                    Text(item.name).tag(item.value)
                                }
                            }
                            .labelsHidden()

                        default:
                            EmptyView()
                        }
                    }
                    .padding(8)
                    .background(Color.secondary.opacity(0.1))
                    .cornerRadius(8)
                }
            }
        }
    }
}

struct RoutersSection: View {
    let routers: [RouterModel]
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Signal Routers")
                .font(.headline)

            ForEach(routers) { router in
                VStack(alignment: .leading, spacing: 6) {
                    Text(router.name)
                        .font(.subheadline)
                        .bold()

                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 8) {
                            ForEach(router.legalBundles) { bundle in
                                let isActive = router.activeBundleIds.contains(bundle.id)
                                Button(action: {
                                    state.toggleRouterBundle(routerNodeId: router.id, bundleId: bundle.id)
                                }) {
                                    Text("Bundle #\(bundle.id) (\(bundle.routes.count) rts)")
                                        .font(.caption)
                                        .padding(.horizontal, 8)
                                        .padding(.vertical, 4)
                                        .background(isActive ? Color.accentColor : Color.secondary.opacity(0.2))
                                        .foregroundColor(isActive ? .white : .primary)
                                        .cornerRadius(6)
                                }
                                .buttonStyle(.plain)
                            }
                        }
                    }
                }
                .padding(8)
                .background(Color.secondary.opacity(0.05))
                .cornerRadius(8)
            }
        }
    }
}

struct MetersSection: View {
    let meters: [MeterModel]

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Telemetry Meters")
                .font(.headline)

            HStack(spacing: 16) {
                ForEach(meters) { meter in
                    VStack {
                        ProgressView(value: max(0.0, meter.value - meter.min), total: max(1.0, meter.max - meter.min))
                        Text(meter.name)
                            .font(.caption2)
                            .lineLimit(1)
                    }
                    .frame(maxWidth: 160)
                }
            }
        }
    }
}
