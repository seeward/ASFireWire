struct AudioConfigurationCapability: Identifiable, Equatable {
    let sampleRateHz: UInt32
    let inputOptical: AudioOpticalMode
    let outputOptical: AudioOpticalMode
    let inputChannels: UInt32
    let outputChannels: UInt32

    var id: String {
        "\(sampleRateHz)-\(inputOptical.rawValue)-\(outputOptical.rawValue)"
    }
}
