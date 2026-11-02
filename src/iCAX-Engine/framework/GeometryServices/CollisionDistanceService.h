#pragma once
#include <Services/IService.h>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <limits>
#include <vector>
#include <GeometryData/GeometryData.h>

namespace iCAX::GeometryServices
{
    using CollisionVector = std::array<double,3>;
    using CollisionTransform = std::array<double,16>;
    // Column axes, half extents; coordinates are supplied by the geometry owner.
    struct CollisionBox {
        CollisionVector Center{}, HalfExtents{};
        std::array<CollisionVector,3> Axes{{{1,0,0},{0,1,0},{0,0,1}}};
    };
    struct CollisionFrustum { double Z=0, Height=1, LowerRadius=1, UpperRadius=1; };
    // Convex hull of the supplied points, expanded by a nonnegative radius.
    struct CollisionHull { std::vector<CollisionVector> Points;double Padding=0; };
    struct CollisionMesh {
        std::vector<CollisionVector> Vertices;
        std::vector<std::array<unsigned,3>> Triangles;
        double Padding=0;
        // Optional owner-declared closed material volumes, each listing triangle
        // indices for all its outer/inner shells. Volumes are united; shells in
        // one volume use parity, preserving cavities. No vertices are moved.
        // Without this declaration, exactly watertight topology is detected.
        std::vector<std::vector<unsigned>> ClosedVolumes;
    };
    class CollisionDistanceModel {
    public:
        virtual ~CollisionDistanceModel()=default;
    };
    using CollisionDistanceModelRef=std::shared_ptr<const CollisionDistanceModel>;
    // Conservative CAD solid outer bounds. Intersecting bounds require further
    // checking; they must never be presented as an exact CAD collision verdict.
    class IBRepCollisionBoundsService : public Services::IService {
    public:
        virtual std::vector<CollisionBox> Boxes(const GeometryData::BRepModel&,
            const std::function<bool()>& cancelled={}) const=0;
    };
    struct CollisionDistanceResult {
        // A conservative lower bound for the supplied volumes. Zero means
        // unresolved/overlapping bounds, not a CAD collision verdict.
        double LowerBound=0;
        std::size_t NarrowPhaseQueries=0;
    };
    struct CollisionDistanceWitness {
        std::size_t FixedPartIndex=std::numeric_limits<std::size_t>::max(),MovingPartIndex=std::numeric_limits<std::size_t>::max();
        // Both witnesses are in the fixed model/query frame. They belong to the
        // supplied geometry surfaces; SurfaceDistance is their Euclidean gap.
        CollisionVector PointOnFixed{},PointOnMoving{};
        // Occupied-material distance includes declared padding/solid volumes.
        // For containment it is zero even when the two surfaces are separated.
        double Separation=0,SurfaceDistance=0;
        bool Containment=false;
    };
    struct CollisionSeparationResult {
        // Distance bracket for the supplied primitive/triangle colliders.
        // Unlike LowerBound, zero is not substituted on early termination.
        double LowerBound=0, UpperBound=0;
        bool Overlapping=false;
        std::size_t NarrowPhaseQueries=0;
        std::optional<CollisionDistanceWitness> Witness;
    };
    class ICollisionDistanceService : public Services::IService {
    public:
        virtual CollisionDistanceModelRef PrepareBoxes(const std::vector<CollisionBox>&,
            const std::function<bool()>& cancelled={}) const=0;
        virtual CollisionDistanceModelRef PrepareVolumes(const std::vector<CollisionBox>&,
            const std::vector<CollisionHull>&,const std::function<bool()>& cancelled={}) const=0;
        virtual CollisionDistanceModelRef PrepareMesh(const CollisionMesh&,
            const std::function<bool()>& cancelled={}) const=0;
        // Shares immutable geometry; no repeated BVH construction.
        virtual CollisionDistanceModelRef Combine(const std::vector<CollisionDistanceModelRef>&,
            const std::function<bool()>& cancelled={}) const=0;
        // Circular frusta are conservatively enclosed, including end caps.
        virtual CollisionDistanceModelRef PrepareFrusta(const std::vector<CollisionFrustum>&) const=0;
        // Prepared models are immutable and can be queried concurrently.
        // No Scene, IDs, units, machining policy or resource persistence here.
        virtual CollisionDistanceResult LowerBound(const CollisionDistanceModelRef& fixed,
            const CollisionDistanceModelRef& moving,const CollisionTransform& movingTransform,
            double stopBelow,const std::function<bool()>& cancelled={}) const=0;
        virtual CollisionSeparationResult Separation(const CollisionDistanceModelRef& fixed,
            const CollisionDistanceModelRef& moving,const CollisionTransform& movingTransform,
            const std::function<bool()>& cancelled={}) const=0;
    };
}
