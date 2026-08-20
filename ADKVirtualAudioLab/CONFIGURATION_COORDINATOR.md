# Device Configuration Coordinator

## Status

Design baseline for implementation in `ADKVirtualAudioLab`, followed by
integration into ASFW. This design covers the structural control plane only;
it does not add playback or capture behavior.

## 1. Decision

ASFW will have one `DeviceConfigurationCoordinator` per published audio
endpoint. It is the sole owner of structural configuration state and of the
transition from one coherent committed configuration to another.

The coordinator lives on the ASFW core side, where hardware identity, route
generation, protocol lifetime, and hardware confirmation are authoritative.
The AudioDriverKit device is an adapter that obtains the host's configuration
window and projects a coordinator-confirmed result into ADK objects.

The existing `AudioDuplexCoordinator` remains the hardware/transport actuator.
It already owns device clock application, duplex stop/start, route-generation
validation, timeout handling, and device-family protocol dispatch. The new
coordinator composes it; it does not duplicate its restart state machine.

```text
                            ASFW core service

 UI/user client ─────┐
 hardware event ─────┼──> DeviceConfigurationCoordinator
 ADK intent RPC ─────┘           │
                                 ├── pure resolver
                                 ├── AudioDuplexCoordinator / hardware backend
                                 ├── runtime snapshot store
                                 └── ADK notification port
                                          │
                              versioned bounded IIG messages
                                          │
                                          ▼
                            AudioDriverKit device service

                             ADK transaction adapter
                             + ADK object projection
```

This placement avoids two authorities. `ASFWAudioDevice` must not maintain a
separate current rate while `AudioCoordinator`, `AudioEndpointRuntime`, and the
hardware backend maintain competing copies.

## 2. Source of Truth

The coordinator distinguishes four concepts:

- **desired** — what a client asked for;
- **candidate** — a desired configuration that resolves and validates;
- **confirmed** — what hardware readback or a successful native operation says
  the device is using; and
- **committed** — a confirmed configuration whose resolver, ADK, runtime, and
  UI projections have all completed successfully.

A desired or candidate configuration is never exposed as current state.

For readable hardware, confirmation is readback or an authoritative hardware
notification. For write-only state, confirmation is successful completion of
the native operation. Such cached confirmation is invalidated by bus reset,
disconnect, route replacement, or any event that makes the write's continuing
effect uncertain.

## 3. Scope of Structural Configuration

The coordinator operates on one protocol-neutral value. V1 exercises sample
rate and optical modes, but the type must leave room for the full structural
domain without turning every field into a separate transaction path.

```cpp
struct DeviceConfiguration final {
    uint32_t sampleRateHz{0};
    ClockSourceId clockSource{};
    OpticalMode opticalInput{OpticalMode::kUnavailable};
    OpticalMode opticalOutput{OpticalMode::kUnavailable};
    StreamMode streamMode{StreamMode::kUnspecified};
};
```

The exact field types will reuse the semantic model. Unsupported or
inapplicable fields are explicit values, not magic integers.

Ordinary controls such as gain, mute, routes, and mixer coefficients do not go
through this coordinator unless changing them alters stream geometry, buffer
geometry, available controls, clocking, or another I/O-related structure.

## 4. Identity and Revisions

Every operation carries three independent identities:

```cpp
struct ConfigurationIdentity final {
    AudioEndpointId endpointId{};
    uint64_t transactionToken{0};
    FW::Generation routeGeneration{0};
};
```

- `endpointId` selects the runtime endpoint.
- `transactionToken` identifies one coordinator transition.
- `routeGeneration` rejects completions from a device instance that was reset,
  disconnected, or rebound while work was in flight.

Committed snapshots also have a monotonically increasing configuration
revision. A transaction token is not a revision: failed and aborted
transactions consume tokens but do not create committed revisions.

## 5. Request Origins

```cpp
enum class ConfigurationOrigin : uint8_t {
    kControlClient,
    kCoreAudio,
    kHardwareObservation,
    kInitialDiscovery,
    kRecovery,
};
```

Origin is diagnostic and policy information. It never changes the meaning of
the requested configuration.

- A control client expresses intent and requires a driver-initiated ADK
  configuration transaction.
- Core Audio enters through `HandleChangeSampleRate` or another ADK property
  callback. V1 treats this as a synchronous rate-request callback only when the
  device is observed idle; it is not assumed to be equivalent to
  `PerformDeviceConfigurationChange` for arbitrary structural mutation.
- A hardware observation reports a state that may already have taken effect.
  If it affects active I/O, transport is quiesced immediately and ADK is then
  synchronized through a driver-initiated configuration transaction.

## 6. State Model

The internal state is a discriminated union. An enum plus optional fields would
permit impossible combinations such as `Idle` with a pending transaction or
`ProjectingADK` without a confirmed projection plan.

```cpp
using CommittedHandle =
    std::shared_ptr<const CommittedAudioConfiguration>;
// Constructed only by the commit factory; non-null in every state that
// requires a coherent revision.

struct TransitionContext final {
    ConfigurationIdentity identity{};
    ConfigurationOrigin origin{ConfigurationOrigin::kControlClient};
    DeviceConfiguration requested{};
    std::shared_ptr<const ResolvedAudioConfiguration> candidate{};
    CommittedHandle prior{};
};

struct ApplyCandidate final {};
struct ProjectObserved final {
    ConfirmedHardwareConfiguration confirmed{};
};
using PendingWork = std::variant<ApplyCandidate, ProjectObserved>;

struct Uninitialized final {};
struct Idle final {
    CommittedHandle committed{};
    std::optional<ConfigurationFailure> lastFailure{};
};
struct AwaitingCandidate final {
    ConfigurationIdentity identity{};
    ConfigurationOrigin origin{ConfigurationOrigin::kControlClient};
    DeviceConfiguration requested{};
    CommittedHandle prior{};
};
struct AwaitingADKPerform final {
    TransitionContext transition{};
    PendingWork work{};
};
struct AwaitingHardware final {
    TransitionContext transition{};
    uint64_t deadlineTicks{0};
};
struct AwaitingADKProjection final {
    TransitionContext transition{};
    ConfirmedProjectionPlan plan{};
};
struct Recovering final {
    CommittedHandle lastCoherent{}; // nullable only before the first commit
    std::optional<ConfigurationIdentity> failedIdentity{};
    RecoveryCause cause{};
};
struct Unavailable final {
    CommittedHandle lastCoherent{}; // nullable only before the first commit
    ConfigurationFailure failure{};
};

using DeviceConfigurationState = std::variant<
    Uninitialized,
    Idle,
    AwaitingCandidate,
    AwaitingADKPerform,
    AwaitingHardware,
    AwaitingADKProjection,
    Recovering,
    Unavailable>;
```

The implementation may store large resolved objects behind immutable owned
handles. No pointer to such an object crosses an `IOService` boundary.

`Committing` is intentionally not a durable state. A successful projection
event constructs the new immutable committed value and transitions directly
from `AwaitingADKProjection` to `Idle`; snapshot publication is an effect of
that transition. Likewise, `ApplyingHardware` is represented by
`AwaitingHardware`, because the durable fact is that an operation was issued
and a token/generation-matched completion is pending.

Events and effects are also closed variants:

```cpp
using ConfigurationEvent = std::variant<
    ControlIntent,
    CoreAudioRateIntent,
    HardwareObserved,
    CandidateAccepted,
    CandidateRejected,
    ADKPerformGranted,
    HardwareCompleted,
    ProjectionFinished,
    ADKAborted,
    RouteInvalidated,
    DeadlineExpired,
    RecoveryCompleted>;

using ConfigurationEffect = std::variant<
    ResolveCandidate,
    RequestADKWindow,
    ApplyHardware,
    ProjectADK,
    PublishSnapshot,
    QuiesceTransport,
    ObserveHardware,
    MarkProjectionUnavailable,
    EmitDiagnostic>;

struct TransitionResult final {
    DeviceConfigurationState next;
    FixedEffectList<ConfigurationEffect, kMaxEffectsPerTransition> effects;
};

[[nodiscard]] std::expected<TransitionResult, StateMachineError>
Reduce(const DeviceConfigurationState& state,
       const ConfigurationEvent& event) noexcept;
```

The reducer is dependency-free. The coordinator shell serializes events,
executes returned effects through the ports, and feeds completions back as new
events. `std::visit` makes handling explicit for each valid state/event pair.
An invalid pair returns a typed error and no effects.

There is at most one pending structural transition per endpoint. V1 uses this
deterministic admission policy:

- a request equal to committed state succeeds as a no-op;
- a request equal to the active candidate coalesces onto the existing token;
- a different request while one is pending returns busy; and
- an authoritative hardware observation invalidates the pending request and
  starts synchronization or recovery from the observed state.

V1 deliberately has no last-writer-wins queue. Superseding a request that HAL
believes it is currently performing creates unclear ownership and poor failure
semantics.

## 7. Hardware Result Certainty

An `IOReturn` alone is not enough to decide recovery. Every hardware operation
returns a variant whose alternative carries only the data valid for that
outcome:

```cpp
struct HardwareUnchanged final {
    IOReturn status{kIOReturnError};
};
struct HardwareConfirmedRequested final {
    ConfirmedHardwareConfiguration confirmed{};
};
struct HardwareConfirmedOther final {
    IOReturn status{kIOReturnSuccess};
    ConfirmedHardwareConfiguration confirmed{};
};
struct HardwareUnknown final {
    IOReturn status{kIOReturnError};
};

using HardwareConfigurationOutcome = std::variant<
    HardwareUnchanged,
    HardwareConfirmedRequested,
    HardwareConfirmedOther,
    HardwareUnknown>;
```

The coordinator handles results as follows:

| Certainty | Coordinator action |
|---|---|
| `kUnchanged` | Fail the request and retain the prior committed snapshot. |
| `kConfirmedRequested` | Resolve the confirmed result and continue to ADK projection. |
| `kConfirmedOther` | Treat the observed value as hardware truth and synchronize projections to it. |
| `kUnknown` | Enter recovery; do not claim either the old or requested configuration is current. |

If ADK projection fails after hardware is confirmed, the endpoint is
temporarily incoherent. V1 enters recovery and does not automatically write the
old configuration back. A compensating hardware operation is allowed only when
the family backend explicitly declares it safe and confirms its result.

## 8. Ports

The state-machine core depends on narrow, protocol-neutral ports:

```cpp
class IConfigurationResolver {
public:
    virtual std::expected<ResolvedAudioConfiguration, ConfigurationError>
    Resolve(const AudioDeviceCapabilities& capabilities,
            const DeviceConfiguration& configuration) const noexcept = 0;
};

class IHardwareConfigurationPort {
public:
    virtual HardwareConfigurationOutcome ApplyAndConfirm(
        const ConfigurationIdentity& identity,
        const DeviceConfiguration& requested,
        const ResolvedAudioConfiguration& candidate) noexcept = 0;

    virtual HardwareConfigurationOutcome Observe(
        AudioEndpointId endpointId,
        FW::Generation routeGeneration) noexcept = 0;
};

class IADKConfigurationPort {
public:
    virtual IOReturn RequestConfigurationWindow(
        const ConfigurationIdentity& identity) noexcept = 0;

    virtual void MarkProjectionUnavailable(
        AudioEndpointId endpointId,
        uint64_t coherentRevision) noexcept = 0;
};

class IConfigurationSnapshotPort {
public:
    virtual IOReturn Publish(
        const CommittedAudioConfiguration& snapshot) noexcept = 0;
};
```

These are behavioral contracts, not final spelling. In production:

- `AudioDuplexCoordinator` backs hardware clock/transport effects;
- family/device protocol implementations remain below that actuator;
- the ADK notification port uses the nub/action bridge to reach the published
  `ASFWAudioDevice`; and
- `AudioRuntimeRegistry` backs committed snapshot publication.

The current bounded synchronous bridge may be used inside an ADK callback, but
the wait must have a finite deadline and return a certainty-bearing result.
Its completion must run on a queue independent of the blocked caller; otherwise
the adapter must remain asynchronous rather than constructing a same-queue
deadlock.
No coordinator operation may block a realtime I/O callback.

### 8.1 Coordinator public surface

The entry points remain distinct because they carry different ADK permissions;
collapsing them into one generic `SetConfiguration` would make it too easy to
touch hardware before the host grants a perform window.

```cpp
class DeviceConfigurationCoordinator final {
public:
    ConfigurationSubmission SubmitControlIntent(
        AudioEndpointId endpointId,
        const DeviceConfiguration& requested) noexcept;

    ConfirmedProjectionPlan BeginCoreAudioRateChange(
        AudioEndpointId endpointId,
        uint32_t sampleRateHz) noexcept;

    ConfigurationSubmission AcceptHardwareObservation(
        AudioEndpointId endpointId,
        const ConfirmedHardwareConfiguration& observed) noexcept;

    ConfirmedProjectionPlan PerformPending(
        const ConfigurationIdentity& identity) noexcept;

    IOReturn FinishProjection(
        const ConfigurationIdentity& identity,
        IOReturn customProjectionStatus,
        IOReturn superclassStatus) noexcept;

    void AbortPending(
        const ConfigurationIdentity& identity,
        IOReturn reason) noexcept;

    void InvalidateRoute(
        AudioEndpointId endpointId,
        FW::Generation replacementGeneration) noexcept;

    DeviceConfigurationSnapshot CopyState(
        AudioEndpointId endpointId) const noexcept;
};
```

`ConfigurationSubmission` says whether the request was rejected, completed as
a no-op, coalesced, or staged with a token awaiting an ADK window.
`ConfirmedProjectionPlan` is a bounded, versioned value containing status,
identity, certainty, confirmed configuration, and the scalar ADK projection
shape. It contains no cross-service pointers.

`BeginCoreAudioRateChange` is valid only from the idle, non-realtime ADK rate
callback path established for V1. `PerformPending` is valid only for the token
returned by a prior control intent or hardware observation after ADK invokes
the perform callback. Invalid phase/origin combinations fail without effects.

## 9. ADK Adapter Contract

Apple's AudioDriverKit contract requires a driver-initiated structural change
to call `RequestDeviceConfigurationChange` and wait until the host invokes
`PerformDeviceConfigurationChange`; I/O is stopped before the perform callback.
The action value is opaque to the host and is returned to the driver.

ASFW uses the coordinator transaction token as the action identity and passes
`nullptr` for `in_change_info`. The ADK object accepts an action only when it
matches its pending coordinator notification; unknown actions are delegated to
the superclass.

No configuration/profile pointer crosses the service boundary. RPC payloads
are versioned, bounded POD values carried as scalar IIG arguments or `OSData`.

The adapter exposes four logical operations:

```text
NotifyPending(token)       core → ADK; ask ADK to request a host window
PerformPending(token)      ADK → core; apply/confirm or obtain observed truth
FinishProjection(token)    ADK → core; report custom projection + super result
AbortPending(token)        ADK → core; cancel before mutation or enter recovery
```

`FinishProjection` occurs only after both ASFW's ADK mutations and the required
superclass callback succeed. The coordinator publishes the new committed
snapshot only after this acknowledgement.

## 10. Transition Flows

### 10.1 Control-client origin

```text
control request
  → validate/resolve candidate
  → stage token (AwaitingADKPerform)
  → notify ADK adapter
  → RequestDeviceConfigurationChange(token)
  → HAL stops I/O
  → PerformDeviceConfigurationChange(token)
  → core ApplyAndConfirm(candidate)
  → ADK projects confirmed rate/geometry/formats
  → ADK superclass perform succeeds
  → FinishProjection(token, success)
  → publish committed revision
  → notify UI from committed snapshot
```

Hardware is not changed before `PerformDeviceConfigurationChange` grants the
window.

### 10.2 Core Audio origin

```text
HandleChangeSampleRate(rate)
  → begin coordinator request with CoreAudio origin
  → require the V1 device/transport path to be idle
  → validate/resolve candidate
  → ApplyAndConfirm(candidate) within the bounded callback path
  → project confirmed rate and stream geometry to ADK
  → finish projection and commit
  → return success
```

On rejection or known-unchanged failure, the callback returns an error and the
prior committed snapshot remains valid. On unknown hardware state, it returns
an error and the coordinator enters recovery; ASFW must not keep streaming
against either assumed rate.

This path is currently limited to the rate and preallocated stream projection
that the lab has exercised. A change requiring stream replacement, memory
descriptor replacement, latency/safety changes, or another operation documented
as perform-only must use a driver-initiated request/perform window. Whether a
Core Audio callback can safely stage such a follow-up transaction is an
empirical lab question, not an assumption in V1.

### 10.3 Hardware origin

```text
hardware observer reports configuration C at generation G
  → quiesce incompatible transport immediately
  → invalidate any pending token
  → confirm/resolve C
  → stage hardware-origin token
  → notify ADK adapter
  → RequestDeviceConfigurationChange(token)
  → PerformDeviceConfigurationChange(token)
  → no redundant hardware write
  → project C to ADK
  → finish projection and commit
```

An observation from an old route generation is discarded.

### 10.4 Abort and timeout

```text
abort before hardware operation
  → clear pending
  → retain prior committed revision

timeout with confirmed unchanged state
  → fail request
  → retain prior committed revision

timeout after possible hardware mutation
  → Recovering
  → quiesce transport
  → read back or rediscover
  → synchronize ADK to recovered truth, or mark endpoint Unavailable
```

## 11. Commit Protocol

The commit point is a successful `FinishProjection` event for the current
token and route generation. At that point the coordinator:

1. verifies the confirmed result still matches the pending token/generation;
2. creates exactly one new immutable configuration revision;
3. replaces the runtime snapshot;
4. clears pending state;
5. records a terminal success event; and
6. notifies non-realtime UI/tooling observers.

Until this point, readers continue to see the prior coherent committed
snapshot plus optional pending/recovery diagnostics. There is no optimistic
mutation of `currentSampleRate`, channel counts, or UI current values.

Cross-service publication cannot be physically atomic, so the protocol uses a
single logical commit point and an explicit incoherent/recovery state. The
coordinator never represents a partially projected state as committed.

## 12. Concurrency and Lifetime

- All state transitions for one endpoint are serialized by the core-side
  coordinator lock or dedicated control queue.
- Locks protect state only; they are not held while calling IIG, protocol,
  resolver, or notification ports.
- Every effect captures endpoint, token, and route generation, then validates
  them again when its completion returns.
- Endpoint quiesce invalidates pending work before bindings or hardware objects
  are released.
- Disconnect/reset increments or replaces route generation before stale
  completions can publish.
- The coordinator is never called from the realtime stream hot path.

## 13. Observability

Each transition emits bounded control-plane events:

```cpp
// Stable scalar values for diagnostics/wire transport. They are derived from
// variant alternatives and never drive state-machine behavior.
enum class ConfigurationPhase : uint8_t {
    kUninitialized,
    kIdle,
    kAwaitingCandidate,
    kAwaitingADKPerform,
    kAwaitingHardware,
    kAwaitingADKProjection,
    kRecovering,
    kUnavailable,
};

enum class HardwareStateCertainty : uint8_t {
    kUnchanged,
    kConfirmedRequested,
    kConfirmedOther,
    kUnknown,
};

[[nodiscard]] ConfigurationPhase PhaseOf(
    const DeviceConfigurationState&) noexcept;
[[nodiscard]] HardwareStateCertainty CertaintyOf(
    const HardwareConfigurationOutcome&) noexcept;

struct ConfigurationTraceEvent final {
    uint64_t sequence{0};
    uint64_t timestampTicks{0};
    AudioEndpointId endpointId{};
    uint64_t token{0};
    FW::Generation routeGeneration{0};
    ConfigurationOrigin origin{};
    ConfigurationPhase phase{};
    ConfigurationEventKind kind{};
    IOReturn status{kIOReturnSuccess};
    HardwareStateCertainty certainty{};
    uint64_t priorRevision{0};
    uint64_t resultingRevision{0};
};
```

The event ring stores scalar summaries and configuration hashes, not strings or
large resolved graphs. Logs use one stable prefix, `[AudioConfig]`, and include
endpoint, token, generation, origin, phase, certainty, status, old revision,
and new revision. Formatting and UI publication remain off the realtime path.

## 14. Deterministic Lab Tests

The lab first implements the state machine with fake resolver, hardware, ADK,
and snapshot ports. Required scenarios:

1. no-op request for committed state;
2. control-client success through request/perform/finish;
3. Core Audio success through `HandleChangeSampleRate`;
4. hardware-origin synchronization without a redundant hardware write;
5. unsupported candidate rejected before ADK or hardware effects;
6. duplicate candidate coalesced;
7. competing candidate rejected busy;
8. delayed hardware success;
9. known-unchanged hardware rejection;
10. hardware confirms a different configuration;
11. unknown-state timeout enters recovery;
12. ADK request rejected before perform;
13. ADK abort before hardware mutation;
14. ADK projection failure after hardware confirmation;
15. disconnect/reset in every non-idle phase;
16. stale hardware completion after generation replacement;
17. stale ADK perform/abort action;
18. write-only confirmation invalidated by reset; and
19. recovery readback followed by successful ADK resynchronization.

Global invariants checked after every generated event sequence:

```text
the variant alternative contains exactly the data required by its phase
committed revision increases only at FinishProjection success
published configuration is hardware-confirmed
stale token/generation never changes committed state
hardware mutation for driver-origin requests occurs only after ADK perform
terminal event occurs exactly once per token
Idle has no pending transition
Recovering/Unavailable never claims candidate state as committed
```

Tests exercise the Cartesian product of state and event alternatives. Every
pair is either an explicitly valid transition or a typed no-effect rejection;
adding a new variant alternative requires updating this matrix.

The existing CLI should gain fault-injection commands for delayed success,
rejection, unknown timeout, unsolicited hardware change, reset, and state/event
dump. `adk hal-rate` remains the Core Audio-origin oracle.

## 15. Relationship to Current Code

| Current owner/path | Coordinator design |
|---|---|
| Lab `currentConfiguration`, `pendingConfiguration`, and `pendingAction` fields | Replaced by one tested coordinator state. |
| `ASFWAudioDevice::HandleChangeSampleRate` validates, programs hardware, and mutates ADK directly | Becomes a thin ADK adapter: begin Core Audio intent, apply returned projection plan, finish projection. |
| `AudioDriverDeviceState::pendingExternalRateHz` | Removed; hardware-origin pending state lives in the core coordinator with token and generation. |
| `RequestExternalRateResync` special action | Generalized to a coordinator token notification usable for any structural hardware observation. |
| `AudioCoordinator::RequestClockConfig` publishes runtime rate immediately after hardware success | Becomes a hardware-port operation; runtime publication waits for ADK projection acknowledgement. |
| `AudioDuplexCoordinator` restart/clock FSM | Retained as the hardware actuator and extended only to return certainty-bearing applied state. |
| `ASFWAudioNub_IVars::currentSampleRateHz` | Either removed or retained strictly as a committed projection cache, updated only after coordinator commit. |

The migration must remove each superseded ownership path when its coordinator
replacement becomes active. There must not be a compatibility path that can
mutate rate or geometry outside the coordinator.

## 16. Implementation Sequence

### Stage A — Pure lab state machine

Add dependency-free configuration types, the reducer/state machine, fake
ports, and exhaustive tests. Do not alter the current live dext path yet.

### Stage B — Lab ADK adapter — implemented

`LabConfigurationCoordinator` now owns the lab's transaction identity,
pending state, committed revision, and one-shot scripted hardware outcome.
`VirtualAudioDevice` is the ADK adapter: control-client requests wait for
`PerformDeviceConfigurationChange`, while Core Audio's rate callback follows
the external-origin path. The old `currentConfiguration`,
`pendingConfiguration`, and `pendingAction` fields remain only refreshed
diagnostic/projection snapshots.

The existing event ring and CLI remain the observation surface. `adk outcome
<slot> <confirmed|unchanged|unknown>` scripts the next fake hardware outcome;
`unchanged` tests a known rejection and `unknown` deliberately enters the
recovery/unavailable branch. This is lab fault injection only—there is still
no FireWire control or streaming work here. The live ingress paths are the
diagnostic control client, Core Audio's rate callback, and the lab-only
`adk observe` command. The latter injects an already-confirmed observation,
then proves that the ADK adapter requests a perform window and projects it
without invoking the fake hardware-apply port.

### Stage C — Production core coordinator

Add `DeviceConfigurationCoordinator` as a member of the existing
`AudioCoordinator`. Adapt `AudioDuplexCoordinator::RequestClockConfig` as the
hardware clock port and use its applied clock, runtime capabilities, route
generation, and failure information as confirmation evidence.

### Stage D — Production ADK bridge

Replace the direct `HandleChangeSampleRate` hardware/projection sequence and
the separate `pendingExternalRateHz` path with the coordinator protocol. Reuse
the existing nub/action bridge, carrying a transaction token and bounded
result. Remove the superseded paths after the new path passes the lab and
hardware checks.

### Stage E — Structural geometry

Extend the same configuration value and resolver result to optical mode,
clock source, stream mode, channel geometry, layouts, buffer descriptors,
latency, and safety offset. No second coordinator or per-property transaction
path is introduced.

## 17. Open ADK Questions

The coordinator design intentionally leaves these questions to the virtual lab:

1. Is `HandleChangeSampleRate` always called with I/O stopped, or can clients
   invoke it while I/O remains active?
2. If a Core Audio rate request implies a perform-only structural mutation, can
   `HandleChangeSampleRate` stage `RequestDeviceConfigurationChange` without
   creating a transient HAL/device disagreement?
3. Which ADK mutations can fail after hardware confirmation, and what object
   state does the host observe when the callback returns an error?
4. What ordering of custom mutation and superclass perform gives correct host
   state when either half fails?

Until answered, V1 rejects a Core Audio-origin rate request while I/O is active
and uses preallocated maximum geometry in the lab.

## 18. API and Empirical Basis

The design relies on the following AudioDriverKit contracts, verified against
the locally installed SDK headers and the virtual lab:

- a driver-initiated I/O/structural change calls
  `RequestDeviceConfigurationChange` and waits for
  `PerformDeviceConfigurationChange` before mutation;
- the host stops I/O before the perform callback;
- the action and info values are driver-owned and returned unchanged;
- `AbortDeviceConfigurationChange` cancels a previously requested change;
- `HandleChangeSampleRate` is the device callback for a Core Audio-initiated
  nominal-rate change; and
- stream memory/format/geometry changes that affect I/O belong in the granted
  configuration context.

The lab has additionally established that an external Core Audio nominal-rate
write reaches `HandleChangeSampleRate`, and that changing only the device rate
is insufficient: stream rate/format state must be updated coherently. The lab
has not yet established the active-I/O ordering of this callback.
