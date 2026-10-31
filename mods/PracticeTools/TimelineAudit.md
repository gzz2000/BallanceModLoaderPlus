# Practice checkpoint object lifetimes

E2656–E2659 (2026-10-31): an actual-game fixture demonstrated that output
parameter topology and values were not restored: legal connected values11/22
became33, abandoned edges remained, and overwrite/restore lost edge order.
The unchanged PracticeTools failed12 of46 checks. Capture now records every
public output's ordered destinations, including opaque-valued outputs, structured
children and array-owned cells. After owner reconstruction, a preflight resolves
all endpoints through the same identity/semantic-slot resolver and validates the
complete saved graph. All captured outputs are disconnected throughout typed
value copying, including recursive structure copiers, then reconnected in saved
order without DataChanged. Attribute values participate in this same phase.
The identical actual-game fixture passes46/46; the repository output fixture
passes178 checks including malformed graphs, cycles, rejection before graph
mutation, early-exit graph rollback, structured copying and reconstructed array
slots. Normal propagation resumes after commit.
This closes the tested output-connection/value-order gap; it does not make the
whole restore transactional. Owner reconstruction occurs before this graph
preflight, and structured payload references/shared ownership remain separate
gaps. A failed graph phase can roll back its connections, not earlier world
mutations or values already copied. E2660–E2663 gameplay regression is recorded
separately in the experiment log; do not infer full-world parity from fixtures.

E2482–E2488 (2026-10-31): actual-rendering TAS replay revealed another state
owner outside CK: LegacyTASRuntime's Segment split timer. Main SR timer and all
captured physics could match while the fourth split accumulated an extra
24.2424 seconds. PracticeTools now captures that module's versioned POD state,
validates its instance/map generation before changing the world, and restores
the timer, sector, current-level records and GUI duty-cycle state without firing
events or writing config. The DLL stays loaded for the checkpoint lifetime.
No-restore neutrality, 6400 repeated fan samples and 38400 rendered samples
across checkpoints pass; final Segment config equals control byte-for-byte.
This does not close the general object-rehydration/transactional gaps below or
provide automatic snapshots of every third-party mod's private state.

E2490–E2492 (2026-10-31): real-engine fixtures found three structural
parameter failures: deleted cells lose consumers, surviving cells shifted to
another row reconnect consumers to the wrong restored value, and recreated
attribute parameters lose consumers. Capture now records each array cell's
source identity and ownership. Restore repairs shapes, maps saved identities
to owned slots, reconstructs attributes, restores cross-owner shortcuts, then
rewires inputs. The ordinary lifetime-token check is not relaxed. A separate
append-index error could loop indefinitely after column deletion; array shape
repair now uses the -1 append sentinel and checks mutation progress. Fixtures
cover 462 shape assertions and 244 binding assertions with twelve repeated
reconstructions and shared source propagation. E2496/E2497 full-game regression passes three captures/six restores in SR04
fan gameplay, all 6400 repeated physical samples, captured scores/collectibles
and six collision/host/listener/render commits. These specific tests do not
establish global checkpoint parity.
Object-valued/structured parameter payload IDs, shared attribute ownership,
output-parameter destination lists and transient shared-input rewiring cycles
remain explicit topology-audit gaps, not covered by those fixture passes.

Audit: 2026-10-30, E1916–E1927 and E1979–E1989. This is a partial world-lifetime implementation,
not a claim of complete save/restore support for every object or mod.

## Evidence and correction

SR06's life pickup creates an entity/material pair through the normal object
copy path. Rewinding parameter values, arrays and groups detached its logical
references but left both objects in the scene. E1916 observed two, then four
new objects after repeated restore. This was independent of the renderer
callback registration problem fixed earlier.

The restore now enumerates eligible new objects and previews their normal
delete dependencies before changing the saved world. `CK_DEPENDENCIES_NONE`
avoids following optional shared materials, meshes and children; it still
traverses mandatory scripts and attributes. Every member of that closure must
be in the eligible set. Otherwise restore reports failure without performing
the deletion. Deletion also rejects a deferred-behavior-execution phase or an
already pending dependency transaction.

One normal notified destruction sequence then removes the new objects. The
engine cleans scene, group, parameter, render-parent and temporary-callback
references; the enclosing restore subsequently rewinds values and runtime
state. Direct deletion, notification suppression, names and visibility are
not used to approximate ownership. We do not request ID recycling, but objects
may already carry the engine's FREEID flag: IDs are not durable generations.

## Ownership boundaries

| State | Owner / current treatment |
| --- | --- |
| Ordinary objects present at capture | Captured by identity and semantic field sets; never eligible for new-object deletion. |
| Ordinary objects created afterwards | Eligible unless a separately owned category below applies; dependency-checked notified deletion before field restoration. |
| Private parameter copies, nested structured values, saved message parameters | Snapshot storage; excluded recursively by ownership. |
| Data-array parameter cells | Reconstructed by row/column and type; excluded from ordinary deletion. |
| Simple-shadow private materials | Reconciled by the simple-shadow semantic snapshot; excluded. |
| Render callback containers, temporary queues and persistent transparent order | Renderer snapshot; validate captured owners and scene nodes before mutation; restore registrations, ordered entries and membership flags after other setters. |
| Physics objects/controllers/listeners/collision caches | Physics snapshot plus manager deletion notifications; validated separately from CK object topology. |
| Particle emitters and pools | Particle semantic snapshot; lifetime validation rejects destroyed/replaced captured emitters. |

## Verified and unresolved

E1919b's native fixture checks eight repeated creation/deletion cycles, shared
saved resources, mandatory ownership rejection, deferred-phase rejection,
scene/hierarchy/group/parameter/array cleanup and render-callback survival.
E1921 confirms two saves/four restores preserve the SR06 player traces and
collision graphs while removing the extra life objects. E1922 adds a save
after life acquisition, followed by two restores: objects belonging to the
new snapshot survive. E1924 adds SR10 repeated-restore trajectory and collision
coverage (no new ordinary objects at its four restore points). E1925 verifies
the fixture through its repository CMake target. E1927 crosses SR08 play-state
holds and a ball-type transition, preserving both branch streams and collision
checks. These checks do not cover every class destructor or
opaque pointer held by third-party code.

General rehydration of an ordinary object destroyed after capture is still
missing. Render/particle owner validation rejects several such cases; this
must eventually be replaced by complete lifecycle restoration where needed.

Previous-render frustum history is saved independently from recomputable boxes.
TT Simple Shadow queries `IsAllOutsideFrustrum` during its pre-render callback,
before the new traversal overwrites that history. E3016's pixel probe exposed
missing shadows for the first rendered frame after two restore points even
though transforms, particles and repeated physical state matched. Native entity
snapshots now include scene-graph node presence and its two frustum-history bits;
the renderer API restores only those bits, preserving independent box/sort/list
invalidation. It does not force an extra render or remove the shadow culling
test. E3024's actual CK-context regression checks all four historical bit states,
repeated/overwrite restores, nonmutation of other graph flags and rejection of
invalid or missing nodes. Game-level pixel qualification is recorded separately
in the experiment log; this is not a full-renderer checkpoint claim.
Transparent draw ordering is also historical state. The scene graph retains
its transparent list across frames, and the depth comparator can preserve ties.
E3035 observed identical per-emitter submitted geometry/colors/UVs/matrices but
a different order for two overlapping particle emitters after restore. E3037
extends the renderer callback snapshot to preserve the ordered transparent
entries and their membership flags, with node/owner validation and allocation
before commit. New owners retain independent entries pending caller lifetime
reconciliation. E3038 checks 173 cases plus the existing 79 callback checks.
E3039 showed this was only a partial fix. E3043 then found different list
membership despite identical inputs for shared entities. Scene traversal used
the wall-time-reset FPS count as an epoch, permitting stale nodes to match a
later frame. E3046 marks retained entries unvisited before each traversal;
actual traversal still marks visible nodes. E3047 covers counter reuse/reset
and unsigned wrap in 214 checks, plus 79 existing callback checks. Game-level
qualification remains in the experiment log; these units do not prove full
pixel or world-state fidelity.

Authored bounding boxes must be immediately cullable after restore. A cleared
BOXVALID bit makes IsInViewFrustrum ignore an enabled USERBOX and test a meshless
emitter's origin instead; it does not call the cache-rebuilding accessor there.
E3050 restores the local box, derives its world box from the restored transform,
and sets USERBOX|BOXVALID as SetBoundingBox(local) does. Hierarchy boxes still
invalidate normally. E3051 adds assertions before any GetBoundingBox call
(which would otherwise repair and mask the problem): old code fails, new code
passes 1222 checks. E3048 showed the FPS-marker fix alone was insufficient;
full game-level qualification of the combined changes is recorded separately.

On native macOS, the general identity map now includes a CK2 registration token
in addition to ID/address. A side registry keeps retail class layouts and ID
allocation unchanged. Tokens never rewind with the snapshot and are not used
as an ordering key. The engine-thread API returns zero for absent or pending
deletion objects. Manager teardown frees its registry entries; a library-wide
monotonic counter prevents context address reuse from recycling old tokens.
Native PracticeTools checks every ordinary saved owner before semantic
validators or mutations; missing/replaced owners explicitly fail until general
rehydration is implemented. Array cells and attribute parameters retain their
owner-based semantic reconstruction. This token extension does not yet replace
all subsystem-specific raw pointer validation, nor establish thread-safe access
to a context. Non-macOS PracticeTools retains the prior identity path.

E1979 forces ID and address reuse with placement construction; the previous
predicate accepts the replacement. E1980's matching runtime fixture passes519
checks across57 token lifetimes, including48 forced same-address constructions,
notified destruction, pending deletion/cancellation, load aliases and dynamic
reset across three context lifetimes. Existing102 timeline checks also pass.
The initial link and runtime-routing failures are preserved in the experiment
log; only the final run verified the candidate actually loaded.
The whole restore is not transactional: a later semantic restore failure can
occur after field or object mutations. All-world completion requires these
gaps, cached runtime settings and creation/deletion side effects to be covered.

The test CMake directory can be added by a native parent that supplies matching
`CK2`, `CK2_3D` and `VxMath` targets. See the experiment log for the exact tested
library hashes, commands, failed fixture premise and corrected result.

## Native lifetime-token integration regression

E1982–E1986 use the new CK2 and PracticeTools in isolated game directories.
SR06 capture-only is neutral, with24,514 selected original trajectory rows
matching (original internal time-code remains separately unqualified). SR06
six restores plus two after injected death, SR08 four across holds/ball change,
and SR10 four all complete. Every primary/selector branch record equals the
matching old native run except wall-clock freeDelta; collision/listener/callback
checks pass. No ordinary-owner rejection occurs in those cases. This does not
prove complete-world restoration, general rehydration or final UI acceptance.

E1985's first extra cross-process count-equality gate failed. E1988/E1989
read-only diagnostic controls show that different friction-hash capacities
change the number of empty slots compared (one table8 vs16 slots,2 occupied
in both;32 extra field checks). Each run still restores its own exact saved
layout. Historical E1957/E1985 slot counts were not logged. The invalid
cross-process total-count assumption was removed with the original failure
retained, without excluding any fields from within-run restoration checks.

The native mod now requires CK2 exporting CKGetObjectLifetimeToken; deploy
the matching CK2 and PracticeTools builds together. The isolated diagnostic
physics library is not part of the product change.

## Score clock modes (E2354–E2364)

The toolbox clock distinguishes ordinary play from legacy TAS simulation.
Ordinary play counts fixed logical samples, preserving 132 samples per score
interval regardless of the practice pacing limit. Recorded TAS levels consume
the recording's actual delta with the retail binary32 accumulation and TT_Timer
output convention. Using the host's 264 Hz delta for a 132 Hz recording halved
the score countdown while leaving the ball's physical trajectory unchanged.

`TT_TimeCaptureRuntimeStateV2` and `TT_TimeRestoreRuntimeStateV2` preserve the
clock mode along with ticks, accumulated time and the timer's pause flag. Mode
transitions preserve the currently visible elapsed time; resetting a timer
retains the selected mode. The original V1 API remains exported. Current native
PracticeTools requires the matching V2 toolbox runtime.

The clock fixture covers ordinary pacing changes, variable recorded deltas,
pause, mode changes and repeated restoration. SR11 at 132 Hz and SR12 at 240 Hz
match all captured original score observations. HS02 at 264 Hz had one single-sample score discrepancy; E2370–E2381 traced
it to the original counter's unconnected Time input on a >= Test, not to clock
accumulation. The original reads tiny positive uninitialized stack data;
BallanceTAS now defines the qualified original B counter boundary as strictly
past the next threshold during recorded mode, restoring >= for ordinary play.
The recognized graph's comparison parameter is part of the existing graph
snapshot. Connected custom inputs and generic Test behavior remain unchanged.
E2380 matches all31,810 observed player and score samples. This is a defined
reference-profile compatibility rule, not emulation of arbitrary stack garbage;
do not infer full-game or complete-world snapshot parity from these checks.

## Failed overwrite capture (E2693–E2695)

The committed checkpoint is immutable during a new capture. Pending values use
an independent scratch bank; only after all fallible captures succeed is the
new checkpoint published. The previous checkpoint's parameter and array value
clones become reusable scratch storage, while its physics, renderer, scheduler
and timer snapshots are released. Both storage banks are excluded recursively
from gameplay object capture and timeline deletion, and cleared on level exit.
A failed capture releases pending resources and retains the committed checkpoint.
The host pacing schedule is reset after cleanup on success and failure.

The in-game `CaptureFailureProbe` calls the real exported PracticeTools API. It
checks initial failure, failures after value copies, after physics capture and
immediately before publication, two restores after each failure, subsequent
successful overwrites and scratch reuse. It verifies unequal connected parameter
values, output order, typed array cells and strings. E2693 old ownership failed
36 of148 checks; E2695 the candidate passed all148 with the same probe binary.
These are failed-save checks, not proof that failed world restoration rolls back
all changes, that arbitrary parameter types copy successfully, or that every
third-party runtime state is covered.

Build the test mod with a native parent supplying matching BMLPlus, CK2, CK2_3D
and VxMath targets/headers and `PRACTICE_BUILD_CAPTURE_FAILURE_PROBE=ON`.
Use an isolated game, set `BALLANCE_CAPTURE_PROBE_LOG` to a writable result file
and `BALLANCE_CAPTURE_PROBE_PRACTICE` to that game's PracticeTools module path.
The probe starts after4000 in-level callbacks, exits normally, and writes its
check/failure totals; process exit alone is not a pass. The failure selector
`BALLANCE_PRACTICE_CAPTURE_FAIL_STAGE` is test-only and normally unset. The probe
is not installed by the normal mod build and is inert without both path variables.

E2702 also propagates scalar parameter, typed array and message copy failures,
registering partially captured array/message storage before filling it so that
an early return releases preceding copies. Opaque message arguments are rejected
instead of silently dropping positional arguments. The native toolbox clock is
a required capture. The expanded in-game fixture injects parameter and array
copy failures too; E2703 passes216/216. This does not prove general support for
opaque message payloads, stale array cells, arbitrary plugin-owned data, allocation
exceptions or failure rollback during RestoreState.

## Shared-input graph restoration (E2707–E2711)

Shared inputs must be restored as a complete graph after array and attribute
semantic slots have been rebound. Installing one saved edge while abandoned
edges remain can temporarily form a cycle: CK2 rejects the setter and leaves
the abandoned connection in place. That connection may reference an array cell
which has already been destroyed. Ignoring the setter's error reported a
successful restore despite incorrect inputs.

`InputTopology.h` resolves every saved input and source, validates type
compatibility, unique input identities, closure and acyclicity before changing
input connections. It then disconnects every old edge and installs the saved
graph, including enable flags and shared-null inputs. Consumers are installed
before their sources so the CK2 setter's traversal limit does not reject a
valid long chain. Every setter result is checked. This preflight protects the
input graph itself; earlier world mutations in RestoreState still require a
separate transactional rollback design.

`SharedInputProbe` uses the same opt-in native test build and path variables as
CaptureFailureProbe. Install only one probe in an isolated test game. It calls
the actual PracticeTools API with opposite-direction chains, a shared fanout,
disabled inputs, destroyed/recreated array cells, four saves and eight restores.
E2708 old candidate fails22/253 checks; E2710 the new candidate passes253/253
using the identical probe binary. The initial E2707 probe also dereferenced an
unexpected source pointer and crashed; its evidence is retained and the guarded
E2708 probe is the comparison fixture. The helper regression additionally
checks invalid graphs leave connections intact, semantic remapping, shared-null
flags, input order changes and1100-node chains (108 checks).

This closes shared-input rewiring, not typed parameter payload remapping:
CKParameter::CopyValue copies object IDs, and reconstructed array/attribute
objects may need those IDs rebound within object-valued parameters, object
arrays and nested structures. Those payloads and ordinary destroyed-owner
rehydration remain audit gates.

### Typed parameter references and semantic structure members (E2750–E2773)

A `CKParameterOut` value clone is registered in CK's object manager. Its
`CheckPostDeletion` callback clears object references and collection entries
when the abandoned timeline deletes their targets. The value clone alone is
therefore insufficient as an immutable checkpoint. `ParameterReferences.h`
captures object IDs and ordered collection IDs (including nulls and duplicates)
in ordinary C++ storage, recursively for structured parameters. It never
interprets ordinary integer values as IDs.

On restore, array and attribute roots and their recursively owned structure
members are rebound by semantic slot before input/output topology. Current
replacement attribute parameters and structure members must be excluded from
new-timeline deletion. After all value copies, with output propagation still
suspended, typed references are applied through the lifetime-aware resolver.
Object array cells and message arguments use the same mapping. A nonzero
unresolvable target fails restoration; it is not silently replaced by null.
Readonly `Myself` references are validated against their current derived owner
value instead of being assigned through a setter that necessarily rejects them.

The actual game fixture `tests/ReferenceProbe.cpp` covers four saves and eight
restores, deleted/rebuilt array cells and attributes, references to nested
members, collections and raw object array cells. It is test-only and uses the
same exported capture/restore API as keyboard controls. The guarded baseline
fixture failed 24/62 checks; the fixed combination passed 62/62 and the expanded
fixture passed 134/134. These are scoped checks, not full TAS/app qualification.

Strict validation also exposed a separate CK2 defect: `CKBeObject::SetAttribute`
wrote textual default strings with `SetValue`, overwriting structure member IDs
(e.g. Floor's `0;TRUE;0;FALSE;TRUE`). CK2 now uses `SetStringValue`; the same test
executable fails on old CK2 and passes 77 checks on the corrected CK2. Use the
corrected CK2 with this PracticeTools candidate. Actual TAS regression results
and any failures are recorded in the workspace experiment log.

Still unproven: full-world restore failure rollback, ordinary destroyed-owner
rehydration, complete opaque/private third-party state, and broad final TAS/app
acceptance. A successful typed-reference fixture does not waive these gates.

### Authored entity bounding boxes (E2907–E2911, 2026-10-31)

`VX_MOVEABLE_USERBOX` selects a caller-supplied local box. It is not merely a
cache-valid bit: `RCK3dEntity::UpdateBox` uses that geometry instead of the mesh,
and `IsInViewFrustrum` uses it to decide visibility. `ParticleEmitter`,
`MarkSystem`, and `Text3D` are actual writers. The earlier transform snapshot
and moveable-flag mask preserved neither the saved local box nor the saved
choice between authored and mesh-derived geometry. A saved emitter restored
with a later box can therefore receive different visibility/render updates.

Native snapshots now separately capture `CKEntityUserBoxState` without calling
`GetBoundingBox` or updating caches. After final matrix restoration, transfer
the authored local geometry and its enable bit; invalidate world-box, extent,
and scene-graph caches for recomputation. Do not restore cache-valid bits whose
payloads were not captured. The existing transform ABI remains unchanged, and
capture requires the new API pair before accepting a snapshot.

E2909 executes real CK objects/API calls: the old transform-only path retains
the future authored box; the new API passes 822 checks across repeated restore,
overwrite, enabled/disabled transitions, world-box recomputation and invalid
state rejection. This fixture does not by itself prove the complete game
restores correctly. Full HS04 rendered control/restore qualification is recorded
in the workspace experiment ledger (E2910/E2911); broader world coverage remains
open. Ordinary-object destruction, global rollback, and unregistered plugin
private state are not solved by this change.

E2910/E2911 completed: both exit 0, three saves/six restores, 30,000 repeated
captured-player visits, complete score and TT_Extra comparisons, final score
5765. With null rendering on every Process, 3D local-state repeat differences
fell from 13,650 (old restore) to zero; 2D and the other sampled semantic visual
categories also match before and after foreground rendering. Composite cache
hashes still differ, and fresh-process fragments differ before any save. Finite
detailed windows show only cache/movement flags after restore; full-run cache
matrices and actual GPU requalification remain open. Do not call this a complete
world or release pass.

E3052 validates the combined renderer changes on HS04: all 5625 complete
four-channel GPU readbacks (1875 rendered instants, three visits each) match
exactly across three saves and six restores; 700 repeated observed particle
draw submissions also match in original order. Physical captured streams,
score/collectibles and RNG checks pass. This scope excludes BML ImGui overlay,
other levels and general destroyed-owner rehydration; see E3054 evidence.


### Material-owned effect parameter lifetimes (E3126–E3129)

`RCKMaterial::SetEffect` destroys its parameter when switching to an effect
without one, and changes its GUID (destroying/recreating structure members)
when the parameter type changes. Those parameters are semantic owner slots,
like attribute parameters and array cells; their old identity need not survive.
The previous ordinary-identity preflight rejected a valid rewind after clearing
an effect. Applying SetEffect after copying saved values could also erase them.

Capture records the material's root/recursive parameter identities. Restore
excludes the current material-owned parameter tree from new-object deletion,
sets the saved effect and binds its reconstructed members before input/output
wiring and value restoration. External typed references use those bindings.
No late SetEffect call may overwrite the restored values. The material itself
still requires its original lifetime; this does not rehydrate arbitrary owners.

The same isolated actual-game MaterialEffectProbe fails the unchanged candidate
on its first deleted-parameter restore, and passes 88 checks on the new mod:
five captures/overwrites and fifteen restores of NONE, enum and structured
effects, changing types/values and references to structure members. All actual
loaded module paths/hashes and clean process exits were checked. Broader
reference/input/failure and GPU gameplay regression is queued separately;
this fixture is not a complete-world or release qualification.

### Animation graph and next evaluation (E3146–E3154)

The actual-game animation fixture demonstrated that step-only restore accepted
success while retaining future binding, channel membership, key payloads,
length, timing, merge and centering. The same binary failed20/27 checks on the
old combination and passed27/27 on the new renderer/PracticeTools combination.
The expanded fixture passes494 checks across all11 built-in controller kinds,
shared keyframe owner/aliases, reciprocal ordered entity lists, lazy caches,
merged evaluation, three overwritten checkpoints and nine repeated restores.

Native capture now requires CKAnimationSnapshot. Prepare all replacement
controller groups and lists before world mutation; after timeline reconciliation,
commit the graph without SetStep or pose evaluation. Exact transforms/mesh data
remain owned by their existing semantic snapshots. Animation references use
registration lifetime tokens; ordinary destroyed owners are still rejected.
See CKRenderEngine's AnimationSnapshotAudit.md for the field-by-field inventory,
cache-copy decisions and explicit character/skinning/global-rollback boundaries.
GPU replay and provider-lifecycle results are recorded separately in the workspace
experiment log; a controller fixture does not establish complete world parity.

E3155/E3156 full HS04/fan replay passes 5625/2400 whole-frame GPU readbacks,
30,000/6400 repeated samples per physical stream, score/collectibles and RNG.
E3159/E3168 add provider-lifecycle rejection and explicit successful bytewise
controller evaluation, 671 checks. The same candidate also passes the88/134/
216/253 material/reference/capture-failure/shared-input suites. Full world,
ordinary destroyed-owner reconstruction and special TAS environments remain open.
