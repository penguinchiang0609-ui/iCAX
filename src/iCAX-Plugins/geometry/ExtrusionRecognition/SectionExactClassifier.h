#pragma once
#include <GeometryData/GeometryData.h>
#include <BRepClass_FaceExplorer.hxx>
#include <BRepClass_FClassifier.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <ShapeAnalysis_Curve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <map>
#include <vector>
namespace iCAX::ExtrusionRecognition::section_detail {
using iCAX::GeometryData::Point2;
enum class EPointRelation { Outside, Boundary, Inside };
    // A candidate is fixed while thousands of points are classified. Retain
    // its explorer, finite boundary curves and conservative exact bounds.
    // The candidate uses the fixed XY plane created by CandidateFace.
    // Cache only equal coordinates; no snapping or tolerance changes.
    struct SExactCandidateClassifier
    {
        struct SBoundary { Handle(Geom_Curve) Curve; double First, Last; Bnd_Box Bounds; };
        BRepClass_FaceExplorer Explorer;
        BRepTopAdaptor_FClass2d FiniteClassifier;
        double Tolerance;
        std::vector<SBoundary> Boundary;
        Bnd_Box FiniteBounds;
        std::map<std::pair<double,double>,EPointRelation> Points;
        std::map<std::pair<double,double>,bool> BoundaryPoints;

        SExactCandidateClassifier(const TopoDS_Face& face,double tolerance)
            : Explorer(face),FiniteClassifier(face,Precision::Confusion()),Tolerance(tolerance)
        {
            Explorer.SetUseBndBox(true);
            for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
                const auto edge=TopoDS::Edge(it.Current());
                double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
                if(curve.IsNull())continue;
                Bnd_Box bounds;BRepBndLib::AddOptimal(edge,bounds,false,false);
                bounds.Enlarge(tolerance);
                Boundary.push_back({curve,first,last,bounds});
                FiniteBounds.Add(bounds);
            }
        }

        EPointRelation Classify(const Point2& point)
        {
            const auto key=std::make_pair(point.X,point.Y);
            if(const auto found=Points.find(key);found!=Points.end())return found->second;
            const auto relation=ClassifyUncached(point);
            Points.emplace(key,relation);return relation;
        }

        bool OnFiniteBoundary(const Point2& point)
        {
            const auto key=std::make_pair(point.X,point.Y);
            if(const auto found=BoundaryPoints.find(key);found!=BoundaryPoints.end())return found->second;
            const gp_Pnt p(point.X,point.Y,0);bool on=false;
            for(const auto& boundary:Boundary) {
                if(boundary.Bounds.IsOut(p))continue;
                if(p.Distance(boundary.Curve->Value(boundary.First))<=Tolerance
                    ||p.Distance(boundary.Curve->Value(boundary.Last))<=Tolerance){on=true;break;}
                GeomAPI_ProjectPointOnCurve projection(p,boundary.Curve,boundary.First,boundary.Last);
                if(projection.NbPoints()&&projection.LowerDistance()<=Tolerance){on=true;break;}
                gp_Pnt projected;double parameter;
                ShapeAnalysis_Curve analysis;
                const auto trimmed=Handle(Geom_Curve)(new Geom_TrimmedCurve(boundary.Curve,boundary.First,boundary.Last));
                if(analysis.Project(trimmed,p,Tolerance,projected,parameter)<=Tolerance){on=true;break;}
            }
            BoundaryPoints.emplace(key,on);return on;
        }

        EPointRelation ClassifyUncached(const Point2& point)
        {
        const gp_Pnt nativePoint(point.X,point.Y,0);
        if(!FiniteBounds.IsVoid()&&FiniteBounds.IsOut(nativePoint))return EPointRelation::Outside;
        if(OnFiniteBoundary(point))return EPointRelation::Boundary;
        const auto finiteState=FiniteClassifier.Perform(gp_Pnt2d(point.X,point.Y),false);
        if(finiteState==TopAbs_OUT)return EPointRelation::Outside;
        if(finiteState==TopAbs_IN)return EPointRelation::Inside;
        BRepClass_FClassifier classifier(Explorer,gp_Pnt2d(point.X,point.Y),Tolerance);
        // At a junction between circular arcs with different parameter
        // frames, the ray classifier can return IN for a trim endpoint.
        // Verify distance to the finite original boundary before using that
        // answer as evidence of an interior edge. Keep the same tolerance.
        if(classifier.State()==TopAbs_IN)for(const auto& boundary:Boundary) {
            if(boundary.Bounds.IsOut(nativePoint))continue;
            const auto& curve=boundary.Curve;
            if(nativePoint.Distance(curve->Value(boundary.First))<=Tolerance
                ||nativePoint.Distance(curve->Value(boundary.Last))<=Tolerance)return EPointRelation::Boundary;
            GeomAPI_ProjectPointOnCurve projection(nativePoint,curve,boundary.First,boundary.Last);
            if(projection.NbPoints()&&projection.LowerDistance()<=Tolerance)return EPointRelation::Boundary;
        }
        if(classifier.State()==TopAbs_IN)return EPointRelation::Inside;
        if(classifier.State()==TopAbs_ON)return EPointRelation::Boundary;
        return EPointRelation::Outside;
        }
    };

}
