struct AudioConfigurationSnapshot: Equatable {
    let endpointID: AudioEndpointID
    /// Structural identity for the resolved stream/topology shape. Control and
    /// meter state must carry this same value before the UI combines them.
    let topologyRevision: UInt64
    let committed: AudioConfigurationCapability
    let capabilities: [AudioConfigurationCapability]
}
