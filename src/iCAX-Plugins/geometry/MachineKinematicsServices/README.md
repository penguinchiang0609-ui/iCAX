# Neutral machine kinematics

`IMachineKinematicsService` consumes the owner's already normalized tree. It
does not read URDF/SDF, entities, tools, repositories, CAM intents or files.
The owner converts source lengths to mm once, and passes joint angles in radians.
Every result position vector retains the input node slots; fixed slots are zero.

Pinocchio 2.7.0 programmatically builds that tree and supplies FK and analytical
joint/frame Jacobians. A tool and a moving support may occupy different branches.
For each query the service uses a fresh mutable `pinocchio::Data` associated with
the immutable prepared model; concurrent queries do not share scratch state.

Ceres 2.1.0 optimizes normalized active coordinates with explicit lower/upper
bounds. Fixed axes are eliminated. The six residual components are TCP position
and the signed unit beam vector; the direction part has rank two. The service
does not add a roll condition, cut angle cone, collision penalty, tool policy or
rapid orientation policy. Each returned candidate passes a fresh physical FK,
position/direction angular tolerances and physical limits. Conditioning, margin,
seed distance and actual finite search counts accompany candidates.

A tool chain containing three independent prismatic joints followed by two
rotary joints has a topology-derived analytic inverse. The reference branch
must be fixed, with no additional moving joints outside this chain. The beam
equation is solved by Rodrigues rotations about the actual normalized axes;
translations come from the full-rank prismatic Jacobian. This supports skew
Cartesian axes, separated rotary origins and arbitrary fixed tool offsets.
At a beam-direction pole, caller outer-angle coordinates are retained. Physical
turn domains and fixed constraints remain binding, and Pinocchio independently
accepts every candidate. Other topologies use the finite Ceres search. Caller
seeds already meeting the requested FK tolerance are accepted before numerical
optimization, so duplicate target stations cannot lose a valid preceding pose.

`FullPose` is a distinct task. FK/Jacobian expose its relative Tool orientation
and spatial angular derivative, while the current inverse provider explicitly
returns `Unsupported` for FullPose; it never changes a 5D request into a 6D task.

Numerical seed searches do not enumerate all inverse branches. `NoCandidate`
does not prove unreachability. Cancellation and deadlines return explicit states
and may retain candidates found earlier. Deterministic Halton seeds supplement
caller seeds and the reference configuration within `MaxSeeds`; no hidden turn
wrapping extends an owner's physical continuous-axis domain.

## Windows build

Run `src/tools/build/build_machine_kinematics_services.ps1` from PowerShell.
It pins archive SHA256 values and Pinocchio's jrl-cmakemodules gitlink. Dependency
sources, intermediates, install tree, logs and plugin output stay under
`C:\icax-machine-kinematics` (overridable to another directory on C:).
It uses VS2022 x64 Debug /MDd, Eigen 3.4.0, Boost 1.87.0, minimal Ceres with
miniglog/static dense solvers and Pinocchio with Python/URDF/collision disabled.
No Python or ROS runtime is deployed. Existing iCAX Debug framework import
libraries are selected by `-FrameworkBinaryDirectory`.
The pinned jrl uninstall template receives one documented quoting fix for
`CMAKE_COMMAND` under Windows Program Files; no solver source is modified.

The regular vcxproj is also provided for solution integration. Its dependency
prefix property is `ICAXMachineKinematicsDependencies`; it intentionally exposes
only the validated Debug/x64 configuration. The build script never changes the
main solution, product manifests or running application.

Native tests use the existing MachineDescriptionLoader for the actual LC-3015
and IR-1600 resources, then pass only its normalized tree to this service. They
verify every world frame against independent tree multiplication, LC's documented
physical offsets, analytical relative Jacobians against central differences,
moving translation/rotation support branches, bounded inverse results, IR free
roll, signed antipodal direction, fixed axes, finite failure, cancellation,
deadline and concurrent query contexts. These tests establish the neutral
service, not machine collision, path continuity, postprocessing or real-machine
qualification; those remain responsibilities of their owning services/products.
