namespace straight_boundary_contract_audit
{
#include <ExtrusionRecognition/MachiningFaceCutCoverageStraight.inc>

namespace event_contract
{
using namespace R;
struct CurveUse {std::uint64_t EdgeId=0;Handle(Geom2d_Curve) Curve;double First=0.,Last=0.;};
struct Boundary {CurveUse Use;bool Source=false;};
struct Ref {std::uint64_t Edge=0;double Parameter=0.;};
struct Event {double SourceParameter=0.;std::vector<Ref> Refs;};
#include <ExtrusionRecognition/MachiningFaceCutCoverageNormalRay.inc>
}

Handle(Geom_BSplineCurve) NativeSegment(const gp_Pnt& first,const gp_Pnt& last)
{
    TColgp_Array1OfPnt poles(1,2);poles(1)=first;poles(2)=last;
    TColStd_Array1OfReal knots(1,2);knots(1)=0.;knots(2)=1.;
    TColStd_Array1OfInteger multiplicities(1,2);multiplicities(1)=multiplicities(2)=2;
    return new Geom_BSplineCurve(poles,knots,multiplicities,1,false);
}

CircularFixture OriginalDegreeOneStrip(bool shifted=false,double overhang=0.)
{
    const double first=shifted?-11.749999999999991:0.,last=shifted?11.75000000000001:10.;
    const double lower=shifted?-.6000000000000121:0.,upper=shifted?.5999999999999872:.4;
    const std::vector<gp_Pnt> corners{{0,first,lower},{0,last,lower},{0,last+overhang,upper},{0,first-overhang,upper}};
    BRepBuilderAPI_MakeWire wire;
    for(std::size_t i=0;i<corners.size();++i)
        wire.Add(BRepBuilderAPI_MakeEdge(NativeSegment(corners[i],corners[(i+1)%corners.size()]),0.,1.).Edge());
    CircularFixture fixture;fixture.Face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(1,0,0)),wire.Wire(),true).Face();
    fixture.Section=machining_path_audit::PolygonSection({{first-1,lower},{last+1,lower},{last+1,upper},{first-1,upper}});
    if(!BRepCheck_Analyzer(fixture.Face).IsValid())throw std::runtime_error("Original degree-one spline strip is invalid");
    return fixture;
}

template<class Check> void Run(const fs::path& directory,const Check& check)
{
    {
        using namespace event_contract;
        const CurveUse source{1,new Geom2d_Line(gp_Pnt2d(0,0),gp_Dir2d(1,0)),0.,1.};
        std::vector<Boundary> boundaries{{source,true},{{2,new Geom2d_Line(gp_Pnt2d(0,1),gp_Dir2d(1,0)),0.,1.},false},
            {{3,new Geom2d_Line(gp_Pnt2d(0,0),gp_Dir2d(0,1)),0.,1.},false}};
        R::SMachiningFaceCutCoverageOptions options;std::vector<Hit> hits;std::string reason;double targetParameter=-7.;
        const bool allEndpoint=RayHits(source,0.,1.,boundaries,options,hits,reason,true);
        hits.clear();const bool interior=RayHits(source,.5,1.,boundaries,options,hits,reason);
        check(!allEndpoint&&interior&&hits.size()==1
            &&AtEvent(source,Event{0.,{}},boundaries[1],1.,boundaries,options,targetParameter,reason)&&targetParameter==0.,
            "a real vertical side may coincide with the endpoint normal while the certified open-component target retains its unique original endpoint; interior first-hit still tests all boundaries");
        TColgp_Array1OfPnt2d bentPoles(1,3);bentPoles(1)={1,-1};bentPoles(2)={-1,0};bentPoles(3)={1,1};
        const CurveUse lowerSource{4,new Geom2d_Line(gp_Pnt2d(0,-1),gp_Dir2d(1,0)),0.,1.};
        std::vector<Boundary> curved{{lowerSource,true},{{5,new Geom2d_BezierCurve(bentPoles),0.,1.},false}};
        targetParameter=-7.;
        check(!AtEvent(lowerSource,Event{.25,{}},curved[1],1.,curved,options,targetParameter,reason)
            &&targetParameter==-7.&&reason=="normal-strip-certified-boundary-returned-multiple-native-hits",
            "selected-target closure still rejects two actual positive native hits on a curved target rather than selecting a nearer or clamped endpoint");
        targetParameter=-7.;
        check(!AtEvent(source,Event{0.,{}},boundaries[2],1.,boundaries,options,targetParameter,reason)
            &&targetParameter==-7.&&reason=="normal-strip-normal-coincides-with-a-physical-boundary",
            "a selected target that itself coincides with the event normal remains rejected");
        curved[1].Use.Last=.1;targetParameter=-7.;
        check(!AtEvent(lowerSource,Event{.25,{}},curved[1],1.,curved,options,targetParameter,reason)&&targetParameter==-7.,
            "selected-target closure cannot import either curved intersection from outside the target's unchanged native range");
    }
    const auto line=NativeSegment({0,0,0},{0,10,0});
    const auto edge=BRepBuilderAPI_MakeEdge(line,0.,1.).Edge();
    check(BRepAdaptor_Curve(edge).GetType()==GeomAbs_BSplineCurve
        &&native_straight_boundary::Certified(BRepAdaptor_Curve(edge)),
        "an unchanged clamped degree-one two-pole native BSpline has an exact straight-support certificate despite its non-Line runtime type");
    TColgp_Array1OfPnt2d uv(1,2);uv(1)={0,0};uv(2)={10,0};
    TColStd_Array1OfReal weights(1,2);weights(1)=.5;weights(2)=2.;
    const Handle(Geom2d_BezierCurve) rational=new Geom2d_BezierCurve(uv,weights);
    Geom2dAdaptor_Curve rationalAdaptor(rational,0.,1.);
    check(native_straight_boundary::Certified(rationalAdaptor,0.,1.)
        &&rational->Weight(1)==.5&&rational->Weight(2)==2.
        &&rationalAdaptor.FirstParameter()==0.&&rationalAdaptor.LastParameter()==1.,
        "positive unequal degree-one weights prove the original straight support without normalizing weights or replacing its rational native parameterization");
    check(!native_straight_boundary::Certified(rationalAdaptor,-1.,1.)
        &&!native_straight_boundary::Certified(rationalAdaptor,0.,0.),
        "a straight-support certificate rejects a changed out-of-domain or zero native range");
    const auto collapsed=NativeSegment({0,0,0},{0,0,0});
    check(!native_straight_boundary::PositiveTwoPoleDegreeOne(collapsed,0.,1.),
        "two identical original poles cannot certify a nondegenerate straight boundary");
    TColgp_Array1OfPnt2d highPoles(1,3);highPoles(1)={0,0};highPoles(2)={5,0};highPoles(3)={10,0};
    const Handle(Geom2d_BezierCurve) high=new Geom2d_BezierCurve(highPoles);
    check(!native_straight_boundary::Certified(Geom2dAdaptor_Curve(high,0.,1.),0.,1.),
        "higher-degree collinear controls remain outside this narrowly certified degree-one support family");
    highPoles(2)={5,1};const Handle(Geom2d_BezierCurve) bent=new Geom2d_BezierCurve(highPoles);
    check(!native_straight_boundary::Certified(Geom2dAdaptor_Curve(bent,0.,1.),0.,1.),
        "a curved side cannot be accepted from its endpoint chord or apparent straight display");
    const auto fixture=OriginalDegreeOneStrip();
    const auto analysis=machining_path_audit::Analyze(fixture.Face,fixture.Section);
    const auto before=G::Persistence::Serialize(analysis.Classification.SplitGeometry);
    const auto built=O::ReconstructOpenCascadeShape(analysis.Classification.SplitGeometry);
    const auto coverage=R::MachiningFaceCutCoverageWork{}.Run(analysis.Classification,analysis.Edges,analysis.Paths);
    Save(directory/"original-degree-one-spline-strip-result.json",Summary(coverage));
    std::size_t sides=0;bool originalSideCurves=true;
    for(const auto& path:analysis.Paths.Edges)if(path.bSide){++sides;for(const auto& use:path.SourceUses)
        originalSideCurves=originalSideCurves&&use.FirstParameter==0.&&use.LastParameter==1.
            &&BRepAdaptor_Curve(built.Edges.at(use.EdgeId)).GetType()==GeomAbs_BSplineCurve;}
    check(machining_path_audit::Selected(analysis.Paths)==2&&sides==2&&originalSideCurves,
        "the real strip fixture retains two skin paths and two whole original degree-one BSpline sides with unchanged [0,1] native ranges");
    check(coverage.bSuccess&&coverage.bComplete&&coverage.Sources.size()==2
        &&std::all_of(coverage.Sources.begin(),coverage.Sources.end(),[](const auto& source){return source.bComplete&&source.Method=="plane-certified-normal-strip-first-boundary";})
        &&std::all_of(coverage.Coverage.begin(),coverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
        &&NativeIntervals(coverage,built)&&SourceEvidenceConsistent(coverage,&analysis.Paths)
        &&analysis.SourceUnchanged&&before==G::Persistence::Serialize(analysis.Classification.SplitGeometry),
        "normal-family mapping covers the original BSpline-sided planar strip through native proofs while preserving complete upstream CAD bytes");
    for(const auto& setting:std::vector<std::pair<bool,double>>{{true,0.},{false,2.e-8}}) {
        const auto closureFixture=OriginalDegreeOneStrip(setting.first,setting.second);
        const auto closure=machining_path_audit::Analyze(closureFixture.Face,closureFixture.Section);
        const auto closureBuilt=O::ReconstructOpenCascadeShape(closure.Classification.SplitGeometry);
        const auto closureBefore=G::Persistence::Serialize(closure.Classification.SplitGeometry);
        const auto closureCoverage=R::MachiningFaceCutCoverageWork{}.Run(closure.Classification,closure.Edges,closure.Paths);
        const double lower=setting.first?-.6000000000000121:0.;
        std::string lowerKey;
        for(const auto& path:closure.Paths.Edges)if(path.bSelectedPath)for(const auto& use:path.SourceUses){BRepAdaptor_Curve curve(closureBuilt.Edges.at(use.EdgeId));
            if(curve.Value(use.FirstParameter).Z()==lower&&curve.Value(use.LastParameter).Z()==lower)lowerKey=path.Key;}
        const auto source=std::find_if(closureCoverage.Sources.begin(),closureCoverage.Sources.end(),[&](const auto& s){return s.SourcePathKey==lowerKey;});
        Save(directory/(setting.first?"shifted-degree-one-event-closure.json":"oblique-degree-one-event-closure.json"),Summary(closureCoverage));
        bool interiorTarget=!setting.second;
        for(const auto& arrival:closureCoverage.Arrivals)if(arrival.SourcePathKey==lowerKey)
            interiorTarget=interiorTarget||(arrival.TargetFirstParameter>0.&&arrival.TargetLastParameter<1.);
        check(!lowerKey.empty()&&source!=closureCoverage.Sources.end()&&source->bComplete&&interiorTarget
            &&NativeIntervals(closureCoverage,closureBuilt)&&SourceEvidenceConsistent(closureCoverage,&closure.Paths)
            &&closure.SourceUnchanged&&closureBefore==G::Persistence::Serialize(closure.Classification.SplitGeometry),
            setting.first?"nonzero original native coordinates cannot let an adjacent endpoint side overlap discard the entire proved straight-strip component":
                "original endpoints oblique within the BREP budget map to the selected target's genuine interior native parameters without clamping or changing CAD");
    }
}
}
