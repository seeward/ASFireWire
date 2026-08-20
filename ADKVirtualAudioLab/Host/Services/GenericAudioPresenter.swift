import Foundation

/// Pure, generic presentation engine that derives pro studio channel strips, sends, and master outputs
/// from the authoritative AudioModel semantics (Channels, Buses, Crosspoints, Parameters, Meters).
enum GenericAudioPresenter {

    /// A mixer big enough to be shown as a crossbar contributes its crosspoints
    /// to the matrix view, not to channel strips: an 18x16 TCAT matrix has a
    /// coefficient per crosspoint, and stacking sixteen of them in one strip is
    /// truthful and unusable. Same rule the console rack uses to pick the view.
    static func isMatrixMixer(_ mixer: MixerModel, in snapshot: LabDeviceSnapshot) -> Bool {
        let hint = snapshot.presentation.mixerHint(for: mixer.id)
        if hint?.style == ASFW_MIXER_STYLE_MATRIX { return true }
        if hint?.style == ASFW_MIXER_STYLE_CHANNEL_STRIPS { return false }
        return mixer.crosspoints.count > 32
    }

    static func deriveChannelStrips(from snapshot: LabDeviceSnapshot) -> [ChannelStripModel] {
        var strips: [ChannelStripModel] = []

        // If logical channels are explicitly defined in the topology, use them:
        if !snapshot.channels.isEmpty {
            for ch in snapshot.channels {
                let chPorts = Set(ch.portIds)

                // 1. Sends. A crosspoint says the contribution exists; the
                // *level* may sit on the crosspoint or on the mixer input port
                // (AUAA 13.1), and a boolean on the crosspoint is a send
                // enable, not a level. Sends are grouped by the parameter that
                // carries the gain, so one input gain feeding two mixer buses
                // is one fader with two enables rather than two dead faders.
                var mainSend: SendControlModel? = nil
                var auxSends: [SendControlModel] = []
                var sendEnables: [SendEnableModel] = []
                var sendsByLevelParam: [UInt32: Int] = [:]   // parameterId -> index in collected
                var collected: [SendControlModel] = []

                for mixer in snapshot.mixers where !isMatrixMixer(mixer, in: snapshot) {
                    for cp in mixer.crosspoints where chPorts.contains(cp.inputPortId) {
                        let destBus = snapshot.buses.first { Set($0.portIds).contains(cp.outputPortId) }
                        let busName = destBus?.name ?? snapshot.portName(for: cp.outputPortId)
                        let busSemantic = destBus?.semantic ?? ASFW_BUS_SEMANTIC_UNKNOWN

                        let crosspointParam = snapshot.parameter(forTargetCrosspoint: cp.id)

                        if let param = crosspointParam, param.kind == ASFW_PARAM_KIND_BOOLEAN {
                            sendEnables.append(SendEnableModel(
                                id: cp.id,
                                busName: busName,
                                parameter: param
                            ))
                        }

                        // The gain: on the crosspoint when the hardware puts it
                        // there, otherwise on the mixer input port this
                        // crosspoint reads from.
                        let levelParam: ParameterModel? = {
                            if let param = crosspointParam, param.kind != ASFW_PARAM_KIND_BOOLEAN { return param }
                            return snapshot.parameters(forTargetPort: cp.inputPortId)
                                .first { $0.semantic == ASFW_SEMANTIC_LEVEL }
                        }()
                        guard let level = levelParam else { continue }

                        if let existing = sendsByLevelParam[level.id] {
                            // Same gain, another destination: widen the label
                            // rather than emitting a duplicate control.
                            let previous = collected[existing]
                            if !previous.busName.contains(busName) {
                                collected[existing] = SendControlModel(
                                    crosspointId: previous.crosspointId,
                                    busName: "\(previous.busName) + \(busName)",
                                    busSemantic: previous.busSemantic,
                                    parameter: previous.parameter,
                                    presentation: previous.presentation
                                )
                            }
                            continue
                        }

                        let hint = snapshot.presentation.parameterHint(for: level.id)
                        let pres = (hint?.presentation != nil && hint!.presentation != ASFW_CONTROL_AUTO)
                            ? hint!.presentation
                            : (busSemantic == ASFW_BUS_SEMANTIC_AUX ? ASFW_CONTROL_ROTARY : ASFW_CONTROL_FADER)

                        sendsByLevelParam[level.id] = collected.count
                        collected.append(SendControlModel(
                            crosspointId: cp.id,
                            busName: busName,
                            busSemantic: busSemantic,
                            parameter: level,
                            presentation: pres
                        ))
                    }
                }

                for send in collected {
                    let wantsFader = send.presentation == ASFW_CONTROL_FADER
                        || send.busSemantic == ASFW_BUS_SEMANTIC_MAIN
                    if wantsFader && mainSend == nil {
                        mainSend = send
                    } else {
                        auxSends.append(send)
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
                        case ASFW_SEMANTIC_PAN, ASFW_SEMANTIC_BALANCE:
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
                            if mainSend == nil && sendsByLevelParam[p.id] == nil {
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
                    sendEnables: sendEnables,
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
