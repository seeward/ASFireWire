import Foundation

/// Pure, generic presentation engine that derives pro studio channel strips, sends, and master outputs
/// from the authoritative AudioModel semantics (Channels, Buses, Crosspoints, Parameters, Meters).
enum GenericAudioPresenter {

    static func deriveChannelStrips(from snapshot: LabDeviceSnapshot) -> [ChannelStripModel] {
        var strips: [ChannelStripModel] = []

        // If logical channels are explicitly defined in the topology, use them:
        if !snapshot.channels.isEmpty {
            for ch in snapshot.channels {
                let chPorts = Set(ch.portIds)

                // 1. Sends (Crosspoints originating from this channel)
                var mainSend: SendControlModel? = nil
                var auxSends: [SendControlModel] = []

                for mixer in snapshot.mixers {
                    for cp in mixer.crosspoints {
                        if chPorts.contains(cp.inputPortId) {
                            // Destination bus
                            let destBus = snapshot.buses.first { Set($0.portIds).contains(cp.outputPortId) }
                            let busName = destBus?.name ?? snapshot.portName(for: cp.outputPortId)
                            let busSemantic = destBus?.semantic ?? ASFW_BUS_SEMANTIC_UNKNOWN

                            if let param = snapshot.parameter(forTargetCrosspoint: cp.id) {
                                let hint = snapshot.presentation.parameterHint(for: param.id)
                                let pres = (hint?.presentation != nil && hint!.presentation != ASFW_CONTROL_AUTO)
                                    ? hint!.presentation
                                    : (busSemantic == ASFW_BUS_SEMANTIC_AUX ? ASFW_CONTROL_ROTARY : ASFW_CONTROL_FADER)

                                let sendModel = SendControlModel(
                                    crosspointId: cp.id,
                                    busName: busName,
                                    busSemantic: busSemantic,
                                    parameter: param,
                                    presentation: pres
                                )

                                if pres == ASFW_CONTROL_FADER || busSemantic == ASFW_BUS_SEMANTIC_MAIN {
                                    if mainSend == nil {
                                        mainSend = sendModel
                                    } else {
                                        auxSends.append(sendModel)
                                    }
                                } else {
                                    auxSends.append(sendModel)
                                }
                            }
                        }
                    }
                }

                // 2. Channel Parameters (Pan, Mute, Solo, Phase, Phantom, Nominal)
                var panParam: ParameterModel? = nil
                var muteParam: ParameterModel? = nil
                var soloParam: ParameterModel? = nil
                var phaseParam: ParameterModel? = nil
                var phantomParam: ParameterModel? = nil
                var nominalParam: ParameterModel? = nil
                var preampLevelParam: ParameterModel? = nil

                for pId in ch.portIds {
                    for p in snapshot.parameters(forTargetPort: pId) {
                        switch p.semantic {
                        case ASFW_SEMANTIC_PAN:
                            panParam = p
                        case ASFW_SEMANTIC_MUTE:
                            muteParam = p
                        case ASFW_SEMANTIC_SOLO:
                            soloParam = p
                        case ASFW_SEMANTIC_PHASE_INVERT:
                            phaseParam = p
                        case ASFW_SEMANTIC_PHANTOM_POWER:
                            phantomParam = p
                        case ASFW_SEMANTIC_NOMINAL_LEVEL:
                            nominalParam = p
                        case ASFW_SEMANTIC_LEVEL:
                            if mainSend == nil {
                                preampLevelParam = p
                            }
                        default:
                            break
                        }
                    }
                }

                // If this channel has no crosspoint send but has a direct port level (e.g. Duet preamp gain),
                // create a pseudo MainSend so the channel fader binds to it:
                if mainSend == nil, let pl = preampLevelParam {
                    mainSend = SendControlModel(
                        crosspointId: 0,
                        busName: "Gain",
                        busSemantic: ASFW_BUS_SEMANTIC_MAIN,
                        parameter: pl,
                        presentation: ASFW_CONTROL_FADER
                    )
                }

                // 3. Peak Meter
                var meterModel: MeterModel? = nil
                for pId in ch.portIds {
                    if let m = snapshot.meters(forTargetPort: pId).first {
                        meterModel = m
                        break
                    }
                }

                strips.append(ChannelStripModel(
                    channelId: ch.id,
                    name: ch.name,
                    mainSend: mainSend,
                    auxSends: auxSends,
                    pan: panParam,
                    mute: muteParam,
                    solo: soloParam,
                    phase: phaseParam,
                    phantom: phantomParam,
                    nominalLevel: nominalParam,
                    meter: meterModel
                ))
            }
        }

        return strips
    }

    static func deriveOutputMasters(from snapshot: LabDeviceSnapshot) -> [OutputMasterStripModel] {
        var masters: [OutputMasterStripModel] = []
        var seenNames = Set<String>()
        let channelPorts = Set(snapshot.channels.flatMap { $0.portIds })
        let busPorts = Set(snapshot.buses.flatMap { $0.portIds })

        // 1. Check logical buses with explicit Level / Mute controls or bus meters
        for bus in snapshot.buses {
            var levelParam: ParameterModel? = nil
            var muteParam: ParameterModel? = nil
            var dimParam: ParameterModel? = nil
            var meters: [MeterModel] = []

            for pId in bus.portIds {
                for p in snapshot.parameters(forTargetPort: pId) {
                    if p.semantic == ASFW_SEMANTIC_LEVEL && levelParam == nil { levelParam = p }
                    if p.semantic == ASFW_SEMANTIC_MUTE && muteParam == nil { muteParam = p }
                    if p.semantic == ASFW_SEMANTIC_DIM && dimParam == nil { dimParam = p }
                }
                meters.append(contentsOf: snapshot.meters(forTargetPort: pId))
            }

            if levelParam != nil || muteParam != nil || !meters.isEmpty {
                masters.append(OutputMasterStripModel(
                    name: bus.name,
                    level: levelParam,
                    mute: muteParam,
                    dim: dimParam,
                    meters: meters
                ))
                seenNames.insert(bus.name)
            }
        }

        // 2. Check physical output ports with explicit Level or Mute parameters (not belonging to channels or buses)
        for port in snapshot.ports where !channelPorts.contains(port.id) && !busPorts.contains(port.id) {
            let portParams = snapshot.parameters(forTargetPort: port.id)
            let level = portParams.first { $0.semantic == ASFW_SEMANTIC_LEVEL }
            let mute = portParams.first { $0.semantic == ASFW_SEMANTIC_MUTE }
            let dim = portParams.first { $0.semantic == ASFW_SEMANTIC_DIM }
            let portMeters = snapshot.meters(forTargetPort: port.id)

            let cleanName = port.name.replacingOccurrences(of: "Phys Out: ", with: "")
                                     .replacingOccurrences(of: "Out: ", with: "")
            if !seenNames.contains(cleanName) && (level != nil || mute != nil) {
                masters.append(OutputMasterStripModel(
                    name: cleanName,
                    level: level,
                    mute: mute,
                    dim: dim,
                    meters: portMeters
                ))
                seenNames.insert(cleanName)
            }
        }

        // 3. Presentation groups of kind Monitor/OutputChannel for node-level master stages
        for grp in snapshot.presentation.groups where grp.kind == ASFW_PRES_GROUP_MONITOR || grp.kind == ASFW_PRES_GROUP_OUTPUT_CHANNEL {
            if !seenNames.contains(grp.name) {
                let grpParams = grp.parameterIds.compactMap { snapshot.parameter(forId: $0) }
                let grpMeters = grp.meterIds.compactMap { snapshot.meter(forId: $0) }

                let level = grpParams.first { $0.semantic == ASFW_SEMANTIC_LEVEL }
                let mute = grpParams.first { $0.semantic == ASFW_SEMANTIC_MUTE }
                let dim = grpParams.first { $0.semantic == ASFW_SEMANTIC_DIM }

                if level != nil || mute != nil || !grpMeters.isEmpty {
                    masters.append(OutputMasterStripModel(
                        name: grp.name,
                        level: level,
                        mute: mute,
                        dim: dim,
                        meters: grpMeters
                    ))
                    seenNames.insert(grp.name)
                }
            }
        }

        return masters
    }
}
