# ASFW Unified Audio Architecture
## Engineering Design Document

**Project:** ASFW / ASFireWire  
**Primary implementation laboratory:** `ADKVirtualAudioLab`  
**Language:** C++23 core, AudioDriverKit integration, SwiftUI tooling/UI  
**Status:** Architecture baseline / semantic-core implementation phase  
**Audience:** ASFW contributors, protocol/backend implementers, driver/runtime developers, UI/tooling developers

---

## 1. Purpose

ASFW aims to support a broad and historically messy set of FireWire audio interfaces through one macOS driver architecture without pretending that the devices themselves are uniform.

The central problem is not merely to stream audio from many devices. The harder problem is to represent, configure, control, expose, and debug devices whose hardware topologies, routing fabrics, mixers, clocking models, control protocols, and user-interface expectations differ substantially.

The architecture therefore does **not** attempt to define one universal “FireWire device API” into which every vendor-specific feature must fit.

Instead, ASFW defines a **small, explicit semantic audio language** for the parts of device behavior that are genuinely common:

- signal endpoints;
- ports;
- fixed signal relationships;
- routers;
- mixers;
- processors;
- parameters;
- meters;
- topology;
- runtime state.

Everything below that semantic layer may remain family-specific or device-specific. Everything above it should consume resolved semantic truth rather than protocol-specific details.

The design principle is:

> **Unified driver does not mean unified topology. It means a unified language capable of describing different topologies truthfully.**

A second, equally important principle is:

> **Assume no abstraction is universal. Extend the common vocabulary only when real hardware proves that an additional common concept is required.**

This document describes the intended architecture, the current semantic model, the dependency boundaries, the hardware-support workflow, the testing strategy, and the integration path into AudioDriverKit.

---

## 2. Motivation and Problem Statement

FireWire audio interfaces vary along several largely independent axes.

### 2.1 Protocol-family differences

Examples include:

- BeBoB / BridgeCo devices using AV/C Audio and Music subunits;
- DICE devices using asynchronous register transactions and large hardware routers;
- Oxford/OXFW devices with vendor-dependent AV/C/FCP control;
- M-Audio devices with proprietary control blocks layered over otherwise familiar streaming;
- devices that partially implement standard AV/C but require vendor quirks or avoidance of unsafe commands.

A driver architecture that leaks these differences upward becomes increasingly device-specific.

### 2.2 Topology differences

A device may contain:

- direct physical-input-to-host paths;
- playback streams feeding physical outputs directly;
- low-latency mixers;
- large programmable routing matrices;
- nested source selectors;
- channel strips;
- reverbs;
- monitor sections;
- headphone selectors;
- output groups;
- hardware meters;
- fixed paths that cannot be changed.

These are not interchangeable concepts. A mixer is not simply a router with many active edges. A router does not sum signals. A selector is often a degenerate router. A processor transforms audio and may or may not expose internal details.

### 2.3 Configuration-dependent topology

Some devices change topology when configuration changes.

Examples:

- ADAT channel count varies with sample rate;
- optical ports may switch between ADAT and S/PDIF;
- stream modes can change host-visible channel counts;
- sample rate can change the number or meaning of hardware slots;
- clock source may constrain available rates or modes;
- a device can expose different routing resources in different configurations.

Therefore the driver must not expose a permanently fixed graph and merely mark some channels “disabled” when those channels do not physically exist in the current mode.

The current graph must tell the truth.

### 2.4 Control-model differences

A “gain” may be implemented as:

- an AV/C Audio Function Block operation;
- a DICE register;
- a vendor-dependent FCP command;
- a write-only M-Audio configuration field cached by the host;
- a nonlinear coefficient requiring a lookup table.

The semantic model must represent “gain” independently of how it is encoded.

### 2.5 State-observability differences

Some state is:

- readable from hardware;
- write-only and therefore cached;
- inferred from relative encoder events;
- derived from several lower-level values;
- invalidated by bus reset or configuration replacement.

The runtime must distinguish state provenance rather than pretending all device state is directly observable.

### 2.6 UI differences

Some devices map cleanly to a generic mixer/routing UI. Others expose:

- front-panel knob modes;
- LED/display configuration;
- monitor-specific behavior;
- proprietary DSP editors;
- unusual source-selection semantics;
- diagnostics or firmware consoles.

The architecture must support a strong generic UI for common semantics without requiring every vendor-specific feature to masquerade as a common primitive.

---

## 3. Architectural Goals

The architecture should satisfy the following goals.

### 3.1 Truthful representation

The model should describe what the current hardware actually is, not what would be convenient for Core Audio or the UI.

### 3.2 Device-family isolation

Protocol-specific knowledge should stay below the semantic layer.

The semantic core must not contain:

- AV/C opcodes;
- DICE register offsets;
- FCP transaction rules;
- vendor command IDs;
- FireWire packet layouts;
- AudioDriverKit object classes;
- Swift or SwiftUI types.

### 3.3 Dynamic topology

The model must support structural replacement when sample rate or other configuration changes the actual device topology.

### 3.4 One committed representation

At any committed configuration:

- stream geometry;
- semantic topology;
- routing state;
- parameters;
- Core Audio representation;
- UI representation

must describe the same reality.

### 3.5 Extensibility without semantic pollution

Unusual device-specific features must be allowed to exist outside the common semantic vocabulary.

The common model should grow because multiple real devices demand a new shared concept, not because one vendor has one unusual register.

### 3.6 Testability

All core semantics should be dependency-free C++ and testable without hardware, FireWire, AudioDriverKit, or SwiftUI.

### 3.7 Architecture by falsification

New devices should actively try to break the current semantic language.

If a device cannot be described truthfully, the architecture must determine whether:

1. the fixture is wrong;
2. the protocol understanding is wrong;
3. the semantic model is incomplete;
4. the feature belongs outside the common semantic model.

---

## 4. Non-Goals

The semantic core is intentionally not:

- a universal representation of every possible hardware feature;
- a generic GUI toolkit;
- a FireWire protocol API;
- an AV/C abstraction;
- a DICE abstraction;
- a packetizer;
- a register map;
- a plugin ABI;
- a replacement for protocol-specific diagnostics;
- a model of physical front-panel behavior unless that behavior has clear shared audio semantics.

The architecture also does not require that every device use every semantic primitive.

A device with no mixer should not contain a fake mixer.

A device with fixed routing should not contain a configurable router merely to make the graph uniform.

---

# 5. High-Level Architecture

```text
                         ┌──────────────────────────────┐
                         │      Device / Bus Layer      │
                         │ FireWire discovery, identity │
                         └──────────────┬───────────────┘
                                        │
                           family/device-specific facts
                                        │
             ┌──────────────────────────▼──────────────────────────┐
             │             Capability Discovery Layer              │
             │ AV/C / BeBoB | DICE | OXFW | vendor-specific       │
             └──────────────────────────┬──────────────────────────┘
                                        │
                             normalized capabilities
                                        │
                             + profile / quirks
                                        │
       UI / Core Audio intent ──────────┼────────── hardware observation
                                        ▼
             ┌─────────────────────────────────────────────────────┐
             │              Configuration Coordinator              │
             │ confirmed + desired + one pending transition       │
             └──────────────────────────┬──────────────────────────┘
                                        │
                 capabilities + coordinated configuration
                                        │
             ┌──────────────────────────▼──────────────────────────┐
             │               Configuration Resolver                │
             │ candidate validation or confirmed projection       │
             └───────────────┬──────────────────────┬──────────────┘
                             │                      │
                             │                      │
                   semantic topology         resolved stream plan
                             │                      │
                             └──────────┬───────────┘
                                        │
                         ResolvedAudioConfiguration
                                        │
               ┌────────────────────────┼────────────────────────┐
               │                        │                        │
               ▼                        ▼                        ▼
        Runtime State             AudioDriverKit             SwiftUI/UI
        + Bindings                projection                  tooling
               │
               ▼
      protocol/control bindings
               │
      ┌────────┴─────────┐
      ▼                  ▼
 AV/C/FCP/DICE       vendor control
 codecs/transports       codecs
```

The primary architectural idea is that **hardware-family-specific mechanisms terminate before the semantic model**.

Conversely, AudioDriverKit and SwiftUI should not need to understand protocol-family details.

---

# 6. The Two Architectural Waists

ASFW is expected to converge around two major intermediate representations.

## 6.1 `AudioDeviceCapabilities`

This is the normalized description of what the hardware can potentially support.

It is produced by:

- generic discovery;
- family-specific probing;
- device-specific safe probing;
- profile/quirk overlays.

It is not necessarily the current topology.

Conceptually:

```cpp
struct AudioDeviceCapabilities {
    DeviceIdentity identity;

    // Potential physical and host-facing resources.
    std::vector<PhysicalPortCaps> physicalPorts;
    std::vector<StreamCaps> streams;

    // Clock/rate capability.
    ClockCapabilities clocking;

    // Potential audio structures.
    RoutingCapabilities routing;
    MixerCapabilities mixers;
    ProcessorCapabilities processors;
    ControlCapabilities controls;

    // Supported structural modes.
    std::vector<SampleRate> sampleRates;
};
```

The exact schema should remain hardware-driven and evolve only after the semantic fixtures stabilize.

## 6.2 `ResolvedAudioConfiguration`

This is the committed current interpretation of the device.

Conceptually:

```cpp
struct ResolvedAudioConfiguration {
    uint64_t revision;

    Topology topology;
    ResolvedStreamPlan streams;
    StreamBindings streamBindings;

    // Future:
    // ControlBindings
    // RouteBindings
    // ClockState / sync model
};
```

Everything above the resolver should consume this representation.

The resolved configuration is the contract between:

- device-specific knowledge;
- stream transport;
- Core Audio;
- UI;
- runtime state;
- diagnostics.

---

# 7. Semantic Core

The semantic core should remain deliberately small and boring.

```text
Node
Port
FixedLink
Router
Route
RouteBundle
Mixer
MixerCrosspoint
Processor
Parameter
Meter
Topology
State
```

These are **value-level concepts**, not an inheritance hierarchy.

Current implementation direction uses `std::variant` for the closed set of node bodies.

---

## 7.1 Strong IDs

Identity is explicit.

```cpp
template <typename Tag>
struct Id {
    uint32_t value{};
    constexpr auto operator<=>(const Id&) const = default;
};

using NodeId        = Id<struct NodeTag>;
using PortId        = Id<struct PortTag>;
using RouteBundleId = Id<struct RouteBundleTag>;
using CrosspointId  = Id<struct CrosspointTag>;
using ParameterId   = Id<struct ParameterTag>;
using MeterId       = Id<struct MeterTag>;
```

### Rules

- IDs are not vector indexes.
- IDs are stable for the lifetime/revision in which the represented object exists.
- A topology revision scopes the validity of its IDs.
- State must never silently apply IDs from an old topology revision to a new one.
- Semantic string names may later be layered over numeric IDs for persistence, diagnostics, UI, or capability resolution.

---

# 8. Node Model

A node is an identifiable signal-processing or signal-boundary object.

```cpp
enum class EndpointKind {
    Physical,
    Host,
};

struct EndpointNode {
    EndpointKind kind{EndpointKind::Physical};
};

struct RouterNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
    std::vector<RouteBundle> legalBundles;
    RouterConstraints constraints;
};

struct MixerNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
    std::vector<MixerCrosspoint> crosspoints;
};

struct ProcessorNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
};

using NodeBody = std::variant<
    EndpointNode,
    RouterNode,
    MixerNode,
    ProcessorNode
>;

struct Node {
    NodeId id;
    std::string name;
    NodeBody body;
};
```

This removes the earlier duplicated identity problem where a router simultaneously had `NodeId` and `RouterId`.

### Why `variant`

The semantic node vocabulary is currently closed and owned by the core.

That is exactly where `std::variant` is useful:

- exhaustive visitation;
- no base-class ownership;
- no vtable requirement;
- value semantics;
- explicit compile-time alternatives.

This rule should **not** be generalized to protocol families.

A future arbitrary protocol/plugin set should not be encoded as one giant semantic-core `variant`.

---

# 9. Endpoints

`EndpointNode` identifies graph boundaries.

## 9.1 Physical endpoint

Represents physical device-facing audio I/O:

- analog inputs;
- analog outputs;
- S/PDIF;
- ADAT;
- headphone outputs;
- other physical connectors.

## 9.2 Host endpoint

Represents host-visible stream channels.

This distinction is semantic and must not be inferred from names such as `"Host Audio Streams"`.

Future endpoint kinds may be added only if real devices prove they are semantically useful.

---

# 10. Port Model

A port is a **connectable signal boundary** owned by one node.

```cpp
enum class PortDirection {
    Input,
    Output,
};

struct Port {
    PortId id;
    NodeId owner;
    PortDirection direction;

    uint32_t channels{1};
    std::string name;
};
```

## 10.1 Important definition

Earlier architecture discussion used the phrase:

> Port is the atomic routing unit.

That is too strong once `RouteBundle` exists.

The more accurate definition is:

> **A Port is a connectable signal boundary. A RouteBundle is the smallest set of routes which must change atomically.**

A port can itself carry multiple channels when the hardware exposes those channels as one semantic connection unit.

Examples:

- stereo S/PDIF pair;
- stereo headphone pair;
- stereo mixer bus;
- mono analog channel.

The `channels` field is intentionally simple for the first implementation.

A richer channel-layout model may be introduced later if actual hardware forces distinctions such as:

- named channel positions;
- non-contiguous channel groups;
- asymmetric route compatibility;
- bundle width different from signal width.

---

# 11. Fixed Links

A fixed link represents a signal relationship that exists in the current resolved topology and is not user-selectable.

```cpp
struct FixedLink {
    PortId source;
    PortId destination;
};
```

Examples:

```text
Physical Input → Capture Processor
Mixer Output → Output Stage
Router Output → Physical Output
Preamp Output → Host Capture
```

### Invariants

- source exists;
- destination exists;
- source is `Output`;
- destination is `Input`;
- channel geometry is compatible;
- an input must not have multiple fixed drivers unless the semantic model explicitly supports that case.

A fixed link is **structure**, not state.

---

# 12. Router Model

A router represents configurable connectivity without signal summation.

> **Router = configurable subset of legal source→destination relationships subject to constraints, without combining signals.**

Examples:

- input XLR/instrument selector;
- output playback/mixer selector;
- headphone source selector;
- large DICE route matrix;
- Phase88 output selection.

---

## 12.1 Route

```cpp
struct Route {
    PortId input;
    PortId output;
};
```

A route represents one potential internal connection through the router.

---

## 12.2 RouteBundle

Some hardware operations switch several signal paths together.

```cpp
struct RouteBundle {
    RouteBundleId id;
    std::vector<Route> routes;
};
```

Examples:

### Independent input selector

```text
Bundle 1: XLR1   → Selected1
Bundle 2: Phone1 → Selected1
Bundle 3: XLR2   → Selected2
Bundle 4: Phone2 → Selected2
```

Two bundles may be active simultaneously.

### Coupled stereo selector

```text
Bundle 5:
    Playback L → Output L
    Playback R → Output R

Bundle 6:
    Mixer L → Output L
    Mixer R → Output R
```

Only one bundle may be active.

The distinction is hardware-semantic, not cosmetic.

Stereo channels should not automatically be bundled merely because they are stereo.

---

## 12.3 Router constraints

Current direction:

```cpp
struct RouterConstraints {
    std::optional<uint32_t> maxActiveBundles;
    std::optional<uint32_t> maxActiveRoutes;
    std::optional<uint32_t> maxSourcesPerOutput;
    std::optional<uint32_t> maxDestinationsPerInput;
};
```

These dimensions are deliberately separate.

Examples:

- selector: one source per output;
- DICE matrix: many routes globally, one source per destination;
- fan-out: one source may drive several destinations;
- hardware table: total route-entry capacity is limited.

Constraints should describe semantic legality, not implementation convenience.

---

## 12.4 Route bundle identity scope

Preferred rule:

> `RouteBundleId` is local to a router node.

A bundle is naturally identified by:

```text
(NodeId router, RouteBundleId bundle)
```

This avoids maintaining an unnecessary topology-wide bundle allocator.

If later serialization or persistence strongly benefits from globally unique bundle IDs, that can be revisited explicitly.

---

# 13. Mixer Model

A mixer is a first-class semantic object.

> **Mixer = a node which may combine multiple input signals into one or more outputs.**

```cpp
struct MixerCrosspoint {
    CrosspointId id;
    PortId input;
    PortId output;
};

struct MixerNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
    std::vector<MixerCrosspoint> crosspoints;
};
```

A mixer must not be flattened into a generic routing graph.

The distinction matters because:

- multiple sources may contribute to one destination;
- crosspoints may have coefficients;
- input strips may have gains/mutes;
- output stages may have master controls;
- hardware may expose only a subset of all theoretical crosspoints.

---

## 13.1 Crosspoints are structural, not control values

A `MixerCrosspoint` means:

> this input contribution to this output exists.

It does **not** mean:

> this crosspoint necessarily has an independent gain control.

Parameter placement is independent.

A device may attach level semantics to:

- each crosspoint;
- each mixer input port;
- each mixer output port;
- the mixer node;
- some combination.

This is necessary because real hardware places controls differently.

---

## 13.2 Full and sparse matrices

The model stores crosspoints explicitly.

It does not assume:

```text
inputs × outputs = all possible crosspoints
```

Helper builders may later provide:

```cpp
makeFullMixer(...)
makeSparseMixer(...)
```

but matrix completeness is not part of the semantic definition.

---

## 13.3 Open question: no crosspoint equivalent of `RouteBundle`

§12.2 introduces `RouteBundle` because *"some hardware operations switch several
signal paths together"*, and §10.1 generalises it as *"the smallest set of routes
which must change atomically"*. Real mixer gestures have the same property, and
the mixer model currently has no equivalent.

`MixerNode` holds bare `crosspoints`, and `ParameterTarget` is a single
`variant<NodeId, PortId, CrosspointId>`. A stereo strip's level-plus-balance is
one semantic gesture over a **pair** of crosspoints that must be written and
confirmed together; there is presently no way to express that.

Evidence so far comes from one device. The Saffire Pro 24 DSP's monitor mixer is
eight stereo mixes of eighteen sources, where both a stereo-source balance and a
mono-source pan are single gestures writing two cells atomically. ASFW carries
this today as ad-hoc `presentationGroupId` / `channelRole` fields on the matrix
wire format — the missing concept under a different name. See
`documentation/SPRO24DSP.md` §3 and §3.1.

This is §3.7 case 3, *the semantic model is incomplete*, and is recorded as an
open question rather than a decided change. Per §3.5 the vocabulary should grow
only when **multiple** real devices demand the shared concept; one device is not
yet that evidence.

---

# 14. Processor Model

A processor represents signal transformation that is neither routing nor summation.

```cpp
struct ProcessorNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
};
```

Examples:

- preamp stage;
- channel strip;
- reverb;
- monitor output stage;
- DSP engine;
- virtual capture bus;
- level/mute output stage.

Version 1 intentionally does not attempt to model every possible DSP algorithm.

A processor can be:

- semantically generic;
- partially described through parameters;
- opaque except for input/output ports.

This allows truthful topology without requiring a universal EQ/compressor/reverb ontology.

---

# 15. Parameter Model

A parameter is mutable semantic state attached to an identifiable semantic target.

```cpp
enum class ParameterSemantic {
    Unknown,
    Level,
    Mute,
    PhantomPower,
    PhaseInvert,
    Balance,
    NominalLevel,
    ClockSource,
    Dim,
};
```

Native hardware encoding is not part of `ParameterSemantic`.

---

## 15.1 Parameter values

```cpp
using ParameterValue =
    std::variant<bool, double, int64_t>;
```

The exact value alternatives may grow if hardware proves a need.

---

## 15.2 Domains

```cpp
struct BooleanDomain {};

enum class ScalarUnit {
    Generic,
    Decibels,
    Hertz,
    Percent,
    Normalized,
};

struct ScalarDomain {
    double min;
    double max;
    std::optional<double> step;
    ScalarUnit unit;
};

struct EnumItem {
    int64_t value;
    std::string name;
};

struct EnumDomain {
    std::vector<EnumItem> values;
};

using ParameterDomain =
    std::variant<
        BooleanDomain,
        ScalarDomain,
        EnumDomain
    >;
```

---

## 15.3 Parameter target

```cpp
using ParameterTarget =
    std::variant<
        NodeId,
        PortId,
        CrosspointId
    >;
```

Examples:

```text
Input gain        → PortId
Phantom power     → PortId
Output stage mute → NodeId
Mixer coefficient → CrosspointId
Clock source      → NodeId or future clock object
```

---

## 15.4 Semantic value vs native representation

A native register range must not automatically become the public semantic range.

Examples:

```text
native -32768..0
    → semantic -∞..0 dB
```

or:

```text
native 0..64
    → semantic -64..0 dB
```

when the mapping is known.

However, the semantic layer must also avoid inventing conversions.

If a device exposes a coefficient `0..0x3fff` and the coefficient-to-dB law is unknown, it is safer to expose:

```text
0..16383, Generic
```

than to invent a fake `-96..0 dB` mapping.

The binding/codec layer is responsible for conversion.

Mappings may be:

- linear;
- nonlinear;
- lookup-table based;
- piecewise;
- sentinel-based.

---

## 15.5 Parameter applicability and dynamic domain

Some parameter semantics depend on configuration.

Example:

- one input source may expose a 10–75 dB microphone gain range;
- another source on the same connector may expose a different range;
- a parameter may be meaningless in a particular mode.

Therefore future architecture may need one of:

- configuration-resolved parameter sets;
- conditional parameter applicability;
- domain replacement during configuration resolution.

The preferred approach is to resolve the **current truthful parameter set/domain** rather than expose impossible values and rely on the UI to guess.

---

# 16. Meter Model

A meter is separate from a parameter.

```cpp
enum class MeterSemantic {
    Level,
    Peak,
    Unknown,
};

struct Meter {
    MeterId id;
    std::variant<NodeId, PortId> target;
    MeterSemantic semantic;
    ScalarDomain domain;
    std::string name;
};
```

### Contract

> One `Meter` represents one scalar telemetry point.

Multi-channel metering is represented by multiple meter objects.

Reasons for keeping meters separate from parameters:

- read-only behavior;
- high update frequency;
- different transport cadence;
- different UI update policy;
- telemetry buffering;
- diagnostics.

---

# 17. Structure vs Runtime State

The semantic model deliberately separates structural truth from mutable state.

```text
STRUCTURE                     STATE

Node                          parameter value
Port                          active route bundles
FixedLink                     meter values
Router
Mixer
Processor
Parameter definition
Meter definition
```

Current state direction:

```cpp
struct RouterState {
    NodeId node;
    std::vector<RouteBundleId> activeBundles;
};

struct DeviceState {
    uint64_t topologyRevision;

    std::vector<RouterState> routers;
    std::unordered_map<ParameterId, ParameterValue> parameters;
    std::unordered_map<MeterId, double> meters;
};
```

### Invariants

- state belongs to one topology revision;
- active bundle IDs exist on the referenced router;
- parameter values conform to current domains;
- meter values belong to existing meter definitions;
- stale state must not be applied after structural replacement.

---

# 18. State Provenance

Some hardware is not fully readable.

ASFW should explicitly model where runtime truth comes from.

```cpp
enum class StateOrigin {
    Observed,
    Cached,
    Derived,
};
```

### Observed

Directly read from hardware.

### Cached

Written by the host but not readable back from hardware.

Example: write-only vendor configuration.

### Derived

Computed from:

- relative encoder events;
- several lower-level registers;
- protocol events;
- current configuration.

This distinction becomes important for:

- UI confidence;
- reconnect behavior;
- bus-reset recovery;
- diagnostics;
- state invalidation.

---

# 19. Dynamic Topology

Dynamic topology is a first-class architectural requirement.

Conceptually:

```text
Capabilities
    +
Configuration
    ↓
Resolver
    ↓
Resolved Topology
```

A device may have:

```text
48 kHz + ADAT
    → 8 optical channels

96 kHz + SMUX
    → fewer optical channels

optical S/PDIF mode
    → stereo S/PDIF port
```

The current topology should contain only the ports and nodes which actually exist.

It should not create eight permanent ADAT channels and merely mark six “disabled” when the hardware no longer exposes them.

---

# 20. Configuration

Configuration is distinct from ordinary runtime control.

Examples of structural configuration:

- sample rate;
- optical mode;
- stream mode;
- clock mode when it changes supported topology;
- high-rate mode;
- hardware operating mode.

Examples of runtime parameter changes:

- gain;
- mute;
- mixer coefficient;
- route selection;
- monitor level.

A useful future distinction may be:

```cpp
enum class ParameterEffect {
    Runtime,
    Configuration,
};
```

or a simpler:

```cpp
bool requiresConfigurationChange;
```

This should be introduced only when the configuration transaction layer needs it.

---

# 21. AudioDriverKit Configuration Transactions

AudioDriverKit provides an operating-system-level structural configuration transaction.

The expected sequence is:

```text
configuration intent or hardware observation
        ↓
coordinator stages one transition
        ↓
request or enter the appropriate ADK configuration window
        ↓
HAL stops I/O
        ↓
PerformDeviceConfigurationChange
        ↓
apply or accept observed hardware configuration
        ↓
confirm hardware state
        ↓
resolve new stream geometry
        ↓
resolve new semantic topology
        ↓
replace streams / controls / bindings
        ↓
atomically commit runtime state and notify UI
        ↓
resume I/O
```

The core invariant is:

> **At every observable committed configuration, stream geometry, semantic topology, routing state, available controls, and Core Audio representation describe the same hardware state.**

ASFW must not publish half-applied configuration.

## 21.1 Configuration Coordinator

Structural configuration is a fallible, bidirectional control-plane operation.
The `ConfigurationCoordinator` is the non-realtime state machine that owns that
operation for one audio device. AudioDriverKit supplies the operating-system
transaction window; it does not own ASFW's hardware policy or decide which
state is authoritative.

The coordinator accepts configuration input from three origins:

- ASFW UI, CLI, or another control client expressing an intent;
- Core Audio through an AudioDriverKit configuration callback; and
- hardware observations, including unsolicited clock/rate/mode changes,
  reconnect, and post-reset rediscovery.

It conceptually owns:

```text
confirmed configuration
desired configuration
pending transition {
    generation/token
    origin
    requested configuration
    prior confirmed configuration
    phase
}
```

This is conceptual ownership, not the in-memory representation. The
implementation uses a closed `std::variant` of states such as idle, awaiting
candidate resolution, awaiting ADK perform, awaiting hardware, awaiting ADK
projection, recovering, and unavailable. Events, effects, and hardware
outcomes are variants as well.
Scalar phase/certainty enums are derived only for logs and wire snapshots; they
must not drive the state machine alongside optional fields that could form
impossible combinations.

There is at most one active structural transition per device. New requests are
validated, rejected, or deliberately coalesced; they must not independently
mutate hardware, ADK objects, and UI state. Every asynchronous completion is
matched against both the transition token and the relevant device/bus
generation so stale completions cannot commit state after reset or reconnect.

The source-of-truth rule is:

> **Desired UI state and Core Audio property values are requests. A
> configuration becomes committed only when it represents hardware-confirmed
> state and its semantic, stream, ADK, runtime, and UI projections agree.**

For readable hardware, confirmation means readback or an authoritative hardware
event. For write-only state, it means successful completion of the native
operation, followed by explicit invalidation on disconnect, bus reset, or any
event that makes the cached value unreliable.

The coordinator is responsible for:

1. normalizing requests from all origins into one configuration intent;
2. resolving and validating a candidate against capabilities and the confirmed
   state without publishing it;
3. serializing the transition and assigning its generation/token;
4. entering or requesting the appropriate ADK configuration window;
5. invoking the family/device backend and awaiting confirmation;
6. resolving topology, stream geometry, bindings, and state migration from the
   confirmed result;
7. updating the ADK projection only in the permitted transaction context;
8. atomically publishing one new committed runtime snapshot;
9. notifying UI/tooling from that committed snapshot; and
10. handling rejection, timeout, ADK abort, disconnect, reset, and unsolicited
    hardware changes without publishing a fictional intermediate state.

The resolver therefore remains useful on both sides of the hardware operation:
before the operation, to reject an impossible candidate and derive a hardware
operation plan; afterward, to resolve and verify the hardware-confirmed result
that may be committed. A valid candidate is not itself current state.

Failure handling depends on what is known. A failure before hardware mutation
preserves the prior committed snapshot. If hardware may have changed but its
result is ambiguous, the coordinator enters recovery and attempts readback or
rediscovery. It must not blindly claim rollback succeeded. If truth cannot be
re-established, the device/configuration is marked unavailable or unknown until
recovery completes.

The coordinator does **not** encode native protocols, calculate topology, own
the realtime stream path, or render UI. Those remain responsibilities of the
hardware backend, resolver, realtime projection, and UI projection
respectively. The state-machine core should depend on narrow ports for those
effects so delayed success, rejection, timeout, stale completion, reset, and
unsolicited-change behavior can be tested without hardware or DriverKit.

The concrete ownership model, states, ports, cross-service ADK handshake,
failure certainty, test matrix, and staged integration plan are specified in
[Device Configuration Coordinator](CONFIGURATION_COORDINATOR.md).

## 21.2 Current hardware-free ADK experiment

`ADKVirtualAudioLab` now publishes four simultaneous Core Audio devices from
the same resolved model used by the CLI: Duet (2 in / 2 out), PHASE 88
(10 / 10), FireWire 1814 (16 / 12), and Saffire Pro 24 DSP (16 / 8). This is
deliberately a projection test: the dormant packet fixture does not define the
HAL channel geometry.

The diagnostic user client can request 44.1 or 48 kHz and optical-mode changes
for each device. The device records `RequestDeviceConfigurationChange`,
`PerformDeviceConfigurationChange`, and `AbortDeviceConfigurationChange`, plus
separate device-rate, output-stream-format, and input-stream-format mutations.
Mutation happens only inside the perform callback. A bounded event ring makes
the transient transaction available to the host and CLI after the callback,
while `[ADKConfig]` and `[ADKConfigHost]` provide driver and host-side traces.

The lab has established the following ADK/Core Audio behavior:

- a user-client request for rate or optical mode enters the ADK configuration
  transaction and publishes the resulting rate and stream-format changes;
- the FireWire 1814's optical ADAT/S/PDIF geometry is reflected by Audio MIDI
  Setup/Core Audio;
- the Saffire Pro 24 DSP's resolved S/PDIF input geometry is 10 capture
  channels while its DAW playback stream remains 8 channels; and
- a rate change initiated outside the lab UI (for example, Audio MIDI Setup or
  a DAW) invokes `HandleChangeSampleRate`. The host observes Core Audio's
  nominal-rate and stream-format properties and refreshes its UI from the
  resulting committed state. `adk hal-rate` exercises this path without using
  the diagnostic user client.

The lab deliberately does **not** claim that every profile's geometry change
has been proven: the Saffire result still needs targeted runtime investigation.
More importantly, it does not validate playback, capture, packet timing, or
the real FireWire hardware/backend transaction. A production configuration
coordinator must make the hardware-confirmed configuration the source of truth,
serialize in-flight requests, and recover coherently from rejection, timeout,
disconnect, and unsolicited hardware changes. See `BENCH.md` for the
executable procedure and CLI oracle.

---

# 22. Capabilities and Discovery

Protocol-family discovery normalizes into the capability layer.

## 22.1 AV/C / BeBoB

Possible discovery sources include:

- `UNIT_INFO`;
- `SUBUNIT_INFO`;
- `PLUG_INFO`;
- stream format descriptors;
- Audio Subunit;
- Music Subunit;
- function blocks;
- selector topology;
- supported sample rates.

## 22.2 DICE

Possible sources include:

- global register space;
- clock configuration;
- TX/RX stream descriptors;
- router capabilities;
- mixer definitions;
- device-specific extension space.

## 22.3 Vendor protocols

Examples:

- Apogee vendor-dependent AV/C;
- M-Audio proprietary configuration block;
- model-specific controls.

These backends may still normalize common facts into the same capability representation.

---

# 23. Device Profiles and Quirks

Generic discovery is preferred, but some devices require hardcoded knowledge.

The architecture treats hardcoded knowledge as an **overlay** rather than a replacement for generic discovery.

```text
discovered capabilities
        +
device profile / quirks
        ↓
effective capabilities
```

Possible quirk categories:

- discovery quirks;
- unsafe-command avoidance;
- capability correction;
- configuration ordering;
- stream geometry quirks;
- control encoding quirks;
- lifecycle quirks;
- bus-reset recovery quirks.

Example pattern:

```text
unknown BeBoB device
    → generic AV/C discovery
    → generic capabilities

known FW1814
    → safe BeBoB discovery
    + profile: avoid unsafe standard control probe
    + proprietary write-only control backend
    → same semantic/resolution/runtime pipeline
```

Family does not imply behavior.

---

# 24. Control Protocol Architecture

The control plane should have three distinct layers:

```text
bytes
 ↓
protocol object
 ↓
family/device meaning
 ↓
semantic operation/state
```

This yields the invariant:

> **SERDE understands protocol representation; family code understands protocol meaning; semantic core understands audio meaning.**

---

## 24.1 Protocol object

Examples:

- AV/C Audio Function Block command;
- AV/C selector command;
- DICE register object;
- Apogee vendor command;
- M-Audio control blob field.

## 24.2 Codec / SERDE

Responsible for:

- byte layout;
- bit fields;
- endianness;
- enum encoding;
- length checking;
- validation.

Not responsible for:

- “microphone gain”;
- “monitor source”;
- “headphone output”;
- UI semantics.

## 24.3 Family/device binding

Responsible for translating:

```text
semantic operation
    ↔
protocol operation(s)
```

One semantic action may require multiple protocol operations.

Phase88 is an important example: one semantic routing choice can require coordinated AV/C selector updates.

---

# 25. Control Bindings

Bindings map semantic identity to hardware implementation.

Conceptually:

```cpp
struct ParameterBinding {
    ParameterId parameter;
    // backend-specific operation descriptor
};

struct RouteBinding {
    NodeId router;
    RouteBundleId bundle;
    // one or several native operations
};
```

Examples:

```text
ParameterId(Input1Gain)
    → Apogee vendor command

ParameterId(StreamMute)
    → AV/C feature function block

ParameterId(LineOutVolume)
    → M-Audio cached configuration field

RouteBundle(OutputSourceMixer)
    → one Apogee source command

RouteBundle(Phase88MixerToOut34)
    → multiple coordinated AV/C selector operations
```

Bindings are not part of the public semantic topology.

---

# 26. Transport Layer

Family meaning and transaction execution must remain separate.

> **Family = what the command/register means. Transport = how the transaction executes.**

Examples:

### AV/C

```text
AV/C command object
    ↓
FCP transport
    ↓
FireWire async transaction
```

### DICE

```text
register operation
    ↓
async read/write transport
```

Transport owns concerns such as:

- serialization execution;
- timeout;
- retry policy;
- cancellation;
- bus generation;
- reset handling;
- request ordering.

---

# 27. Stream/Wire Architecture

Control semantics and realtime data transport are separate.

```text
CONTROL
semantic op
 → protocol binding
 → protocol object
 → codec/SERDE
 → FCP/async

AUDIO DATA
Core Audio buffers
 → encoder/decoder
 → packetizer
 → AMDTP/AM824
 → isochronous transport
```

The packetizer must never contain logic such as:

```cpp
if (device == SaffirePro24DSP && sampleRate == 96000) ...
```

Device-specific stream geometry must already be resolved.

---

# 28. Resolved Stream Plan and Realtime Stream Layout

The architecture explicitly distinguishes between two representations of stream geometry:

```text
resolver/configuration-time representation
vs
representation consumed on every packet in the realtime hot path
```

## 28.1 Configuration-Time Representation

During configuration resolution (while I/O is stopped and the configuration is being built), dynamic containers may be used to construct and validate the stream plan.

```cpp
struct ResolvedStream {
    StreamId id;
    StreamDirection direction;

    uint32_t pcmChannels;
    uint32_t midiChannels;

    SampleRate sampleRate;
    TransmissionMode transmissionMode;

    uint32_t dataBlockSize;
    uint32_t sytInterval;

    // Configuration-time representation only.
    // Not necessarily the representation consumed by realtime packet code.
    std::vector<WireSlot> slots;
};
```

## 28.2 Realtime Projection

Before publication to the realtime audio path, stream geometry is flattened into immutable, bounded, allocation-free tables suitable for direct packet processing.

The packet hot path must never traverse dynamically allocated containers or perform lookups with unbounded indirection.

Conceptually:

```cpp
struct WireSlot {
    // Pre-resolved slot information (e.g. format, offset, channel index)
};

struct RealtimeStreamLayout {
    const WireSlot* slots;
    uint32_t slotCount;

    // or non-owning view:
    // std::span<const WireSlot> slots;
};
```

The underlying storage backing the view is stable, preallocated, and immutable for the lifetime of the committed configuration revision. For bounded channel capacities:

```cpp
template <std::size_t Capacity>
struct FixedSlotTable {
    std::array<WireSlot, Capacity> slots;
    uint32_t count{0};
};
```

### Architectural Contract

> **The resolver may use dynamic containers. Before publication to the realtime path, stream geometry is flattened into immutable, bounded, allocation-free tables suitable for direct packet processing.**

```text
configuration/control plane:
    vector / map / strings / ranges are fine

realtime projection:
    bounded POD-ish tables
    stable addresses
    no allocation
    no mutation
    minimal indirection
```

The packetizer consumes this pre-flattened realtime projection and contains zero device-specific branching.

---

# 29. Semantic Topology ↔ Stream Binding

The semantic graph and the wire plan need an explicit bridge.

Conceptually:

```cpp
struct StreamBinding {
    PortId topologyPort;
    StreamId stream;
    uint32_t channel;
};
```

This prevents the topology and packetizer from silently disagreeing.

A committed configuration should make it possible to answer:

```text
Which semantic host port corresponds to which wire channel?
Which physical semantic input becomes which capture slot?
Which Core Audio stream channel maps to which topology port?
```

without device-specific code in the consumer.

---

# 30. Core Audio / AudioDriverKit Projection

AudioDriverKit is a **projection** of the resolved semantic/configuration model.

The projection layer may create:

- `IOUserAudioDevice`;
- streams;
- controls;
- selectors;
- level controls;
- booleans;
- format/rate representations.

It should not make protocol decisions.

It should not ask:

- which AV/C function block implements this control;
- which DICE offset to write;
- whether the device is Focusrite or Apogee.

It should consume:

- resolved stream geometry;
- semantic parameters;
- semantic current topology;
- state;
- configuration-change events.

---

# 31. SwiftUI / Tooling Projection

The SwiftUI side should consume the same semantic configuration/state used by AudioDriverKit.

It should not require device names for basic rendering.

Generic UI can be generated from:

- endpoint nodes;
- routers;
- mixers;
- processors;
- parameter definitions;
- meter definitions.

Device-specific UI extensions remain allowed for features outside the common vocabulary.

This makes the UI an architecture debugger:

- graph visualization;
- active route display;
- mixer matrix;
- parameter inspection;
- stream binding inspection;
- topology revision;
- live meters;
- control provenance.

---

# 32. Diagnostics and Escape Hatches

Reverse engineering and low-level device support require capabilities that do not belong in the semantic audio model.

Examples:

- raw AV/C explorer;
- DICE register inspector;
- vendor command console;
- firmware UART/mailbox console;
- FireWire transaction trace;
- packet dump;
- stream-slot visualization;
- bus-reset diagnostics;
- protocol object inspector.

These should be first-class diagnostics, but **outside `AudioModel`**.

The semantic model should not gain concepts such as `FirmwareConsoleNode` merely because one device exposes a debugging UART.

---

# 33. Dependency / Ignorance Rules

The architecture is intentionally abstraction-heavy only where each abstraction hides a real axis of variability.

The practical rule is:

> **Each abstraction should enforce an ignorance boundary.**

## 33.1 Semantic model

May know:

- nodes;
- ports;
- routes;
- mixers;
- processors;
- parameters;
- meters;
- state.

Must not know:

- AV/C;
- DICE;
- FireWire transactions;
- AudioDriverKit;
- SwiftUI;
- packet formats.

## 33.2 Family layer

May know:

- protocol objects;
- device semantics;
- discovery rules;
- quirks.

Must not know:

- SwiftUI presentation;
- Core Audio object layout.

## 33.3 SERDE

May know:

- bytes;
- bit fields;
- endianness;
- protocol layout.

Must not know:

- “mic gain”;
- “monitor mixer”;
- “headphone selector”.

## 33.4 Packetizer

May know:

- pre-flattened realtime stream layout;
- bounded slot layout;
- timing mode;
- packet rules.

Must not know:

- device model;
- routing UI;
- phantom power;
- protocol control commands;
- dynamic container allocations.

## 33.5 ADK projection

May know:

- semantic topology;
- parameters;
- stream geometry.

Must not know:

- AV/C selector IDs;
- DICE register offsets;
- vendor command encodings.

---

# 34. Modern C++23 Implementation Style

Preferred tools:

### Ownership

Use values by default.

Use `std::unique_ptr` only for real runtime polymorphism or ownership boundaries.

### Closed semantic alternatives

Use:

```cpp
std::variant
std::visit
```

where the core owns and intentionally closes the vocabulary.

### Failure

Use:

```cpp
std::expected<T, E>
```

rather than status-code/out-parameter combinations.

### Optionality

Use:

```cpp
std::optional
```

for genuinely optional semantic data.

### Views

Use:

```cpp
std::span
std::mdspan
```

where non-owning views improve clarity.

`std::mdspan` may be particularly useful for exposing mixer matrices over flat storage.

### Identity

Use strong IDs rather than raw integers or vector positions.

### Fixtures

Use aggregate initialization and designated initializers where readable.

### Protocol Codecs and Byte Order

Protocol codecs should use explicit endian-aware load/store operations. In the DriverKit/IOKit implementation, prefer the platform byte-order primitives where appropriate (`OSReadBigInt*`, `OSWriteBigInt*`, `IOSwap*`). Host-side tests may use the corresponding standard C++ facilities (`std::byteswap`, `std::endian`, `std::bit_cast`, `std::to_underlying`).

The architecture requires that **protocol codecs own endianness conversion**, but does not prescribe the low-level primitive. The semantic layer must remain completely unaware of either choice.

A lightweight wrapper encapsulates both environments:

```cpp
namespace ByteOrder {

uint32_t loadBE32(const std::byte* p) noexcept;
void storeBE32(std::byte* p, uint32_t v) noexcept;

}
```

```text
DriverKit build  → IOSwap / OS byte-order primitives
host tests       → portable standard C++ implementation
```

The protocol codec consumes the wrapper and remains identical in both environments.

### Concepts

Use concepts/`requires` sparingly for codec/provider contracts.

Do not turn the semantic model into template metaprogramming infrastructure.

### Ranges

Use ranges where they make discovery/resolution code clearer.

Do not force range pipelines into realtime code merely for style.

---

# 35. Validation

The topology validator is the executable law book of the semantic model.

```cpp
std::vector<TopologyError> validateAll(const Topology&);
std::expected<void, TopologyError> validate(const Topology&);
```

The implementation belongs in `.cpp`, not a giant inline header.

---

## 35.1 Structural validation

Validate:

- duplicate node IDs;
- duplicate port IDs;
- nonexistent owners;
- zero channel counts;
- invalid endpoint references.

## 35.2 Fixed-link validation

Validate:

- output → input direction;
- endpoint existence;
- channel compatibility;
- no self-link;
- no multiple fixed drivers where illegal.

## 35.3 Router validation

Validate:

- input/output port ownership;
- port direction;
- route membership;
- bundle non-emptiness;
- route compatibility;
- duplicate routes;
- duplicate bundle IDs within router scope;
- valid constraint values.

## 35.4 Mixer validation

Validate:

- crosspoint ID uniqueness;
- input/output ownership;
- direction;
- crosspoint membership;
- duplicate input→output contributions where semantically illegal.

Do not enforce channel-count equality merely because two ports are joined by a mixer crosspoint unless that rule is actually part of the mixer semantics.

## 35.5 Parameter validation

Validate:

- target existence;
- domain consistency;
- enum non-emptiness;
- enum value uniqueness;
- positive step;
- min ≤ max.

Future state validation should validate actual value/domain compatibility.

## 35.6 State validation

Future:

```cpp
validateState(const Topology&, const DeviceState&)
```

should validate:

- topology revision;
- router identity;
- active bundle identity;
- router constraints;
- one-source-per-output rules;
- route capacity;
- parameter existence;
- parameter domain/value compatibility;
- meter IDs.

---

# 36. Device Specification and Conformance

Testing in ASFW is not merely regression protection, and the methodology extends beyond traditional narrow unit TDD.

The system is built on **machine-readable hardware specifications and conformance testing**:

```text
               Device Specification
                      │
          ┌───────────┼────────────┐
          ▼           ▼            ▼
     capabilities   controls    expected topology
          │           │            │
          └───────────┼────────────┘
                      ▼
                    resolver
                      │
                      ▼
                 conformance
```

TDD functions as an inner development technique within this broader specification framework.

---

## 36.1 Declarative Hardware Specifications

For initial authoring and lab development (V1), device truth can exist as **declarative data** (YAML or JSON) rather than handwritten C++ construction code.

Example conceptual schema:

```yaml
device:
  vendor: Apogee
  model: Duet FireWire

rates:
  - 44100
  - 48000
  - 88200
  - 96000

endpoints:
  physical_inputs:
    - id: xlr-1
      channels: 1
    - id: phone-1
      channels: 1

routers:
  input-source:
    outputs:
      - selected-1
      - selected-2

    bundles:
      - id: xlr-1
        routes:
          - [xlr-1, selected-1]

      - id: phone-1
        routes:
          - [phone-1, selected-1]

controls:
  - id: input-1-gain
    semantic: level
    target: preamp-in-1

mixers:
  monitor:
    inputs: [...]
    outputs: [...]
    crosspoints: [...]
```

This establishes an authoring and validation workflow in `ADKVirtualAudioLab`:

```text
Linux source / reversing / documentation
                 ↓
         YAML/JSON fixture
                 ↓
       parser + schema validation
                 ↓
      semantic Topology/Capabilities
                 ↓
         semantic validator
                 ↓
        ADK / UI / resolver
```

---

## 36.2 The Migration Path

Declarative specifications provide a clean migration path toward real discovery backends, while creating a permanent, human-readable database of the hardware zoo:

```text
V1:

JSON/YAML
   ↓
Capabilities
   ↓
Resolver


Later:

AV/C / DICE discovery
   ↓
Capabilities
   ↓
same Resolver
```

Declarative schemas are parsed in lab tooling, offline generators, and host-side tests—**never in the realtime driver runtime**. In production builds, offline tools can compile these specifications into static/`constexpr` C++ tables.

---

## 36.3 The Validator as Promoted Knowledge

The validator acts as the executable law book that captures evolving domain knowledge.

Rules typically originate as fixture-specific assertions:

```text
Duet:
output source must select exactly one of: stream pair or mixer pair

DICE:
each router destination has at most one active source
```

When a rule is observed repeatedly across diverse hardware families, it graduates from a local fixture check into a universal semantic invariant enforced by the core validator:

```text
device-specific observation
        ↓
fixture assertion
        ↓
seen repeatedly across hardware
        ↓
recognized semantic invariant
        ↓
validator  (e.g. validate(router))
```

---

## 36.4 The Engineering Loop

The engineering loop for supporting hardware follows this sequence:

1. **Describe known hardware truth declaratively** (or via direct semantic fixture).
2. **Resolve it** into the common semantic model.
3. **Validate it** using the semantic validator.
4. **Exercise every meaningful configuration** (exhaustive state sweeps).
5. **Add actual backend/discovery code**.
6. **Require the backend-generated result to match the specification**.
7. **Promote recurring rules** from fixtures into generic validators.

---

# 37. Device Conformance Suites

Each supported device should eventually provide tests covering:

- identity;
- supported sample rates;
- supported clock modes;
- structural configuration modes;
- topology;
- ports;
- fixed links;
- routing legality;
- mixer geometry;
- parameters;
- meters;
- stream geometry;
- state constraints;
- configuration transitions.

Possible organization:

```text
Tests/AudioModel/
    DuetTopologyTests.cpp
    DuetConfigurationTests.cpp
    DuetStateTests.cpp
    DuetTransitionTests.cpp

    Phase88TopologyTests.cpp
    ...
```

The test harness should remain simple until a clear abstraction need appears.

---

# 38. Exhaustive Configuration Testing

For small finite configuration domains, test every combination.

Example:

```cpp
for (auto rate : allSampleRates)
for (auto optical : allOpticalModes)
for (auto clock : allClockSources)
for (auto input1 : allInput1Sources)
for (auto input2 : allInput2Sources)
for (auto output : allOutputSources) {

    auto resolved = resolve(capabilities, {
        .rate = rate,
        .optical = optical,
        .clock = clock,
        .input1 = input1,
        .input2 = input2,
        .output = output,
    });

    REQUIRE(resolved.has_value());
    REQUIRE(validate(resolved->topology).has_value());

    verifyExpectedSemantics(*resolved);
}
```

Thousands of combinations are cheap.

This is valuable because device modes are often not orthogonal.

A bug may exist only for:

```text
96 kHz + external clock + optical S/PDIF + mixer monitor
```

even though each option works independently.

---

# 39. Property Testing for Huge State Spaces

Exhaustive runtime routing is impossible for large matrices.

A 46×46 router with many legal routes has an astronomical state space.

For those cases test **properties**.

Generate valid states and assert:

```text
active bundles exist
route table capacity respected
destination fan-in respected
source fan-out respected
all ports belong to current topology
state validates against current revision
```

Generate invalid states:

```text
unknown bundle ID
bundle from another router
stale topology revision
two illegal sources on one destination
too many active entries
unknown parameter
out-of-domain parameter value
```

and require rejection.

A dedicated property-testing library is optional initially.

Deterministic generators are enough to establish the methodology.

---

# 40. Configuration Transition Testing

Static topology tests are insufficient.

Real driver failures often occur during transitions.

Test:

```text
44.1 kHz → 48 kHz
48 kHz → 96 kHz
96 kHz → 48 kHz
ADAT → S/PDIF
S/PDIF → ADAT
internal → external clock
external → internal
```

A transition test should eventually cover:

1. old resolved configuration;
2. request origin and normalized intent;
3. coordinator generation/token and phase changes;
4. requested new configuration;
5. hardware operation plan and confirmation;
6. new resolved configuration;
7. state migration/reset rules;
8. stream replacement;
9. ADK control/stream replacement;
10. atomic runtime snapshot publication;
11. UI topology replacement; and
12. final agreement between hardware, resolver, ADK, runtime, and UI state.

Coordinator tests must also cover:

```text
duplicate request for the already-confirmed configuration
second request while another transition is active
delayed hardware success
hardware rejection
hardware timeout before mutation
ambiguous timeout after possible mutation
ADK abort before and after hardware mutation
disconnect or bus reset during every transition phase
stale completion from a prior device/bus generation
unsolicited hardware configuration change
write-only cached state invalidation
UI-origin and Core-Audio-origin requests converging on the same result
```

---

# 41. Protocol Contract Tests

Protocol bindings should have deterministic test vectors.

Examples:

```text
semantic parameter
    ↓
expected protocol object
    ↓
expected serialized bytes
```

and inverse:

```text
captured bytes
    ↓
decoded protocol object
    ↓
expected semantic state
```

Useful sources:

- historical Linux implementations;
- FFADO;
- packet captures;
- vendor documentation;
- device logs;
- firmware console output;
- reverse-engineered command traces.

The test corpus should preserve discovered protocol knowledge independently of the runtime implementation.

---

# 42. Adding New Hardware: Engineering Procedure

Supporting a new device should follow a defined process.

## Phase 1 — Collect facts

Gather:

- identity/GUID/vendor/model;
- protocol family;
- stream format;
- clock behavior;
- sample rates;
- physical ports;
- host channels;
- routing;
- mixer geometry;
- DSP;
- controls;
- meters;
- quirks;
- configuration transitions.

Sources should be classified as:

- documented;
- observed;
- inferred;
- reverse-engineered;
- uncertain.

## Phase 2 — Write declarative device specification / expected fixture

Before implementing protocol code, define the device truth as a declarative specification (YAML/JSON) or direct semantic fixture in `ADKVirtualAudioLab`.

```yaml
# Device specification fixture (e.g. Duet.yaml)
```

or in C++:

```cpp
Topology makeExpectedDeviceXTopology(...);
```

The fixture acts as the semantic oracle.

## Phase 3 — Validate and promote invariants

Run the fixture through the semantic validator. If the device exposes novel constraints or invariants that hold across audio topologies, promote them from local fixture checks into core `validate()` invariants.

## Phase 4 — Enumerate structural configurations

If configuration-dependent, create fixtures for all meaningful configurations.

## Phase 5 — Add capability fixture

```cpp
auto caps = makeDeviceXCapabilitiesFixture();
auto actual = resolve(caps, config);
auto expected = makeExpectedDeviceXTopology(config);

CHECK(semanticallyEquivalent(actual.topology, expected));
```

## Phase 6 — Implement discovery

Replace fixture facts with real discovery:

```cpp
auto caps = DeviceXCapabilityDiscovery{device}.discover();
```

Everything downstream should remain unchanged.

## Phase 7 — Implement bindings

Map semantic parameters/routes to native operations.

## Phase 8 — Implement stream resolution

Produce resolved wire plan and bindings.

## Phase 9 — E2E virtual ADK test

Feed the resolved device through:

- virtual AudioDriverKit driver;
- SwiftUI topology/mixer UI;
- state mutation;
- configuration changes.

## Phase 10 — Hardware validation

Compare:

- discovered capabilities;
- actual routing;
- control response;
- stream geometry;
- state;
- meters;
- bus reset/reconfiguration behavior.

---

# 43. ADKVirtualAudioLab as Architecture Incubator

`ADKVirtualAudioLab` is not merely a unit-test project.

It is the ideal architecture incubator because it already contains:

- a small virtual AudioDriverKit driver;
- a SwiftUI UI;
- host-side test infrastructure;
- much less unrelated complexity than the full ASFWDriver.

The core semantic model should live in a dependency-free target inside this project first.

Suggested organization:

```text
ADKVirtualAudioLab/
    Core/
        AudioModel/
            Id.hpp
            Node.hpp
            Port.hpp
            Link.hpp
            Router.hpp
            Mixer.hpp
            Processor.hpp
            Parameter.hpp
            Meter.hpp
            Topology.hpp
            State.hpp
            Validate.hpp
            Validate.cpp

    Tests/
        AudioModel/
            DuetTopologyTests.cpp
            Phase88TopologyTests.cpp
            FW1814TopologyTests.cpp
            SaffireTopologyTests.cpp

    Driver/
        ADK projection

    Lab/
        SwiftUI / visualization / control

    Protocols/
        future test codecs / backends
```

The virtual lab should allow the same semantic device to drive both:

```text
Topology + State
       │
   ┌───┴────┐
   ▼        ▼
ADK      SwiftUI
```

Neither consumer should need the device model name.

---

# 44. Representative Device Pressure Tests

The current semantic vocabulary is being pressure-tested against four deliberately different devices.

---

## 44.1 Apogee Duet FireWire

Important characteristics:

- vendor-dependent OXFW control;
- independent source choice for each analog input;
- two-channel preamp;
- 4×2 low-latency mixer;
- stereo output source selection;
- master output level/mute;
- meters;
- physical knob/display behavior.

Architectural lessons:

- independent one-route bundles vs coupled stereo bundles;
- mixer crosspoint parameters;
- parameters targeting specific channel ports;
- output stage should not be modeled as router semantics;
- vendor UI/display behavior may live outside the common audio model.

---

## 44.2 Terratec Phase 88 FW

Important characteristics:

- BeBoB / AV/C;
- direct capture paths;
- 12×2 monitor mixer;
- one selected playback source;
- mixer output can be assigned to one destination;
- chained AV/C selectors;
- clock source selection involving multiple function blocks.

Architectural lessons:

- one semantic route can require several protocol writes;
- transport/protocol representation must not leak into topology;
- stereo/coupled selection is hardware-semantic;
- routing constraints can involve destination exclusivity.

---

## 44.3 M-Audio FireWire 1814

Important characteristics:

- BeBoB-like streaming;
- proprietary unsafe/write-only control plane;
- 22×4 main mixer — eleven stereo input pairs (4 analog, 1 S/PDIF, 4 ADAT,
  2 stream) feeding **two** stereo mixer outputs;
- 22×2 aux mixer — the same eleven pairs feeding one stereo aux bus, with its
  own independent set of input gains;
- output/headphone source selectors — each analog output pair chooses its mixer
  output or aux; each headphone pair chooses mixer 1, mixer 2 or aux;
- extensive input/output/aux gains;
- balances — physical inputs only; the parameter window has no stream balance;
- meters — 19 stereo points;
- sync state;
- front-panel events;
- configuration state may need host caching.

Stream geometry is configuration-dependent and decomposes as
`capture = 8 analog + digital_in` and `playback = 4 analog + digital_out`, where
the digital half is one pair for either S/PDIF variant and eight channels of
ADAT that S/MUX halves to four above 48 kHz.

Architectural lessons:

- mixer and router must remain different primitives;
- **level and crosspoint existence are separate placements** — the 1814 holds
  one gain per mixer *input channel*, shared by both mixer outputs, while the
  crosspoints are the on/off bits of two routing masks. §13.1 in practice;
- control state provenance matters;
- family identity does not guarantee safe standard control;
- output selectors belong in semantic routing;
- native control transport can remain entirely proprietary.

---

## 44.4 Focusrite Saffire Pro 24 DSP

Important characteristics:

- DICE;
- large 46×46 router;
- up to roughly 128 route entries;
- 18×16 mixer;
- DSP channel-strip/reverb resources;
- output group;
- sample-rate-dependent routing/stream geometry;
- optical mode changes available topology.

Architectural lessons:

- large routing fabrics require property/invariant tests;
- topology is configuration-dependent;
- router and mixer must remain first-class;
- processor nodes may initially be partially opaque;
- packetizer needs a resolved stream plan, not device-specific branches.

---

# 45. Anti-Overgeneralization and the Finite Hardware Universe

A historical concern in FireWire audio support is that devices vary so much in wire protocol and UI requirements that one universal abstraction may be impossible.

ASFW accepts that warning, with a critical realization:

> **The hardware universe is effectively finite, but our knowledge of it is incomplete.**

We do not need to design an infinitely extensible generic audio ontology capable of describing devices invented in 2045. We need to describe a finite historical zoo well.

The architecture therefore does not require that every device feature fit into `Parameter + Mixer + Router`.

Instead:

> Every feature that genuinely belongs to the shared audio vocabulary should use the shared vocabulary. Device-specific features may live in explicit, typed extensions without polluting the core.

Examples outside the core:

- firmware console;
- front-panel display animation;
- knob-target mode;
- vendor-specific VRM editor;
- firmware updater;
- vendor diagnostics;
- unusual hardware-button behavior.

The supported hardware domain is historically finite. ASFW may therefore eventually model essentially every relevant device feature. This does not imply that every feature belongs in the common semantic core. V1 deliberately models the broadly reusable audio semantics first. Device-specific behavior may initially live in typed extension models and may later be promoted into common semantics when multiple devices demonstrate that it represents a genuine shared concept.

Complete coverage does not require everything to become generic. An architecture with:

```text
95% common semantic language
5% explicit typed historical-device extensions
```

is a completely finished, robust architecture. Some extensions (such as model-specific front-panel knob routing) can remain extensions forever rather than inventing synthetic abstractions like `ParameterSemantic::ApogeeKnobTarget` just to appear unified.

---

# 46. Extension Policy

The progression of hardware understanding follows three stages:

```text
V1 COMMON CORE
Port, Router, Mixer, Processor, Parameter, Meter...
        ↓
DEVICE EXTENSIONS
knob-target mode, display behavior, VRM specifics, vendor DSP controls, special front-panel logic...
        ↓
LATER, AFTER MANY DEVICES
if several extensions reveal the same concept: promote it into shared semantics
```

When new hardware cannot be modeled with the current primitives, ask these questions in order:

### 1. Is the hardware understanding correct?

Verify protocol behavior.

### 2. Is the fixture correct?

Avoid encoding assumptions from another device.

### 3. Can the behavior be composed from existing primitives?

Prefer composition over new taxonomy.

### 4. Is this concept genuinely common audio semantics?

If yes, consider a new core primitive.

### 5. Is it vendor-specific?

Keep it in a typed extension/backend/UI-specific layer.

### 6. Would adding it violate an ignorance boundary?

If yes, it is in the wrong layer.

---

# 47. Concurrency, Realtime Boundaries, and Telemetry

The semantic model is control-plane state and should not be directly mutated from realtime audio callbacks.

Realtime code consumes immutable, pre-flattened snapshots published from the control plane.

Recommended direction:

```text
control/config thread
    builds new immutable snapshot
                ↓
       atomic snapshot publication
                ↓
realtime stream path reads stable plan
```

Realtime code strictly avoids:

- dynamic memory allocation;
- locks or priority-inversion hazards;
- protocol I/O or bus communication;
- graph traversal requiring dynamic mutation;
- UI notifications;
- unbounded logging or string formatting.

---

## 47.1 Realtime Telemetry Architecture

Telemetry collection may occur directly in realtime hot paths and is often most useful there. Realtime instrumentation must remain bounded and RT-safe: atomic counters, preallocated event buffers, lock-free queues/rings, timestamps, and similar constant-cost operations are appropriate. Telemetry aggregation, formatting, persistence, logging, UI publication, and other I/O must happen outside the realtime path.

Architecturally:

```text
             HOT PATH
                │
      ┌─────────┴──────────┐
      │                    │
 audio processing     RT telemetry
                           │
                    atomic counters
                    fixed event slots
                    lock-free ring
                    timestamps
                           │
                           ▼
                non-RT telemetry consumer
                           │
             ┌─────────────┼─────────────┐
             ▼             ▼             ▼
         Instruments     SwiftUI        logs
         metrics         graphs         files
```

This reflects the existing telemetry design pattern preserved in ASFW:

> **Instrumentation is allowed in the realtime path; unbounded telemetry work and telemetry I/O are not.**

---

# 48. Snapshot and Revision Model

A future committed runtime snapshot, published only by the configuration
coordinator after confirmation, may conceptually be:

```cpp
struct AudioRuntimeSnapshot {
    std::shared_ptr<const ResolvedAudioConfiguration> configuration;
    std::shared_ptr<const DeviceState> state;
};
```

Exact ownership strategy remains implementation-specific.

Important semantics:

- configuration has revision;
- state names the revision it belongs to;
- structural replacement creates a new revision;
- stale events can be rejected;
- ADK/UI can observe coherent snapshots.

For realtime code, a more specialized lock-free publication mechanism may replace general shared ownership if profiling requires it.

---

# 49. Error Model

Errors should remain typed and layer-specific.

Examples:

```text
TopologyError
CapabilityDiscoveryError
ProtocolDecodeError
TransportError
ConfigurationError
BindingError
StreamPlanError
```

Do not return one universal `ASFWError` carrying strings from every layer.

Use `std::expected` at layer boundaries.

Errors should contain enough structured context for diagnostics:

- device identity;
- operation;
- semantic target;
- protocol target;
- bus generation;
- current topology revision.

---

# 50. Observability

ASFW should be unusually observable.

Useful debug views:

- current topology graph;
- current configuration;
- capability dump;
- active router bundles;
- mixer crosspoint state;
- parameter state + provenance;
- stream plan;
- semantic-to-wire bindings;
- semantic-to-protocol bindings;
- meter state;
- FireWire transactions;
- packet timing;
- bus reset history;
- ADK projection.

A major project advantage can be that protocol reverse engineering, driver debugging, and semantic inspection are integrated rather than scattered across unrelated tools.

---

# 51. Testing Layers

The test architecture should eventually include:

```text
Level 1: semantic data-structure tests
Level 2: topology validation tests
Level 3: device fixture conformance
Level 4: capability → resolver tests
Level 5: semantic state mutation tests
Level 6: protocol codec tests
Level 7: semantic binding tests
Level 8: stream-plan / packetizer tests
Level 9: configuration transition tests
Level 10: virtual ADK E2E tests
Level 11: hardware integration tests
```

Each level should depend on fewer assumptions than the level above it.

---

# 52. Development Roadmap

## Phase A — Semantic core

Current focus.

Deliver:

- strong IDs;
- node variant;
- ports;
- fixed links;
- router + route bundle;
- mixer + crosspoints;
- processor;
- parameter;
- meter;
- topology;
- state;
- validator.

## Phase B — Real-device semantic fixtures

Finalize truthful fixtures for:

- Duet;
- Phase88;
- FW1814;
- Saffire Pro 24 DSP.

Avoid protocol/discovery implementation in this phase.

## Phase C — State validation and mutations

Add:

- active route bundle mutation;
- parameter mutation;
- topology-revision checks;
- meter update;
- provenance.

## Phase D — Capability IR

Design capability schema from the already-tested semantic truth.

## Phase E — Resolver

Implement:

```text
Capabilities + Configuration → ResolvedAudioConfiguration
```

Use direct handwritten topologies as golden oracles.

## Phase F — Virtual ADK E2E

Project topology/streams/controls into the virtual AudioDriverKit device and SwiftUI.

## Phase G — Protocol codecs and bindings

Integrate:

- AV/C;
- FCP;
- DICE;
- OXFW;
- M-Audio special control.

## Phase H — Real hardware discovery

Replace capability fixtures with real discovery providers.

## Phase I — Full ASFWDriver integration

Move mature semantic/runtime components from the lab into the production audio stack.

---

# 53. Open Questions

The following should remain explicit rather than silently decided.

## 53.1 Parameter conditionality

How should parameter applicability/domain change with routing or configuration?

## 53.2 Channel layout

Is `uint32_t channels` sufficient for all hardware, or do we eventually require richer layouts/groups?

## 53.3 Route-bundle identity scope

Router-local is currently preferred.

## 53.4 State migration

Which values survive topology/config revision changes?

Examples:

- monitor volume;
- route selection;
- mixer coefficients;
- optical mode-dependent parameters.

## 53.5 Clock semantic model

Should clocking become a first-class semantic subsystem distinct from generic parameters?

Likely fields:

- supported sources;
- selected source;
- nominal rate;
- detected external rate;
- lock status;
- sync domain.

## 53.6 Device-specific semantic extensions

What is the extension mechanism for features intentionally outside the common vocabulary?

Possible options:

- separate extension model;
- typed per-device capability object;
- opaque extension tree;
- UI/backend-specific extension API.

Do not solve this until a real E2E requirement appears.

## 53.7 Persistence

Which IDs must remain stable across runs versus only topology revisions?

---

# 54. Architecture Success Criteria

The architecture is successful if:

1. New devices can usually be described by composition of the existing semantic primitives.
2. New protocol backends do not require changes in ADK/UI code.
3. New UI views do not require protocol-specific code.
4. Packetizer logic does not branch on device model.
5. Every structural configuration origin passes through one coordinator per
   device and commits only hardware-confirmed state.
6. Configuration changes publish coherent topology/stream/control snapshots.
7. Device-specific quirks remain localized.
8. Hardware-specific extensions do not pollute the semantic core.
9. Every supported device has executable conformance tests.
10. New hardware can falsify and improve the architecture without destabilizing previous devices.
11. The architecture remains debuggable and inspectable.

---

# 55. Core Invariants Summary

```text
The resolver may use dynamic containers; the realtime projection is bounded, allocation-free, and immutable.

A FixedLink connects compatible output → input ports.

A Router never sums signals.

A Mixer may combine multiple signals.

A Processor transforms signals but is neither routing nor summation.

A Port is a connectable signal boundary.

A Route is one possible router connection.

A RouteBundle is an atomically controlled set of routes.

A Parameter exposes semantic state, not protocol-native encoding unless no better semantic mapping is known.

A Meter is one scalar telemetry point.

Runtime state belongs to one topology revision.

Ports exist only when present in the current resolved configuration.

The same committed configuration feeds ADK, UI, state, and streaming.

Exactly one coordinator owns each device's structural configuration transition; requested or candidate state never becomes current until hardware confirmation and atomic projection commit.

Device descriptions begin as declarative specifications; recurring rules graduate into generic validators.

The FireWire hardware universe is historically finite; common vocabulary models shared semantics, while device extensions handle unique historical features.

Device/family protocol knowledge never appears in the semantic core.

Protocol codecs own endianness and byte layout; the semantic core remains unaware of low-level byte-order primitives.

The packetizer consumes resolved, flattened stream geometry, not device identity.

Realtime telemetry collection is allowed and encouraged via bounded, RT-safe primitives; telemetry I/O, formatting, and persistence are strictly off-path.

No common abstraction is assumed universal.

A device-specific feature may remain device-specific.
```

---

# 56. Engineering Philosophy

ASFW should prefer **falsifiable architecture** over speculative architecture.

The process is:

```text
build the smallest coherent semantic language
            ↓
describe a real device
            ↓
make the device try to break the language
            ↓
fix the model only when reality requires it
            ↓
keep all previous device tests passing
```

The project should not aim to predict every FireWire audio interface in advance.

It should aim to make architectural mistakes cheap to discover and difficult to reintroduce.

The most important artifact for each supported device is therefore not merely the backend implementation.

It is the combination of:

```text
hardware facts
+ semantic fixture
+ configuration matrix
+ protocol vectors
+ conformance tests
+ runtime implementation
```

That combination turns reverse-engineered knowledge into an executable specification and makes the unified driver maintainable as the supported hardware zoo grows.

---

# 57. Short Architectural Definition

If the entire design must be reduced to one paragraph:

> **ASFW is a hardware-driven, configuration-resolved audio architecture in which family-specific discovery and protocol bindings are normalized into a small semantic model of endpoints, ports, routers, mixers, processors, parameters, and meters. One non-realtime coordinator per device serializes structural requests from UI, Core Audio, and hardware, and commits only hardware-confirmed state. The current configuration is resolved together with wire-stream geometry into one committed revision, flattened into bounded allocation-free tables for realtime streaming and projected coherently into AudioDriverKit, SwiftUI, and runtime state. Device quirks and vendor-specific features remain below or beside the common model in typed extensions rather than contaminating it. Every supported device acts as a conformance test for the semantic language, starting as a declarative specification whose recurring rules graduate into core validators, backed by exhaustive configuration sweeps and property/invariant testing.**
