#pragma once
#include "SheetNesting.h"
#include <limits>
#include <optional>

namespace iCAX::SheetNesting
{
    // Source doc/Plane/43: a container belongs to one original specification.
    // Ordinary single templates and same-specification pair templates have
    // separate weights, including all their angle variants. No weight for
    // mixed-specification or more-than-two-member units is defined here.
    enum class PlaneArrayContainerKind { Single, SameSpecificationPair };
    struct PlaneArrayPitchTemplate
    {
        double Length=0,Height=0;
        // Preserve the source container order. The source weight reads only
        // back(), not the minimum-area pitch or the currently selected pitch.
        std::vector<Point> OrderedPitches;
    };
    inline constexpr double PlaneArrayAkwardTolerance=1e-4;
    struct PlaneArrayAkwardResult
    {
        bool Valid=false;
        bool HasEligibleTemplate=false;
        // An empty container, or one without any pitch, retains DBL_MAX just
        // like source SetAkward(). It is not an initialized usable score.
        double K=std::numeric_limits<double>::max();
        std::size_t SelectedTemplate=std::numeric_limits<std::size_t>::max();
        std::string Diagnostic;
    };
    PlaneArrayAkwardResult ComputePlaneArrayAkward(PlaneArrayContainerKind kind,
        const std::vector<PlaneArrayPitchTemplate>& templates);

    struct PlaneArrayEvaluation
    {
        bool Valid=false;
        double E=0;
        // Absent when no explicit container weight was supplied. E and P are
        // source heuristic scores, never material utilization percentages.
        std::optional<double> P;
        std::string Diagnostic;
    };
    // Count is the actual original-piece count, not material area. X/Y are the
    // complete occupied envelope after any partial pair row/column extension.
    // W/H describe the current evaluated free rectangle, not the whole stock.
    // Inputs must be finite, positive-area domains with 0<X<=W and 0<Y<=H.
    PlaneArrayEvaluation EvaluatePlaneArray(std::size_t count,double X,double Y,
        double W,double H,std::optional<double> K={});

    struct PlanePlacementSpaceEvaluation
    {
        bool Valid=false;
        // All values describe only the caller's explicit current domain after
        // the fixed placements and this candidate, never a future placement.
        double LargestFreeArea=0;
        // Net area squared / outer AABB area of the largest-area component.
        // Holes reduce net area; this is a shape proxy, not a rectangle proof.
        double LargestFreeRegularArea=0;
        double FragmentFreeArea=0;
        double FreeArea=0;
        // Every remaining boundary counts, including hole boundaries.
        double FreePerimeter=0;
        // Nonnegative half of P(fixed)+P(candidate)-P(union), using clipped,
        // buffered material contours including holes. Candidate members are
        // unioned first, so their own internal contact is not fixed contact.
        double Attachment=0;
        std::size_t FreeComponents=0;
        // Disks use an inscribed regular 64-gon for this heuristic only.
        bool CircleProxyUsed=false;
        std::string Diagnostic;
    };

    // Fixed-layout scoring cache for a small set of already certified
    // candidates. Do not call it for every raw NFP pose. Material is buffered
    // by PartGap/2 + 4*Tolerance, then clipped to evaluationDomain; there is no
    // default full-stock domain. The Problem must remain alive and unchanged
    // for the evaluator's lifetime. Only referenced placement geometries are
    // read; remaining quantities/specifications are never evaluated.
    // This proxy neither certifies legality nor changes any hard material gate.
    class PlanePlacementSpaceEvaluator
    {
    public:
        PlanePlacementSpaceEvaluator(const Problem&,const Layout&,Rect evaluationDomain);
        bool Ready() const noexcept;
        PlanePlacementSpaceEvaluation Evaluate(const std::vector<Placement>& additions) const;
    private:
        struct Impl;
        std::shared_ptr<const Impl> Cached;
    };
}
