enum AudioOpticalMode: UInt8, CaseIterable, Identifiable {
    case none = 0
    case adat = 1
    case spdif = 2

    var id: UInt8 { rawValue }
    var label: String {
        switch self {
        case .none: return "None"
        case .adat: return "ADAT"
        case .spdif: return "S/PDIF"
        }
    }
}
