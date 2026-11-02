# Continuous machine motion planning

The service accepts a frozen machine, TCP/direction targets, explicit physical
axis velocity/acceleration/jerk limits, and complete-motion owner callbacks.
Pinocchio/Ceres supplies bounded IK candidates and Descartes Light selects a
finite candidate route. Initial and intermediate joint-linear seed edges retain
their existing complete collision checks.

The FiveAxisCAM caller selects sparse keyposes from the original dense reference,
retaining native segment boundaries, process transitions, authored pose anchors
and rapid vertices. Initial seed tolerances (5 mm / 5 degrees, 50 mm span) only
control IK seed selection. The exact native curve/pose field remains the reference:
FK checks joint interpolation at the original 0.01 mm / 1e-4 rad tolerances and
inserts inverse-solved midpoints where needed. Continuous collision checks and
final C2 polynomial verification remain mandatory. Reports distinguish reference
samples, selected keyposes and actual IK calls including adaptive refinement.

Each candidate edge also bounds recursive refinement calls to
max(64, 2^min(MaximumVerificationDepth, 10)). Exhaustion leaves that candidate
unresolved and allows the finite graph to try other endpoints; it never proves
collision or unreachability and cannot publish partial playback. Initial
positioning may stage axes through intermediate configurations; every submove
and the final polynomial still require the owner collision certificates.

After forward and future/pole seeding, frozen-geometry checks prune invalid
station candidates before interior refinement begins. Accepted station proofs
are reused by the graph. A changed graph route checks its previous failed
connection first, while still certifying all selected states and moves.
Tool meshes and closure evidence are scene-owned immutable cache entries keyed
by the complete visual attachments and current source resource versions.

FiveAxisCAM recovery locates the failed TCP/beam in the support frame and offers
the assembly manipulator. Dragging previews locally; release stores one rigid
placement without implicit collision checks or a complete scene read. Workspace
reads do not run PrepareMachines. Its optional PlanMachine
previewConfiguration accepts placement changes only. Native capture validates
the entire assembly sweep/table bounds and freezes collision geometry at the
candidate placement, then runs the same full motion service. Preview tasks never
publish a stored plan or change the job/history; their result is marked Preview.
The finite automatic placement check tries eight planar placements/rotations,
with a three-minute overall client allowance and at most one minute per native
candidate. Failure to find one does not prove no feasible placement exists.
Only a fully accepted candidate is offered for visual preview. Applying it uses
the normal native configuration gate, saves the edit and replans the current
job; a preview cannot directly enable playback or export.

This is fixed-path machining, so a point-to-point online generator such as
Ruckig is not used to silently change the accepted cutting contour. TOPPRA's
usual velocity/acceleration formulation does not supply the required third-order
limit by itself. COPP supplies a third-order fixed-path time law; independent
checks below decide whether the final executable trajectory can be published.
See [COPP](https://github.com/TOPP-THU/copp/tree/78bc8fe907cb425be7c29b74ae3b1c44a79aaae9),
[TOPPRA](https://hungpham2511.github.io/toppra/) and
[Ruckig](https://docs.ruckig.com/).

The delivered trajectory is a C2 sequence of normalized quintics. Adjacent states
share first/second path derivatives. Blend extent decreases when the unchanged
TCP/direction tolerance, physical limits, monotone progress or complete polynomial
collision callback rejects a curve. Exact corners can retain their trace with
zero tangent/curvature. StopAfter belongs to the caller; ID/role strings do not
create motion policies.

Required stops partition rest-to-rest timing blocks, avoiding a singular global
time-law problem at a zero path tangent. Numerical IK uses one tenth of the final
reference tolerance so its residual cannot cause endless geometric refinement.
Stationary axes use the same physical time scale as moving axes instead of
creating extremely large normalized constraint bounds.

COPP 0.2.3 TOPP3-SOCP computes a third-order time law. The SDK is SHA256 pinned by
src/third_party/copp/prepare-copp.ps1. Its C ABI avoids sharing STL allocations
between the Debug host and published Release SDK. Coordinates and time are
nondimensionalized. A failed optional refinement can retain the preceding COPP
candidate; there is no velocity-only fallback. Solved/AlmostSolved optimizer
labels never grant acceptance without checking the delivered trajectory.

Adaptive conversion produces formal quintics with physical seconds. C2 joins
are checked. Conservative Bernstein enclosures cover every polynomial derivative
over the entire interval; global time dilation enforces axis velocity,
acceleration and jerk bounds without changing geometry. Every delivered
polynomial receives its own complete collision certificate. Failure, cancellation
or budget exhaustion publishes no partial samples or segments. Coincident
semantic events retain equal timestamps without artificial millisecond moves.

FiveAxisCAM freezes original process curves and pose fields for reference
checks. Playback, executed trail and ICAX-JOINT-MOTION-2 JSON use the same
QuinticNormalized coefficients. Old plans become stale. Generic G1 inverse-time
NC cannot preserve this contract and is rejected until a controller-specific
conversion and output replay are qualified.

TimingQualification=VelocityAccelerationJerk covers axis polynomial limits.
TCP/direction error and TCP feed remain sampled checks, not continuous bounds.
Calibration, servo response, torque, controller behavior, laser quality and
physical machining remain unqualified (MachineReady=false).

Axis maxAcceleration and maxJerk are optional. A blank or omitted value means
unbounded; a supplied value must be finite and positive. Linear units are mm/s²
and mm/s³; rotary UI/output units are °/s² and °/s³, converted to radians in the
neutral planner. Missing TCP feed caps are also unbounded. Authored axis velocity
and travel limits, C2 joins and collision checks remain enforced.

Unbounded constraints are omitted from COPP's finite matrix rows rather than
replaced by fabricated large caps. Mixed bounded/unbounded axes use sparse raw
derivative rows. Reports list each omitted constraint as `Unbounded` and use
`TimingQualification=ConfiguredAxisLimits;BlankIsUnbounded` when axis derivative
limits are omitted. Test fixture limits are not machine ratings.

Build with src/tools/build/build_machine_motion_planning_services.ps1. SDK and
build outputs stay in independent C: directories. Native tests load the actual
kinematics/Descartes/COPP modules and exercise LC-3015/IR-1600 resources, offset
tools, pole continuity, independent physical derivatives, C2 joins and failure
behavior. Their empty collision environments do not qualify actual machine
meshes; product and standalone CEF workflows supply those checks.
