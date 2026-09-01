import Foundation

/// Semantic, read-only projection of the Saffire Pro 24 DSP controls currently
/// published by the driver. IDs are semantic control names shared with the
/// profile; neither this type nor its views know DICE application offsets.
nonisolated struct SaffireControlSurface: Equatable, Sendable {
    enum MicInputMode: Int32, Equatable, Sendable {
        case line = 0
        case instrument = 1

        var label: String { self == .line ? "Line" : "Instrument" }
    }

    enum LineInputLevel: Int32, Equatable, Sendable {
        case low = 0
        case high = 1

        var label: String { self == .low ? "+16 dBu" : "−10 dBV" }
    }

    /// A read-only, driver-resolved route identity for one physical stereo
    /// output. It deliberately does not expose a DICE block/channel or imply
    /// that the route can be changed from this surface.
    enum OutputRouteSource: Int32, Equatable, Sendable {
        case unknown = 0
        case hostPlayback12 = 1
        case hostPlayback34 = 2
        case hostPlayback56 = 3
        case hostPlayback78 = 4
        case mixer12 = 16
        case mixer34 = 17
        case mixer56 = 18
        case mixer78 = 19
        case analog12 = 32
        case spdif12 = 48

        var label: String {
            switch self {
            case .unknown: "Route not resolved"
            case .hostPlayback12: "DAW 1/2"
            case .hostPlayback34: "DAW 3/4"
            case .hostPlayback56: "DAW 5/6"
            case .hostPlayback78: "DAW 7/8"
            case .mixer12: "Mixer 1/2"
            case .mixer34: "Mixer 3/4"
            case .mixer56: "Mixer 5/6"
            case .mixer78: "Mixer 7/8"
            case .analog12: "Analog 1/2"
            case .spdif12: "S/PDIF 1/2"
            }
        }
    }

    struct OutputPair: Equatable, Sendable, Identifiable {
        let id: Int
        let title: String
        let leftVolume: Int32
        let rightVolume: Int32
        let leftMuted: Bool
        let rightMuted: Bool
        let routeSource: OutputRouteSource
    }

    struct ChannelStrip: Equatable, Sendable, Identifiable {
        let id: Int
        let equalizerEnabled: Bool
        let compressorEnabled: Bool
        let equalizerAfterCompressor: Bool
    }

    let micInputModes: [MicInputMode]
    let lineInputLevels: [LineInputLevel]
    let outputPairs: [OutputPair]
    let globalMute: Bool
    let globalDim: Bool
    let channelStrips: [ChannelStrip]
    let reverbEnabled: Bool
    let inSituEnabled: Bool

    init?(_ snapshot: AudioControlSurfaceSnapshot?) {
        guard let snapshot, snapshot.isFocusriteSPro24Dsp else { return nil }
        func value(_ id: UInt32) -> Int32? {
            snapshot.values.first(where: { $0.id == id })?.value
        }
        guard let mic1 = value(SaffireControlID.micInputMode1).flatMap(MicInputMode.init(rawValue:)),
              let mic2 = value(SaffireControlID.micInputMode2).flatMap(MicInputMode.init(rawValue:)),
              let line34 = value(SaffireControlID.lineInputLevel34).flatMap(LineInputLevel.init(rawValue:)),
              let line56 = value(SaffireControlID.lineInputLevel56).flatMap(LineInputLevel.init(rawValue:)),
              let mute = value(SaffireControlID.globalMute),
              let dim = value(SaffireControlID.globalDim),
              let reverb = value(SaffireControlID.reverbEnabled),
              let inSitu = value(SaffireControlID.inSituMode) else { return nil }

        let names = ["LINE OUT 1/2", "LINE OUT 3/4 · HP 1", "LINE OUT 5/6 · HP 2"]
        let pairs = names.enumerated().compactMap { pair, title -> OutputPair? in
            let left = pair * 2
            guard let leftVolume = value(SaffireControlID.outputVolumeFirst + UInt32(left)),
                  let rightVolume = value(SaffireControlID.outputVolumeFirst + UInt32(left + 1)),
                  let leftMute = value(SaffireControlID.outputMuteFirst + UInt32(left)),
                  let rightMute = value(SaffireControlID.outputMuteFirst + UInt32(left + 1)),
                  let routeRaw = value(SaffireControlID.outputRouteSourceFirst + UInt32(pair)),
                  let routeSource = OutputRouteSource(rawValue: routeRaw) else {
                return nil
            }
            return .init(id: pair, title: title, leftVolume: leftVolume, rightVolume: rightVolume,
                         leftMuted: leftMute != 0, rightMuted: rightMute != 0,
                         routeSource: routeSource)
        }
        guard pairs.count == names.count else { return nil }

        let strips = (0..<2).compactMap { index -> ChannelStrip? in
            guard let eq = value(SaffireControlID.channelStripEqFirst + UInt32(index)),
                  let compressor = value(SaffireControlID.channelStripCompressorFirst + UInt32(index)),
                  let order = value(SaffireControlID.channelStripEqAfterCompressorFirst + UInt32(index)) else {
                return nil
            }
            return .init(id: index, equalizerEnabled: eq != 0, compressorEnabled: compressor != 0,
                         equalizerAfterCompressor: order != 0)
        }
        guard strips.count == 2 else { return nil }

        micInputModes = [mic1, mic2]
        lineInputLevels = [line34, line56]
        outputPairs = pairs
        globalMute = mute != 0
        globalDim = dim != 0
        channelStrips = strips
        reverbEnabled = reverb != 0
        inSituEnabled = inSitu != 0
    }
}

/// Shared semantic IDs. Their numerical encoding is private to the app/driver
/// ABI and intentionally does not mirror a register map.
/// App-side ABI names for the driver-owned Saffire control surface. These are
/// semantic IDs, not vendor application-section offsets.
/// Wire constants. `nonisolated` because the module defaults to MainActor
/// isolation (SWIFT_DEFAULT_ACTOR_ISOLATION), which would otherwise confine a
/// table of UInt32 literals to the main actor and drag every reader onto it.
nonisolated enum SaffireControlID {
    static let micInputMode1: UInt32 = 0x5350_0001
    static let micInputMode2: UInt32 = 0x5350_0002
    static let lineInputLevel34: UInt32 = 0x5350_0003
    static let lineInputLevel56: UInt32 = 0x5350_0004
    static let outputVolumeFirst: UInt32 = 0x5350_0100
    static let outputMuteFirst: UInt32 = 0x5350_0110
    static let globalMute: UInt32 = 0x5350_0120
    static let globalDim: UInt32 = 0x5350_0121
    static let outputRouteSourceFirst: UInt32 = 0x5350_0130
    static let channelStripEqFirst: UInt32 = 0x5350_0200
    static let channelStripCompressorFirst: UInt32 = 0x5350_0210
    static let channelStripEqAfterCompressorFirst: UInt32 = 0x5350_0220
    static let reverbEnabled: UInt32 = 0x5350_0230
    static let inSituMode: UInt32 = 0x5350_0231
}
