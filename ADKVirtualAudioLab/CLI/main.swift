import Foundation
import Darwin

// MARK: - ADKLab Console Diagnostic & Render Inspector

struct Ansi {
    static let reset = "\u{001B}[0m"
    static let bold = "\u{001B}[1m"
    static let dim = "\u{001B}[2m"
    static let red = "\u{001B}[31m"
    static let green = "\u{001B}[32m"
    static let yellow = "\u{001B}[33m"
    static let blue = "\u{001B}[34m"
    static let magenta = "\u{001B}[35m"
    static let cyan = "\u{001B}[36m"
    static let orange = "\u{001B}[38;5;208m"
}

func printHeader(_ title: String) {
    let line = String(repeating: "═", count: 78)
    print("\n\(Ansi.cyan)\(Ansi.bold)╔\(line)╗")
    print("║  \(title.padding(toLength: 76, withPad: " ", startingAt: 0))║")
    print("╚\(line)╝\(Ansi.reset)")
}

func printSection(_ title: String) {
    let line = String(repeating: "─", count: 74)
    print("\n\(Ansi.yellow)\(Ansi.bold)┌── [ \(title) ] \(line.prefix(max(0, 70 - title.count)))\(Ansi.reset)")
}

func dumpDeviceSnapshot(_ snap: LabDeviceSnapshot) {
    printHeader("\(snap.manufacturer) — \(snap.model) [Revision: \(snap.revision)]")

    // 1. System & Architecture Metrics
    printSection("HARDWARE ARCHITECTURE & CONFIGURATION")
    print("  • Sample Rate:      \(Ansi.bold)\(snap.currentSampleRate) Hz\(Ansi.reset) (Supported: \(snap.supportedSampleRates.map { "\($0)" }.joined(separator: ", ")))")
    print("  • Streams:          \(Ansi.cyan)\(snap.totalCaptureChannels) Capture Channels\(Ansi.reset) • \(Ansi.orange)\(snap.totalPlaybackChannels) Playback Channels\(Ansi.reset)")
    print("  • Optical Support:  \(snap.hasOptical ? "Yes (In: \(snap.opticalInput), Out: \(snap.opticalOutput))" : "No")")
    print("  • Graph Inventory:  \(snap.channels.count) Channels, \(snap.buses.count) Buses, \(snap.nodes.count) Nodes, \(snap.ports.count) Ports, \(snap.parameters.count) Parameters, \(snap.routers.count) Routers, \(snap.mixers.count) Mixers, \(snap.meters.count) Meters")

    // 2. Audio Semantics: Logical Channels & Busses
    printSection("AUDIO SEMANTICS: LOGICAL CHANNELS & BUSSES")
    if snap.channels.isEmpty {
        print("  \(Ansi.dim)(No logical channels defined)\(Ansi.reset)")
    } else {
        print("  \(Ansi.bold)Logical Channels (\(snap.channels.count)):\(Ansi.reset)")
        for ch in snap.channels {
            let portStr = ch.portIds.map { "#\($0) (\(snap.portName(for: $0)))" }.joined(separator: ", ")
            print("    • Channel #\(ch.id) '\(Ansi.cyan)\(ch.name)\(Ansi.reset)': Ports [\(portStr)]")
        }
    }
    if !snap.buses.isEmpty {
        print("\n  \(Ansi.bold)Logical Busses (\(snap.buses.count)):\(Ansi.reset)")
        for b in snap.buses {
            let portStr = b.portIds.map { "#\($0) (\(snap.portName(for: $0)))" }.joined(separator: ", ")
            print("    • Bus #\(b.id) '\(Ansi.orange)\(b.name)\(Ansi.reset)' [Semantic: \(b.semantic)]: Ports [\(portStr)]")
        }
    }

    // 3. Generic Audio Presenter Derived Channel Strips
    printSection("GENERIC AUDIO PRESENTER: DERIVED CHANNEL STRIPS")
    let strips = GenericAudioPresenter.deriveChannelStrips(from: snap)
    if strips.isEmpty {
        print("  \(Ansi.dim)(No derived channel strips)\(Ansi.reset)")
    } else {
        for s in strips {
            print("  ┌─ Strip #\(s.channelId): \(Ansi.bold)\(s.name)\(Ansi.reset)")
            if let main = s.mainSend {
                print("  │   ├── Main Send [\(main.presentation == ASFW_CONTROL_FADER ? "Fader" : "Rotary")]: '\(main.busName)' = \(String(format: "%.1f %@", main.parameter.scalarValue, main.parameter.unit)) (Param #\(main.parameter.id))")
            }
            if !s.auxSends.isEmpty {
                for aux in s.auxSends {
                    print("  │   ├── Aux Send [\(aux.presentation == ASFW_CONTROL_ROTARY ? "Rotary" : "Fader")]: '\(aux.busName)' = \(String(format: "%.1f %@", aux.parameter.scalarValue, aux.parameter.unit)) (Param #\(aux.parameter.id))")
                }
            }
            if !s.sendEnables.isEmpty {
                let text = s.sendEnables
                    .map { "\($0.busName)=\($0.isEnabled ? "\(Ansi.green)on\(Ansi.reset)" : "off")" }
                    .joined(separator: ", ")
                print("  │   ├── Send Enables [Toggles]: \(text)")
            }
            if let pan = s.pan {
                print("  │   ├── Pan [Rotary]: \(String(format: "%.0f %@", pan.scalarValue, pan.unit)) (Param #\(pan.id))")
            }
            if let mute = s.mute {
                print("  │   ├── Mute [Toggle]: \(mute.boolValue ? "\(Ansi.red)MUTED\(Ansi.reset)" : "\(Ansi.green)UNMUTED\(Ansi.reset)") (Param #\(mute.id))")
            }
            if let solo = s.solo {
                print("  │   ├── Solo [Toggle]: \(solo.boolValue ? "\(Ansi.yellow)SOLOED\(Ansi.reset)" : "OFF") (Param #\(solo.id))")
            }
            if let meter = s.meter {
                print("  │   └── Peak Meter: \(String(format: "%.1f dB", meter.value)) (Meter #\(meter.id))")
            }
        }
    }

    // 4. Generic Audio Presenter Derived Output Masters
    printSection("GENERIC AUDIO PRESENTER: DERIVED OUTPUT MASTERS")
    let masters = GenericAudioPresenter.deriveOutputMasters(from: snap)
    if masters.isEmpty {
        print("  \(Ansi.dim)(No derived output masters)\(Ansi.reset)")
    } else {
        for m in masters {
            let lvlStr = m.level != nil ? String(format: "%.1f %@", m.level!.scalarValue, m.level!.unit) : "Fixed 0dB"
            let muteStr = m.mute != nil ? (m.mute!.boolValue ? " [\(Ansi.red)MUTED\(Ansi.reset)]" : " [UNMUTED]") : ""
            print("  • Master Strip: \(Ansi.bold)\(m.name)\(Ansi.reset) ➔ Level: \(lvlStr)\(muteStr)")
        }
    }

    // 5. Signal Routers & Destination Selectors
    printSection("SIGNAL ROUTING & DESTINATION SELECTORS")
    for r in snap.routers {
        let hint = snap.presentation.routerHint(for: r.id)
        let styleStr = hint != nil ? (hint!.style == ASFW_ROUTER_STYLE_SELECTOR ? "Selector" : (hint!.style == ASFW_ROUTER_STYLE_PATCHBAY ? "Patchbay" : (hint!.style == ASFW_ROUTER_STYLE_MATRIX ? "Matrix" : "Auto"))) : "Auto"
        print("  ┌─ Router Node #\(r.id): \(Ansi.bold)\(r.name)\(Ansi.reset) [UI Style: \(styleStr)]")
        if let bg = hint?.bundleGroups, !bg.isEmpty {
            for g in bg {
                print("  │   ├── Group: '\(Ansi.bold)\(g.name)\(Ansi.reset)'")
                for bId in g.bundleIds {
                    if let bundle = r.legalBundles.first(where: { $0.id == bId }) {
                        let active = r.activeBundleIds.contains(bId) ? "\(Ansi.green)[ACTIVE]\(Ansi.reset)" : "\(Ansi.dim)[inactive]\(Ansi.reset)"
                        let routeNames = bundle.routes.map { "\(snap.portName(for: $0.inputPortId)) ➔ \(snap.portName(for: $0.outputPortId))" }.joined(separator: ", ")
                        print("  │   │   • Bundle #\(bId) \(active): \(routeNames)")
                    }
                }
            }
        } else {
            for b in r.legalBundles.prefix(10) {
                let active = r.activeBundleIds.contains(b.id) ? "\(Ansi.green)[ACTIVE]\(Ansi.reset)" : "\(Ansi.dim)[inactive]\(Ansi.reset)"
                let routeNames = b.routes.map { "\(snap.portName(for: $0.inputPortId)) ➔ \(snap.portName(for: $0.outputPortId))" }.joined(separator: ", ")
                print("  │   • Bundle #\(b.id) \(active): \(routeNames)")
            }
        }
    }
}

// MARK: - CLI Entry Point

let args = CommandLine.arguments

if let result = ADKLiveCommands.runIfRequested(Array(args.dropFirst())) {
    exit(result)
}

let bridgeState = VirtualLabState()

func dumpCurrent() {
    if let snap = bridgeState.snapshot {
        dumpDeviceSnapshot(snap)
    }
}

if args.contains("--duet") {
    bridgeState.selectDevice(ASFW_VIRTUAL_DEVICE_DUET)
    dumpCurrent()
} else if args.contains("--phase88") {
    bridgeState.selectDevice(ASFW_VIRTUAL_DEVICE_PHASE88)
    dumpCurrent()
} else if args.contains("--fw1814") {
    bridgeState.selectDevice(ASFW_VIRTUAL_DEVICE_FW1814)
    dumpCurrent()
} else if args.contains("--saffire") {
    bridgeState.selectDevice(ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP)
    dumpCurrent()
} else if args.contains("--all") {
    for dev in [ASFW_VIRTUAL_DEVICE_DUET, ASFW_VIRTUAL_DEVICE_PHASE88, ASFW_VIRTUAL_DEVICE_FW1814, ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP] {
        bridgeState.selectDevice(dev)
        dumpCurrent()
    }
} else {
    print("\(Ansi.bold)ADKVirtualLab Diagnostic CLI Inspector\(Ansi.reset)")
    print("Usage: ADKLabCLI [--duet | --phase88 | --fw1814 | --saffire | --all | adk status | adk rate <slot> <44100|48000> | adk config <slot> <44100|48000> <none|adat|spdif> <none|adat|spdif> | adk smoke]")
    print("\nDefaulting to all devices inspection:")
    for dev in [ASFW_VIRTUAL_DEVICE_DUET, ASFW_VIRTUAL_DEVICE_PHASE88, ASFW_VIRTUAL_DEVICE_FW1814, ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP] {
        bridgeState.selectDevice(dev)
        dumpCurrent()
    }
}
