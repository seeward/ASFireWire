import SwiftUI

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

        var nodes: [NodeModel] = []
        if let nPtr = dto.nodes {
            for i in 0..<Int(dto.nodeCount) {
                let n = nPtr[i]
                var inPorts: [UInt32] = []
                if let ipPtr = n.inputPortIds {
                    for j in 0..<Int(n.inputPortCount) { inPorts.append(ipPtr[j]) }
                }
                var outPorts: [UInt32] = []
                if let opPtr = n.outputPortIds {
                    for j in 0..<Int(n.outputPortCount) { outPorts.append(opPtr[j]) }
                }
                let nName = n.name != nil ? String(cString: n.name) : "Node \(n.nodeId)"
                nodes.append(NodeModel(
                    id: n.nodeId,
                    name: nName,
                    kind: n.kind,
                    inputPortIds: inPorts,
                    outputPortIds: outPorts
                ))
            }
        }

        var ports: [PortModel] = []
        if let ptPtr = dto.ports {
            for i in 0..<Int(dto.portCount) {
                let pt = ptPtr[i]
                let ptName = pt.name != nil ? String(cString: pt.name) : "Port \(pt.id)"
                ports.append(PortModel(
                    id: pt.id,
                    name: ptName,
                    ownerNodeId: pt.ownerNodeId,
                    direction: pt.direction,
                    channels: pt.channels,
                    signalKind: pt.signalKind,
                    signalIndex: pt.signalIndex
                ))
            }
        }

        var mixers: [MixerModel] = []
        if let mPtr = dto.mixers {
            for i in 0..<Int(dto.mixerCount) {
                let m = mPtr[i]
                var inPorts: [UInt32] = []
                if let ipPtr = m.inputPortIds {
                    for j in 0..<Int(m.inputPortCount) { inPorts.append(ipPtr[j]) }
                }
                var outPorts: [UInt32] = []
                if let opPtr = m.outputPortIds {
                    for j in 0..<Int(m.outputPortCount) { outPorts.append(opPtr[j]) }
                }
                var cps: [MixerCrosspointModel] = []
                if let cpPtr = m.crosspoints {
                    for j in 0..<Int(m.crosspointCount) {
                        cps.append(MixerCrosspointModel(
                            id: cpPtr[j].id,
                            inputPortId: cpPtr[j].inputPortId,
                            outputPortId: cpPtr[j].outputPortId
                        ))
                    }
                }
                let mName = m.name != nil ? String(cString: m.name) : "Mixer \(m.nodeId)"
                mixers.append(MixerModel(
                    id: m.nodeId,
                    name: mName,
                    inputPortIds: inPorts,
                    outputPortIds: outPorts,
                    crosspoints: cps
                ))
            }
        }

        var routers: [RouterModel] = []
        if let rPtr = dto.routers {
            for i in 0..<Int(dto.routerCount) {
                let r = rPtr[i]
                var inPorts: [UInt32] = []
                if let ipPtr = r.inputPortIds {
                    for j in 0..<Int(r.inputPortCount) { inPorts.append(ipPtr[j]) }
                }
                var outPorts: [UInt32] = []
                if let opPtr = r.outputPortIds {
                    for j in 0..<Int(r.outputPortCount) { outPorts.append(opPtr[j]) }
                }
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
                        let label = b.sourceLabel != nil ? String(cString: b.sourceLabel) : ""
                        bundles.append(RouteBundleModel(id: b.bundleId, routes: routes, sourceLabel: label))
                    }
                }
                var activeSet = Set<UInt32>()
                if let actPtr = r.activeBundleIds {
                    for j in 0..<Int(r.activeBundleCount) {
                        activeSet.insert(actPtr[j])
                    }
                }
                let rName = r.name != nil ? String(cString: r.name) : "Router \(r.nodeId)"
                routers.append(RouterModel(
                    id: r.nodeId,
                    name: rName,
                    inputPortIds: inPorts,
                    outputPortIds: outPorts,
                    legalBundles: bundles,
                    activeBundleIds: activeSet
                ))
            }
        }

        var params: [ParameterModel] = []
        if let pPtr = dto.parameters {
            for i in 0..<Int(dto.parameterCount) {
                let p = pPtr[i]
                var items: [EnumItemModel] = []
                if let iPtr = p.enumItems {
                    for j in 0..<Int(p.enumItemCount) {
                        let itemName = iPtr[j].name != nil ? String(cString: iPtr[j].name) : ""
                        items.append(EnumItemModel(value: iPtr[j].value, name: itemName))
                    }
                }
                let pName = p.name != nil ? String(cString: p.name) : "Parameter \(p.id)"
                let pUnit = p.unit != nil ? String(cString: p.unit) : ""
                params.append(ParameterModel(
                    id: p.id,
                    name: pName,
                    kind: p.kind,
                    semantic: p.semantic,
                    targetKind: p.targetKind,
                    targetId: p.targetId,
                    scalarValue: p.scalarValue,
                    scalarMin: p.scalarMin,
                    scalarMax: p.scalarMax,
                    scalarStep: p.scalarStep,
                    unit: pUnit,
                    boolValue: p.boolValue,
                    enumValue: p.enumValue,
                    enumItems: items
                ))
            }
        }

        var meters: [MeterModel] = []
        if let mPtr = dto.meters {
            for i in 0..<Int(dto.meterCount) {
                let m = mPtr[i]
                let mName = m.name != nil ? String(cString: m.name) : "Meter \(m.id)"
                meters.append(MeterModel(
                    id: m.id,
                    name: mName,
                    targetPortId: m.targetPortId,
                    value: m.value,
                    min: m.min,
                    max: m.max
                ))
            }
        }

        // Presentation Metadata
        var presGroups: [PresentationGroupModel] = []
        if let gPtr = dto.presentation.groups {
            for i in 0..<Int(dto.presentation.groupCount) {
                let g = gPtr[i]
                let gName = g.name != nil ? String(cString: g.name) : "Group \(g.id)"

                var nIds: [UInt32] = []
                if let nIdPtr = g.nodeIds { for j in 0..<Int(g.nodeCount) { nIds.append(nIdPtr[j]) } }

                var ptIds: [UInt32] = []
                if let ptIdPtr = g.portIds { for j in 0..<Int(g.portCount) { ptIds.append(ptIdPtr[j]) } }

                var pIds: [UInt32] = []
                if let pIdPtr = g.parameterIds { for j in 0..<Int(g.parameterCount) { pIds.append(pIdPtr[j]) } }

                var mIds: [UInt32] = []
                if let mIdPtr = g.meterIds { for j in 0..<Int(g.meterCount) { mIds.append(mIdPtr[j]) } }

                presGroups.append(PresentationGroupModel(
                    id: g.id,
                    name: gName,
                    kind: g.kind,
                    nodeIds: nIds,
                    portIds: ptIds,
                    parameterIds: pIds,
                    meterIds: mIds
                ))
            }
        }

        var routerHints: [RouterHintModel] = []
        if let rhPtr = dto.presentation.routerHints {
            for i in 0..<Int(dto.presentation.routerHintCount) {
                let rh = rhPtr[i]

                var inGrps: [PortGroupModel] = []
                if let igPtr = rh.inputGroups {
                    for j in 0..<Int(rh.inputGroupCount) {
                        let ig = igPtr[j]
                        let igName = ig.name != nil ? String(cString: ig.name) : ""
                        var ptIds: [UInt32] = []
                        if let ptIdPtr = ig.portIds { for k in 0..<Int(ig.portCount) { ptIds.append(ptIdPtr[k]) } }
                        inGrps.append(PortGroupModel(name: igName, portIds: ptIds))
                    }
                }

                var outGrps: [PortGroupModel] = []
                if let ogPtr = rh.outputGroups {
                    for j in 0..<Int(rh.outputGroupCount) {
                        let og = ogPtr[j]
                        let ogName = og.name != nil ? String(cString: og.name) : ""
                        var ptIds: [UInt32] = []
                        if let ptIdPtr = og.portIds { for k in 0..<Int(og.portCount) { ptIds.append(ptIdPtr[k]) } }
                        outGrps.append(PortGroupModel(name: ogName, portIds: ptIds))
                    }
                }

                var bndlGrps: [BundleGroupModel] = []
                if let bgPtr = rh.bundleGroups {
                    for j in 0..<Int(rh.bundleGroupCount) {
                        let bg = bgPtr[j]
                        let bgName = bg.name != nil ? String(cString: bg.name) : ""
                        var bIds: [UInt32] = []
                        if let bIdPtr = bg.bundleIds { for k in 0..<Int(bg.bundleCount) { bIds.append(bIdPtr[k]) } }
                        bndlGrps.append(BundleGroupModel(name: bgName, bundleIds: bIds))
                    }
                }

                routerHints.append(RouterHintModel(
                    routerNodeId: rh.routerNodeId,
                    style: rh.style,
                    inputGroups: inGrps,
                    outputGroups: outGrps,
                    bundleGroups: bndlGrps
                ))
            }
        }

        var mixerHints: [MixerHintModel] = []
        if let mhPtr = dto.presentation.mixerHints {
            for i in 0..<Int(dto.presentation.mixerHintCount) {
                let mh = mhPtr[i]

                var inGrps: [PortGroupModel] = []
                if let igPtr = mh.inputGroups {
                    for j in 0..<Int(mh.inputGroupCount) {
                        let ig = igPtr[j]
                        let igName = ig.name != nil ? String(cString: ig.name) : ""
                        var ptIds: [UInt32] = []
                        if let ptIdPtr = ig.portIds { for k in 0..<Int(ig.portCount) { ptIds.append(ptIdPtr[k]) } }
                        inGrps.append(PortGroupModel(name: igName, portIds: ptIds))
                    }
                }

                var outGrps: [PortGroupModel] = []
                if let ogPtr = mh.outputGroups {
                    for j in 0..<Int(mh.outputGroupCount) {
                        let og = ogPtr[j]
                        let ogName = og.name != nil ? String(cString: og.name) : ""
                        var ptIds: [UInt32] = []
                        if let ptIdPtr = og.portIds { for k in 0..<Int(og.portCount) { ptIds.append(ptIdPtr[k]) } }
                        outGrps.append(PortGroupModel(name: ogName, portIds: ptIds))
                    }
                }

                mixerHints.append(MixerHintModel(
                    mixerNodeId: mh.mixerNodeId,
                    style: mh.style,
                    inputGroups: inGrps,
                    outputGroups: outGrps
                ))
            }
        }

        var paramHints: [ParameterHintModel] = []
        if let phPtr = dto.presentation.parameterHints {
            for i in 0..<Int(dto.presentation.parameterHintCount) {
                let ph = phPtr[i]
                let sec = ph.section != nil ? String(cString: ph.section) : ""
                paramHints.append(ParameterHintModel(
                    parameterId: ph.parameterId,
                    placement: ph.placement,
                    presentation: ph.presentation,
                    section: sec
                ))
            }
        }

        var channels: [ChannelModel] = []
        if let chPtr = dto.channels {
            for i in 0..<Int(dto.channelCount) {
                let ch = chPtr[i]
                let chName = ch.name != nil ? String(cString: ch.name) : "Channel \(ch.id)"
                var ptIds: [UInt32] = []
                if let ptIdPtr = ch.portIds {
                    for j in 0..<Int(ch.portCount) {
                        ptIds.append(ptIdPtr[j])
                    }
                }
                channels.append(ChannelModel(id: ch.id, name: chName, portIds: ptIds))
            }
        }

        var buses: [BusModel] = []
        if let bPtr = dto.buses {
            for i in 0..<Int(dto.busCount) {
                let bus = bPtr[i]
                let bName = bus.name != nil ? String(cString: bus.name) : "Bus \(bus.id)"
                var ptIds: [UInt32] = []
                if let ptIdPtr = bus.portIds {
                    for j in 0..<Int(bus.portCount) {
                        ptIds.append(ptIdPtr[j])
                    }
                }
                buses.append(BusModel(id: bus.id, semantic: bus.semantic, name: bName, portIds: ptIds))
            }
        }

        let presModel = DevicePresentationModel(
            groups: presGroups,
            routerHints: routerHints,
            mixerHints: mixerHints,
            parameterHints: paramHints
        )

        let mfr = dto.manufacturer != nil ? String(cString: dto.manufacturer) : "Unknown"
        let mdl = dto.model != nil ? String(cString: dto.model) : "Unknown"

        snapshot = LabDeviceSnapshot(
            revision: dto.revision,
            deviceKind: dto.deviceKind,
            manufacturer: mfr,
            model: mdl,
            currentSampleRate: dto.currentSampleRate,
            opticalInput: dto.opticalInput,
            opticalOutput: dto.opticalOutput,
            supportedSampleRates: rates,
            hasOptical: dto.hasOptical,
            totalCaptureChannels: dto.totalCaptureChannels,
            totalPlaybackChannels: dto.totalPlaybackChannels,
            linkCount: dto.linkCount,
            nodes: nodes,
            ports: ports,
            mixers: mixers,
            routers: routers,
            parameters: params,
            meters: meters,
            channels: channels,
            buses: buses,
            presentation: presModel
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

    func setRouterBundleInGroup(routerNodeId: UInt32, groupBundleIds: [UInt32], selectedBundleId: UInt32) {
        guard let snap = snapshot, let router = snap.routers.first(where: { $0.id == routerNodeId }) else { return }
        var active = router.activeBundleIds
        for bId in groupBundleIds {
            active.remove(bId)
        }
        active.insert(selectedBundleId)
        let array = Array(active)
        array.withUnsafeBufferPointer { ptr in
            _ = asfw_lab_set_active_route_bundles(routerNodeId, ptr.baseAddress, UInt32(array.count))
        }
        refresh()
    }
}
