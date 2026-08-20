struct AudioConfigurationSnapshot: Equatable {
    let endpointID: AudioEndpointID
    let committed: AudioConfigurationCapability
    let capabilities: [AudioConfigurationCapability]
}
