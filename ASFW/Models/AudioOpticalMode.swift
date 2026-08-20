enum AudioOpticalMode: UInt8, CaseIterable, Identifiable {
    case adat = 1
    case spdif = 2

    var id: UInt8 { rawValue }
    var label: String { self == .adat ? "ADAT" : "S/PDIF" }
}
