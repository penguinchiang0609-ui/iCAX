// Independent native interval contracts. Expected ranges below come from
// closed-form fixture polynomials, not a second copy of the bound algorithm.
namespace spline_projection_contract_audit
{
struct CurveUse {Handle(Geom2d_Curve) Curve;double First=0.,Last=0.;std::uint64_t EdgeId=0;};
struct Branch {std::uint64_t EdgeId=0;Handle(Geom2d_Curve) Curve;double First=0.,Last=0.,SFirst=0.,SLast=0.;bool bConstant=false;};
#include <ExtrusionRecognition/MachiningFaceCutCoverageProjection.inc>
#include <ExtrusionRecognition/MachiningFaceCutCoverageSplineBounds.inc>
#include <ExtrusionRecognition/MachiningFaceCutCoverageRootCount.inc>
#include <ExtrusionRecognition/MachiningFaceCutCoverageTargetWindow.inc>
#include <ExtrusionRecognition/MachiningFaceCutCoverageInverseRefinement.inc>
#include <ExtrusionRecognition/MachiningFaceCutCoverageSourceWindow.inc>

Handle(Geom2d_BSplineCurve) Cubic(const std::vector<double>& values)
{
    TColgp_Array1OfPnt2d poles(1,4);for(int i=1;i<=4;++i)poles(i)={values.at(i-1),double(i-1)/3.};
    TColStd_Array1OfReal knots(1,2);knots(1)=0.;knots(2)=1.;
    TColStd_Array1OfInteger mult(1,2);mult(1)=mult(2)=4;
    return new Geom2d_BSplineCurve(poles,knots,mult,3);
}

std::string Snapshot(const Handle(Geom2d_Curve)& curve)
{
    std::ostringstream out;out<<std::hexfloat;Geom2dAdaptor_Curve native(curve);
    out<<int(native.GetType())<<' '<<curve->FirstParameter()<<' '<<curve->LastParameter()<<' '<<curve->IsPeriodic()<<' ';
    const auto poles=[&](const auto& c){out<<c->Degree()<<' '<<c->NbPoles()<<' ';for(int i=1;i<=c->NbPoles();++i)out<<c->Pole(i).X()<<' '<<c->Pole(i).Y()<<' '<<c->Weight(i)<<' ';};
    if(native.GetType()==GeomAbs_BSplineCurve){const auto c=native.BSpline();poles(c);for(int i=1;i<=c->NbKnots();++i)out<<c->Knot(i)<<' '<<c->Multiplicity(i)<<' ';}
    else if(native.GetType()==GeomAbs_BezierCurve)poles(native.Bezier());return out.str();
}

template<class Check> void Run(const Check& check,const fs::path& directory)
{
    VariantArray records;std::vector<std::pair<Handle(Geom2d_Curve),std::string>> originals;
    const auto remember=[&](const Handle(Geom2d_Curve)& curve){originals.emplace_back(curve,Snapshot(curve));};
    const auto bound=[&](const std::string& name,const CurveUse& use,double a,double b,double exactLo,double exactHi,double maximumWidth=std::numeric_limits<double>::infinity()) {
        double lower=-12345.,upper=98765.;std::string reason;const bool complete=BoundNativeSplineProjection(use,a,b,lower,upper,reason);
        const bool pass=complete&&std::isfinite(lower)&&std::isfinite(upper)&&lower<=exactLo&&upper>=exactHi&&upper>=lower&&upper-lower<maximumWidth;
        records.emplace_back(ObjectMap{{"name",name},{"passed",pass},{"complete",complete},{"first",a},{"last",b},{"lower",lower},{"upper",upper},{"expectedLower",exactLo},{"expectedUpper",exactHi},{"reason",reason}});return pass;
    };
    TColgp_Array1OfPnt2d quadraticPoles(1,3);quadraticPoles(1)={0,0};quadraticPoles(2)={2,.5};quadraticPoles(3)={0,1};
    const Handle(Geom2d_BezierCurve) quadratic=new Geom2d_BezierCurve(quadraticPoles);remember(quadratic);
    check(bound("bezier-full-interior-maximum",{quadratic,0.,1.},0.,1.,0.,1.)
        &&bound("bezier-small-window-interior-maximum",{quadratic,0.,1.},.375,.625,.9375,1.,1.5),
        "native Bezier continuous bounds include the analytic interior maximum beyond both endpoint projections, also on a strict subwindow");

    const double center=77.21399273929736;const auto stationary=Cubic({center+.75,center-.25,center-.25,center+.75});remember(stationary);
    const double adjacent=std::nextafter(.5,1.);const double left=.5-1.e-9,right=.5+1.e-9;
    check(bound("bspline-one-ulp-stationary-window",{stationary,0.,1.},.5,adjacent,center,center,1.e-8)
        &&bound("bspline-cross-stationary-window",{stationary,0.,1.},left,right,center,center+3.e-18,1.e-6),
        "original cubic BSpline stationary windows keep finite local continuous bands without widening to the complete curve or losing a one-ULP native interval");

    TColStd_Array1OfReal weights(1,3);weights(1)=1.;weights(2)=2.;weights(3)=1.;
    const Handle(Geom2d_BezierCurve) rational=new Geom2d_BezierCurve(quadraticPoles,weights);remember(rational);
    TColgp_Array1OfPnt2d constantPoles(1,3);for(int i=1;i<=3;++i)constantPoles(i)={7.5,double(i-1)/2.};
    const Handle(Geom2d_BezierCurve) constant=new Geom2d_BezierCurve(constantPoles,weights);remember(constant);
    TColStd_Array1OfReal rationalKnots(1,2);rationalKnots(1)=0.;rationalKnots(2)=1.;TColStd_Array1OfInteger rationalMult(1,2);rationalMult(1)=rationalMult(2)=3;
    const Handle(Geom2d_BSplineCurve) rationalSpline=new Geom2d_BSplineCurve(quadraticPoles,weights,rationalKnots,rationalMult,2);remember(rationalSpline);
    check(bound("rational-bezier-positive-weight-full-domain",{rational,0.,1.},0.,1.,0.,4./3.)
        &&bound("rational-bspline-positive-weight-full-domain",{rationalSpline,0.,1.},0.,1.,0.,4./3.)
        &&bound("rational-bezier-constant-projection",{constant,0.,1.},0.,1.,7.5,7.5),
        "nonuniform positive rational weights remain provably positive on the full native domain and enclose analytic rational extrema and constant projection");

    TColgp_Array1OfPnt2d c0Poles(1,7);const double c0x[]{0.,1.,2.,3.,2.,1.,0.};for(int i=1;i<=7;++i)c0Poles(i)={c0x[i-1],double(i-1)};
    TColStd_Array1OfReal c0Knots(1,3);c0Knots(1)=0.;c0Knots(2)=.5;c0Knots(3)=1.;TColStd_Array1OfInteger c0Mult(1,3);c0Mult(1)=c0Mult(3)=4;c0Mult(2)=3;
    const Handle(Geom2d_BSplineCurve) c0=new Geom2d_BSplineCurve(c0Poles,c0Knots,c0Mult,3);remember(c0);
    check(bound("bspline-c0-knot-crossing",{c0,0.,1.},.25,.75,1.5,3.)
        &&bound("bspline-c0-knot-left-end",{c0,0.,1.},.25,.5,1.5,3.)
        &&bound("bspline-c0-knot-right-start",{c0,0.,1.},.5,.75,1.5,3.),
        "a native triple interior knot retains both exact C0 polynomial sides without changing multiplicities or skipping a positive knot span");

    TColgp_Array1OfPnt2d openPoles(1,4);for(int i=1;i<=4;++i)openPoles(i)={2.*(i-1),double(i-1)};
    TColStd_Array1OfReal openKnots(1,7);TColStd_Array1OfInteger openMult(1,7);for(int i=1;i<=7;++i){openKnots(i)=double(i-1);openMult(i)=1;}
    const Handle(Geom2d_BSplineCurve) unclamped=new Geom2d_BSplineCurve(openPoles,openKnots,openMult,2);remember(unclamped);
    check(unclamped->FirstParameter()==2.&&unclamped->LastParameter()==4.
        &&bound("bspline-unclamped-native-support",{unclamped,2.,4.},2.,4.,1.,5.),
        "unclamped native BSpline bounds use the true flat-knot support K_p through K_n+1 rather than the outer stored knots");

    TColgp_Array1OfPnt2d periodicPoles(1,4);periodicPoles(1)={0,0};periodicPoles(2)={1,0};periodicPoles(3)={1,1};periodicPoles(4)={0,1};
    TColStd_Array1OfReal periodicKnots(1,5);TColStd_Array1OfInteger periodicMult(1,5);for(int i=1;i<=5;++i){periodicKnots(i)=double(i-1);periodicMult(i)=1;}
    const Handle(Geom2d_BSplineCurve) periodic=new Geom2d_BSplineCurve(periodicPoles,periodicKnots,periodicMult,2,true);remember(periodic);
    const auto reject=[&](const CurveUse& use,double a,double b){double lower=123.,upper=456.;std::string reason;return !BoundNativeSplineProjection(use,a,b,lower,upper,reason)&&lower==123.&&upper==456.;};
    const double belowZero=std::nextafter(0.,-1.),aboveOne=std::nextafter(1.,2.);
    check(reject({quadratic,0.,1.},belowZero,1.)&&reject({quadratic,0.,1.},0.,aboveOne)
        &&reject({quadratic,0.,1.},.5,.5)&&reject({quadratic,0.,1.},0.,std::numeric_limits<double>::infinity())
        &&reject({quadratic,belowZero,1.},0.,1.)&&reject({unclamped,0.,6.},2.,4.)
        &&reject({periodic,periodic->FirstParameter(),periodic->LastParameter()},periodic->FirstParameter(),periodic->LastParameter())
        &&reject({new Geom2d_Line(gp_Pnt2d(0,0),gp_Dir2d(1,0)),0.,1.},0.,1.)&&reject({{},0.,1.},0.,1.),
        "invalid, out-of-domain by one ULP, zero-length, nonfinite, unsupported and periodic spline requests reject without changing output sentinels or clamping parameters");

    const auto pointBound=[&](const std::string& name,const CurveUse& use,double parameter,double exact) {
        double lower=-12345.,upper=98765.;std::string reason;const bool complete=BoundNativeSplineProjectionPoint(use,parameter,lower,upper,reason);
        const bool pass=complete&&std::isfinite(lower)&&std::isfinite(upper)&&lower<=exact&&upper>=exact;
        records.emplace_back(ObjectMap{{"name",name},{"passed",pass},{"pointParameter",parameter},{"lower",lower},{"upper",upper},{"expected",exact},{"reason",reason}});return pass;
    };
    const auto rejectPoint=[&](const CurveUse& use,double parameter){double lower=123.,upper=456.;std::string reason;return !BoundNativeSplineProjectionPoint(use,parameter,lower,upper,reason)&&lower==123.&&upper==456.;};
    check(pointBound("bezier-original-point-internal-extremum",{quadratic,0.,1.},.5,1.)
        &&pointBound("rational-original-point",{rational,0.,1.},.5,4./3.)
        &&pointBound("bspline-original-start-point",{c0,0.,1.},0.,0.)
        &&pointBound("bspline-original-c0-shared-knot-point",{c0,0.,1.},.5,3.)
        &&pointBound("bspline-original-end-point",{c0,0.,1.},1.,0.)
        &&rejectPoint({quadratic,0.,1.},belowZero)&&rejectPoint({quadratic,0.,1.},aboveOne)
        &&rejectPoint({periodic,periodic->FirstParameter(),periodic->LastParameter()},periodic->FirstParameter())
        &&rejectPoint({quadratic,0.,1.},std::numeric_limits<double>::quiet_NaN())
        &&reject({quadratic,0.,1.},.5,.5),
        "point projection encloses unchanged native endpoints and shared C0 knots while the positive-window API still rejects zero length and point requests outside the original domain preserve sentinels");

    const auto ascending=Cubic({0.,1.,2.,3.}),descending=Cubic({3.,2.,1.,0.}),tangent=Cubic({0.,0.,1.,1.});
    const auto oneUlpReversal=Cubic({0.,1.,std::nextafter(1.,0.),2.});
    remember(ascending);remember(descending);remember(tangent);remember(oneUlpReversal);
    TColgp_Array1OfPnt2d rationalOrderedPoles(1,3);for(int i=1;i<=3;++i)rationalOrderedPoles(i)={double(i-1),double(i-1)};
    const Handle(Geom2d_BSplineCurve) rationalOrdered=new Geom2d_BSplineCurve(rationalOrderedPoles,weights,rationalKnots,rationalMult,2);remember(rationalOrdered);
    const auto certificate=[&](const CurveUse& use,int expected){int direction=73;std::string reason;const bool complete=CertifyStrictOriginalPolynomialSplineProjection(use,direction,reason);return expected?complete&&direction==expected:!complete&&direction==73;};
    check(certificate({ascending,0.,1.},1)&&certificate({descending,0.,1.},-1)&&certificate({tangent,0.,1.},1)
        &&certificate({unclamped,2.,4.},1)&&certificate({stationary,0.,1.},0)
        &&certificate({oneUlpReversal,0.,1.},0)&&certificate({rationalSpline,0.,1.},0)&&certificate({rationalOrdered,0.,1.},0)
        &&certificate({periodic,periodic->FirstParameter(),periodic->LastParameter()},0),
        "strict original polynomial projection certificates accept increasing, decreasing and endpoint-tangent equal-weight curves but reject true folds, one-ULP pole reversals, unequal rational weights and periodic support without altering direction sentinels");

    TColgp_Array1OfPnt2d plateauPoles(1,4);plateauPoles(1)={0,0};plateauPoles(2)={1,1};plateauPoles(3)={1,2};plateauPoles(4)={2,3};
    TColStd_Array1OfReal plateauKnots(1,4);TColStd_Array1OfInteger plateauMult(1,4);for(int i=1;i<=4;++i){plateauKnots(i)=double(i-1);plateauMult(i)=i==1||i==4?2:1;}
    const Handle(Geom2d_BSplineCurve) plateau=new Geom2d_BSplineCurve(plateauPoles,plateauKnots,plateauMult,1);remember(plateau);
    check(certificate({plateau,0.,3.},0)&&certificate({plateau,1.,2.},0)&&certificate({plateau,0.,1.},1)
        &&bound("bspline-real-platform-enclosure",{plateau,0.,3.},1.,2.,1.,1.),
        "a positive original knot-span platform cannot obtain a strict inverse certificate even when other spans advance; an advancing use outside that platform remains certifiable");

    TColgp_Array1OfPnt2d quarterPoles(1,3),quarterDescendingPoles(1,3);
    quarterPoles(1)={0,1};quarterPoles(2)={1,1};quarterPoles(3)={1,0};
    quarterDescendingPoles(1)={1,0};quarterDescendingPoles(2)={1,1};quarterDescendingPoles(3)={0,1};
    TColStd_Array1OfReal quarterWeights(1,3);quarterWeights(1)=quarterWeights(3)=1.;quarterWeights(2)=std::sqrt(.5);
    const Handle(Geom2d_BezierCurve) quarterBezier=new Geom2d_BezierCurve(quarterPoles,quarterWeights);
    const Handle(Geom2d_BezierCurve) quarterDescendingBezier=new Geom2d_BezierCurve(quarterDescendingPoles,quarterWeights);
    const Handle(Geom2d_BSplineCurve) quarterSpline=new Geom2d_BSplineCurve(quarterPoles,quarterWeights,rationalKnots,rationalMult,2);
    const Handle(Geom2d_BSplineCurve) quarterDescendingSpline=new Geom2d_BSplineCurve(quarterDescendingPoles,quarterWeights,rationalKnots,rationalMult,2);
    remember(quarterBezier);remember(quarterDescendingBezier);remember(quarterSpline);remember(quarterDescendingSpline);
    const auto rationalCertificate=[&](const std::string& name,const CurveUse& use,int expected){
        int direction=73;std::string reason;const bool complete=CertifyStrictOriginalPositiveRationalSplineProjection(use,direction,reason);
        const bool passed=expected?complete&&direction==expected:!complete&&direction==73;
        records.emplace_back(ObjectMap{{"name",name},{"passed",passed},{"certified",complete},{"direction",direction},{"expectedDirection",expected},{"reason",reason}});return passed;
    };
    check(rationalCertificate("positive-rational-quarter-bezier-increasing",{quarterBezier,0.,1.},1)
        &&rationalCertificate("positive-rational-quarter-bezier-decreasing",{quarterDescendingBezier,0.,1.},-1)
        &&rationalCertificate("positive-rational-quarter-bspline-increasing",{quarterSpline,0.,1.},1)
        &&rationalCertificate("positive-rational-quarter-bspline-decreasing",{quarterDescendingSpline,0.,1.},-1)
        &&certificate({quarterBezier,0.,1.},0)&&certificate({quarterDescendingBezier,0.,1.},0)
        &&certificate({quarterSpline,0.,1.},0)&&certificate({quarterDescendingSpline,0.,1.},0)
        &&pointBound("positive-rational-quarter-native-point",{quarterBezier,0.,1.},.5,std::sqrt(.5)),
        "ordered original positive-rational quarter-circle Bezier and BSpline projections certify both directions and legal endpoint tangency while the unchanged polynomial API rejects the same unequal weights");

    TColStd_Array1OfReal plateauWeights(1,4);plateauWeights(1)=1.;plateauWeights(2)=2.;plateauWeights(3)=.5;plateauWeights(4)=3.;
    const Handle(Geom2d_BSplineCurve) rationalPlateau=new Geom2d_BSplineCurve(plateauPoles,plateauWeights,plateauKnots,plateauMult,1);remember(rationalPlateau);
    check(rationalCertificate("positive-rational-real-platform-full",{rationalPlateau,0.,3.},0)
        &&rationalCertificate("positive-rational-real-platform-use",{rationalPlateau,1.,2.},0)
        &&rationalCertificate("positive-rational-advancing-use-outside-platform",{rationalPlateau,0.,1.},1)
        &&certificate({rationalPlateau,0.,1.},0)
        &&rationalCertificate("positive-rational-constant-projection",{constant,0.,1.},0),
        "positive rational weights never turn a real active knot-span platform into a strict inverse, and only an advancing original use outside that platform certifies");

    TColgp_Array1OfPnt2d rationalReversalPoles(1,4);for(int i=1;i<=4;++i)rationalReversalPoles(i)=oneUlpReversal->Pole(i);
    TColStd_Array1OfReal reversalWeights(1,4);reversalWeights(1)=1.;reversalWeights(2)=.7;reversalWeights(3)=1.3;reversalWeights(4)=.9;
    const Handle(Geom2d_BezierCurve) rationalOneUlpReversal=new Geom2d_BezierCurve(rationalReversalPoles,reversalWeights);remember(rationalOneUlpReversal);
    const Handle(Geom2d_BSplineCurve) rationalPeriodic=new Geom2d_BSplineCurve(periodicPoles,plateauWeights,periodicKnots,periodicMult,2,true);remember(rationalPeriodic);
    check(rationalCertificate("positive-rational-fold-rejected",{rationalSpline,0.,1.},0)
        &&rationalCertificate("positive-rational-one-ulp-pole-reversal-rejected",{rationalOneUlpReversal,0.,1.},0)
        &&rationalCertificate("positive-rational-periodic-rejected",{rationalPeriodic,rationalPeriodic->FirstParameter(),rationalPeriodic->LastParameter()},0)
        &&rationalCertificate("positive-rational-outside-original-domain-rejected",{quarterSpline,belowZero,1.},0)
        &&rationalCertificate("positive-rational-zero-length-use-rejected",{quarterBezier,.5,.5},0),
        "the positive-rational certificate rejects folds, every original pole reversal including one ULP, periodic support and invalid original domains without changing direction sentinels");

    const auto uniqueRoot=[&](const std::string& name,const CurveUse& use,double s,bool expected){
        std::string reason;const bool certified=CertifyUniqueOriginalNativeSplineProjectionRoot(use,s,reason);
        records.emplace_back(ObjectMap{{"name",name},{"passed",certified==expected},{"requestS",s},{"certified",certified},{"reason",reason}});return certified==expected;
    };
    TColgp_Array1OfPnt2d foldedPoles(1,3);foldedPoles(1)={-1,0};foldedPoles(2)={-2,.5};foldedPoles(3)={1,1};
    const Handle(Geom2d_BezierCurve) singleCrossFold=new Geom2d_BezierCurve(foldedPoles);remember(singleCrossFold);
    int foldedDirection=73;std::string foldedReason;
    check(uniqueRoot("original-bezier-fold-one-request-root",{singleCrossFold,0.,1.},0.,true)
        &&!CertifyStrictOriginalPositiveRationalSplineProjection({singleCrossFold,0.,1.},foldedDirection,foldedReason),
        "a genuine original Bezier fold can certify exactly one requested root without obtaining a false whole-curve monotonicity certificate");
    TColStd_Array1OfReal foldedWeights(1,3);foldedWeights(1)=1.;foldedWeights(2)=.25;foldedWeights(3)=2.;
    const Handle(Geom2d_BezierCurve) singleCrossRationalFold=new Geom2d_BezierCurve(foldedPoles,foldedWeights);remember(singleCrossRationalFold);
    check(uniqueRoot("original-positive-rational-fold-one-request-root",{singleCrossRationalFold,0.,1.},0.,true)
        &&pointBound("positive-rational-fold-analytic-root",{singleCrossRationalFold,0.,1.},.5,0.),
        "positive original rational weights retain the request-specific unique crossing of a folded curve and its independently known analytic root");
    TColgp_Array1OfPnt2d doubleRootPoles(1,3),contactPoles(1,3);doubleRootPoles(1)={3./16.,0};doubleRootPoles(2)={-5./16.,.5};doubleRootPoles(3)={3./16.,1};
    contactPoles(1)={.25,0};contactPoles(2)={-.25,.5};contactPoles(3)={.25,1};
    const Handle(Geom2d_BezierCurve) doubleRoots=new Geom2d_BezierCurve(doubleRootPoles),contact=new Geom2d_BezierCurve(contactPoles);remember(doubleRoots);remember(contact);
    check(uniqueRoot("original-bezier-two-real-roots-rejected",{doubleRoots,0.,1.},0.,false)
        &&uniqueRoot("original-bezier-interior-contact-rejected",{contact,0.,1.},0.,false),
        "two analytic roots at one-quarter and three-quarters and a tangent zero at one-half cannot become a unique original projection crossing");
    TColgp_Array1OfPnt2d knotContactPoles(1,4),zeroPlatformPoles(1,4);const double knotContactX[]{1.,0.,1.,-1.},zeroPlatformX[]{1.,0.,0.,-1.};
    for(int i=1;i<=4;++i){knotContactPoles(i)={knotContactX[i-1],double(i-1)};zeroPlatformPoles(i)={zeroPlatformX[i-1],double(i-1)};}
    TColStd_Array1OfReal thirdsKnots(1,4);for(int i=1;i<=4;++i)thirdsKnots(i)=double(i-1)/3.;
    const Handle(Geom2d_BSplineCurve) knotContact=new Geom2d_BSplineCurve(knotContactPoles,thirdsKnots,plateauMult,1),zeroPlatform=new Geom2d_BSplineCurve(zeroPlatformPoles,thirdsKnots,plateauMult,1);remember(knotContact);remember(zeroPlatform);
    check(uniqueRoot("one-sign-transition-internal-knot-contact-rejected",{knotContact,0.,1.},0.,false),
        "one nonzero coefficient sign transition cannot hide an additional zero contact at an original internal knot");
    check(uniqueRoot("one-sign-transition-zero-platform-rejected",{zeroPlatform,0.,1.},0.,false),
        "an original positive-width zero platform is rejected even when the remaining coefficient signs change exactly once");
    TColgp_Array1OfPnt2d nonzeroPlatformPoles(1,3);nonzeroPlatformPoles(1)={-1,0};nonzeroPlatformPoles(2)={-1,.5};nonzeroPlatformPoles(3)={1,1};
    TColStd_Array1OfReal halfKnots(1,3);halfKnots(1)=0.;halfKnots(2)=.5;halfKnots(3)=1.;TColStd_Array1OfInteger halfMult(1,3);halfMult(1)=halfMult(3)=2;halfMult(2)=1;
    const Handle(Geom2d_BSplineCurve) nonzeroPlatform=new Geom2d_BSplineCurve(nonzeroPlatformPoles,halfKnots,halfMult,1);remember(nonzeroPlatform);
    check(uniqueRoot("non-target-platform-with-one-crossing",{nonzeroPlatform,0.,1.},0.,true),
        "a real platform at a value different from the requested S does not erase an independently unique crossing in another original knot span");
    const auto exactThreeSigns=Cubic({0.,std::nextafter(1.,2.),std::nextafter(1.,0.),2.});remember(exactThreeSigns);
    check(uniqueRoot("one-ulp-coefficient-signs-not-merged",{exactThreeSigns,0.,1.},1.,false),
        "exact original coefficient comparisons retain all three sign variations across one-ULP pole differences");
    TColgp_Array1OfPnt2d endpointContactPoles(1,3);endpointContactPoles(1)={0,0};endpointContactPoles(2)={-1,.5};endpointContactPoles(3)={1,1};
    const Handle(Geom2d_BezierCurve) endpointContact=new Geom2d_BezierCurve(endpointContactPoles);remember(endpointContact);
    check(uniqueRoot("original-endpoint-not-strictly-separated-rejected",{endpointContact,0.,1.},0.,false)
        &&uniqueRoot("original-periodic-root-count-rejected",{periodic,periodic->FirstParameter(),periodic->LastParameter()},.5,false)
        &&uniqueRoot("original-root-count-outside-native-domain-rejected",{singleCrossFold,belowZero,1.},0.,false),
        "request-specific root counts reject non-strict endpoint signs, periodic support and any request outside the original native domain");

    TColgp_Array1OfPnt2d windowPoles(1,2);windowPoles(1)={1,0};windowPoles(2)={2,1};TColStd_Array1OfReal windowKnots(1,2);windowKnots(1)=1.;windowKnots(2)=2.;TColStd_Array1OfInteger windowMult(1,2);windowMult(1)=windowMult(2)=2;
    const Handle(Geom2d_BSplineCurve) windowCurve=new Geom2d_BSplineCurve(windowPoles,windowKnots,windowMult,1);remember(windowCurve);
    const auto sourceBranch=[](const Handle(Geom2d_Curve)& curve,double first,double last){return Branch{1,curve,first,last,curve->Value(first).X(),curve->Value(last).X(),false};};
    const auto windowSource=sourceBranch(windowCurve,1.,2.);Branch windowInner;double windowA=-1.,windowB=-2.;std::string windowReason;
    check(BuildSourceEndpointNativeWindowRetry(windowSource,1.,2.,{true,true},windowInner,windowA,windowB,windowReason)
        &&windowInner.First==std::nextafter(1.,2.)&&windowInner.Last==std::nextafter(2.,1.)&&windowA==windowInner.SFirst&&windowB==windowInner.SLast&&windowA>1.&&windowB<2.&&windowInner.Curve==windowCurve,
        "two failed scalar ends exclude only their original adjacent native windows and keep one unchanged curve");
    const auto noWindowMutation=[&](const Branch& candidate,double a,double b,SourceEndpointInverseFailure failed){
        Branch sentinel{999,windowCurve,-8.,-7.,-6.,-5.,true};const auto old=sentinel;double lo=123.,hi=456.;std::string reason="sentinel";
        return !BuildSourceEndpointNativeWindowRetry(candidate,a,b,failed,sentinel,lo,hi,reason)&&sentinel.EdgeId==old.EdgeId&&sentinel.Curve==old.Curve&&sentinel.First==old.First&&sentinel.Last==old.Last&&sentinel.SFirst==old.SFirst&&sentinel.SLast==old.SLast&&sentinel.bConstant==old.bConstant&&lo==123.&&hi==456.&&reason=="sentinel";
    };
    check(noWindowMutation(windowSource,1.,2.,{false,false})&&noWindowMutation(windowSource,1.25,1.75,{true,false}),
        "no actual endpoint failure or no whole-window S match leaves every output sentinel unchanged");
    const auto oneUlpWindowSource=sourceBranch(windowCurve,1.,std::nextafter(1.,2.));
    check(noWindowMutation(oneUlpWindowSource,oneUlpWindowSource.SFirst,oneUlpWindowSource.SLast,{true,false}),
        "one-ULP original source range cannot invent a positive inner branch");
    const auto twoUlpWindowSource=sourceBranch(windowCurve,1.,std::nextafter(std::nextafter(1.,2.),2.));
    check(noWindowMutation(twoUlpWindowSource,twoUlpWindowSource.SFirst,twoUlpWindowSource.SLast,{true,true}),
        "two excluded native windows cannot collapse the inner range and still report success");
    auto invalidWindowSource=windowSource;invalidWindowSource.SFirst=std::nextafter(invalidWindowSource.SFirst,0.);
    check(noWindowMutation(invalidWindowSource,1.,2.,{true,false})&&noWindowMutation(windowSource,1.,1.,{true,false}),
        "normalized stored endpoint data and nonpositive scalar cells are rejected without changing outputs");
    check(noWindowMutation(sourceBranch(new Geom2d_Line(gp_Pnt2d(0,0),gp_Dir2d(1,0)),1.,2.),1.,2.,{true,true}),
        "unsupported primitive projection cannot use a spline window without a continuous bound proof");
    fs::path case90Root=fs::current_path();const fs::path case90Relative="TestData/Tube/PathAnalysis/Checkpoints/090-075.STEP/split.icaxbrep";
    while(!fs::exists(case90Root/case90Relative)){const auto parent=case90Root.parent_path();if(parent==case90Root||parent.empty())throw std::runtime_error("Original case90 checkpoint unavailable for native source-window audit");case90Root=parent;}
    std::ifstream case90Input(case90Root/case90Relative,std::ios::binary);const std::vector<std::uint8_t> case90Bytes((std::istreambuf_iterator<char>(case90Input)),{});
    const auto case90Model=G::Persistence::Deserialize(case90Bytes);const auto case90Built=O::ReconstructOpenCascadeShape(case90Model);if(!case90Built.bOK)throw std::runtime_error("Original case90 source-window fixture reconstruction failed");
    const auto case90Face=case90Built.Faces.at(245);double case90First=0.,case90Last=0.;const auto case90Curve=BRep_Tool::CurveOnSurface(case90Built.Edges.at(837),case90Face,case90First,case90Last);remember(case90Curve);
    auto case90Source=sourceBranch(case90Curve,case90First,case90Last);case90Source.EdgeId=837;double case90TargetFirst=0.,case90TargetLast=0.;const auto case90Target=BRep_Tool::CurveOnSurface(case90Built.Edges.at(838),case90Face,case90TargetFirst,case90TargetLast);
    const double case90FailedS=case90Target->Value(case90TargetLast).X();Branch case90Inner;double case90A=-1.,case90B=-2.;std::string case90Reason;
    const bool case90Retry=BuildSourceEndpointNativeWindowRetry(case90Source,case90FailedS,case90Source.SFirst,{true,false},case90Inner,case90A,case90B,case90Reason);
    check(case90Retry&&case90Inner.Last==std::nextafter(case90Source.Last,case90Source.First)&&case90Inner.First==case90Source.First&&case90Inner.Curve==case90Curve
        &&case90A==case90Curve->Value(case90Inner.Last).X()&&case90A>case90FailedS&&case90B==case90Source.SFirst&&case90Inner.Last<case90Source.Last,
        "case90 keeps the true source endpoint one-ULP window Pending and retries the long cell from the previous original native parameter rather than fabricating an endpoint inverse");
    check(case90Bytes==G::Persistence::Serialize(case90Model)&&case90Curve->Value(case90First).X()==case90Source.SFirst&&case90Curve->Value(case90Last).X()==case90Source.SLast,
        "case90 original neutral bytes and original pcurve endpoint values remain unchanged");
    records.emplace_back(ObjectMap{{"name",std::string("case90-native-source-window")},{"passed",case90Retry},{"originalNative",VariantArray{case90First,case90Last}},{"innerNative",VariantArray{case90Inner.First,case90Inner.Last}},{"failedS",case90FailedS},{"clippedS",VariantArray{case90A,case90B}},{"reason",case90Reason}});

    const auto refined=[&](const std::string& name,const CurveUse& use,double s,const std::vector<double>& seeds,bool expected){
        double parameter=123.;std::string reason;const bool complete=RefineUniqueOriginalNativeSplineProjectionRoot(use,s,seeds,parameter,reason);
        double lo=0.,hi=0.;std::string pointReason;const bool verified=complete&&parameter>use.First&&parameter<use.Last
            &&BoundNativeSplineProjectionPoint(use,parameter,lo,hi,pointReason)&&s>=lo&&s<=hi;
        const bool passed=expected?verified:!complete&&parameter==123.;
        records.emplace_back(ObjectMap{{"name",name},{"passed",passed},{"expected",expected},{"success",complete},{"requestS",s},{"parameter",parameter},{"pointLower",lo},{"pointUpper",hi},{"reason",reason}});return passed;
    };
    const auto noneContains=[&](const CurveUse& use,double s,const std::vector<double>& seeds){for(const auto seed:seeds){double lo=0.,hi=0.;std::string reason;if(!BoundNativeSplineProjectionPoint(use,seed,lo,hi,reason)||(s>=lo&&s<=hi))return false;}return true;};
    const auto inverseCube=Cubic({0.,0.,0.,1.}),inverseDescending=Cubic({1.,0.,0.,0.});remember(inverseCube);remember(inverseDescending);
    TColgp_Array1OfPnt2d inverseBezierPoles(1,4);for(int i=1;i<=4;++i)inverseBezierPoles(i)=inverseCube->Pole(i);
    const Handle(Geom2d_BezierCurve) inverseBezier=new Geom2d_BezierCurve(inverseBezierPoles);remember(inverseBezier);
    const std::vector<double> coarseSeeds{.25,.75};
    check(noneContains({inverseBezier,0.,1.},.125,coarseSeeds)&&noneContains({inverseCube,0.,1.},.125,coarseSeeds)
        &&refined("native-bezier-coarse-seeds-refined",{inverseBezier,0.,1.},.125,coarseSeeds,true)
        &&refined("native-bspline-coarse-seeds-refined",{inverseCube,0.,1.},.125,coarseSeeds,true),
        "certified unique native Bezier and BSpline inverses recover when every in-range kernel seed point interval initially excludes the requested S");
    gp_Pnt2d derivativePoint;gp_Vec2d zeroEndpointDerivative;inverseCube->D1(0.,derivativePoint,zeroEndpointDerivative);
    check(zeroEndpointDerivative.X()==0.
        &&refined("native-inverse-zero-start-derivative",{inverseCube,0.,1.},.125,{0.},true)
        &&refined("native-inverse-descending",{inverseDescending,0.,1.},.125,{.25,.75},true)
        &&refined("native-inverse-zero-end-derivative",{inverseDescending,0.,1.},.125,{1.},true),
        "safeguarded original-parameter inverse refinement supports decreasing projection and zero endpoint derivatives without inventing a clamped endpoint root");
    check(refined("native-inverse-request-specific-polynomial-fold",{singleCrossFold,0.,1.},0.,{.1,.2},true)
        &&refined("native-inverse-request-specific-rational-fold",{singleCrossRationalFold,0.,1.},0.,{.1,.9},true),
        "a genuine original fold refines only for an independently proven unique requested root and retains its original control data");
    const auto inverseThreeRoots=Cubic({-3./32.,13./96.,-13./96.,3./32.});remember(inverseThreeRoots);
    check(refined("native-inverse-two-real-roots-rejected",{doubleRoots,0.,1.},0.,{.25,.75},false)
        &&refined("native-inverse-three-real-roots-with-opposite-end-signs-rejected",{inverseThreeRoots,0.,1.},0.,{.25,.5,.75},false)
        &&refined("native-inverse-interior-contact-rejected",{contact,0.,1.},0.,{.5},false),
        "two roots, three roots despite opposite endpoint signs, and an interior contact cannot be recovered by choosing an arbitrary kernel seed");
    check(refined("native-inverse-empty-seeds-rejected",{inverseCube,0.,1.},.125,{},false)
        &&refined("native-inverse-all-outside-or-nonfinite-seeds-rejected",{inverseCube,0.,1.},.125,{-1.,2.,std::numeric_limits<double>::quiet_NaN()},false)
        &&refined("case90-original-outside-kernel-candidate-not-recovered",{case90Curve,case90First,case90Last},case90FailedS,{std::nextafter(case90Last,std::numeric_limits<double>::infinity())},false),
        "empty or entirely invalid native seed sets never launch a replacement root search and case90 cannot recover its out-of-domain original kernel candidate");
    check(refined("native-inverse-nonstrict-start-point-sign-rejected",{inverseCube,0.,1.},0.,{.25},false)
        &&refined("native-inverse-nonstrict-end-point-sign-rejected",{inverseCube,0.,1.},1.,{.75},false)
        &&refined("native-inverse-periodic-support-rejected",{periodic,periodic->FirstParameter(),periodic->LastParameter()},.5,{1.},false),
        "inverse refinement requires strict original endpoint interval signs and rejects periodic support instead of snapping an endpoint inverse");
    TColgp_Array1OfPnt2d adjacentRootPoles(1,2);adjacentRootPoles(1)={0,0};adjacentRootPoles(2)={4503599627370496.,1};
    const Handle(Geom2d_BSplineCurve) adjacentRoot=new Geom2d_BSplineCurve(adjacentRootPoles,windowKnots,windowMult,1);remember(adjacentRoot);
    const double adjacentLast=std::nextafter(1.,2.);
    check(refined("native-inverse-no-representable-interior-parameter",{adjacentRoot,1.,adjacentLast},.5,{1.,adjacentLast},false),
        "an actual unique root between two adjacent original native doubles cannot be represented by returning either endpoint and leaves the parameter sentinel unchanged");

    NativeProjectionInverseFailureWindow adjacentFailure;double adjacentParameter=123.;std::string adjacentReason;
    const bool adjacentSolved=RefineUniqueOriginalNativeSplineProjectionRoot({adjacentRoot,1.,2.,37},.5,{1.,2.},adjacentParameter,adjacentReason,&adjacentFailure);
    auto adjacentTarget=sourceBranch(adjacentRoot,1.,2.);adjacentTarget.EdgeId=37;
    TargetEndpointInverseFailure adjacentTargetFailure;adjacentTargetFailure.First=true;adjacentTargetFailure.FirstWindow=adjacentFailure;
    Branch adjacentInner;double adjacentA=-1.,adjacentB=-2.;std::string adjacentWindowReason;
    const bool adjacentRetry=BuildTargetEndpointNativeWindowRetry(adjacentTarget,.5,3.,adjacentTargetFailure,adjacentInner,adjacentA,adjacentB,adjacentWindowReason);
    check(!adjacentSolved&&adjacentParameter==123.&&adjacentFailure.bValid
        &&adjacentFailure.First==1.&&adjacentFailure.Last==adjacentLast
        &&adjacentFailure.Lower<=.5&&adjacentFailure.Upper>=.5&&adjacentFailure.Upper>adjacentFailure.Lower
        &&adjacentRetry&&adjacentInner.Curve==adjacentRoot&&adjacentInner.First>adjacentFailure.Last
        &&adjacentInner.Last==2.&&adjacentA>adjacentFailure.Upper&&adjacentA<3.&&adjacentB==3.
        &&adjacentA==adjacentRoot->Value(adjacentInner.First).X(),
        "an unrepresentable unique target inverse returns no parameter, encloses its original positive ULP window, and retries only from a separate proved original native event while preserving every positive gap");

    const auto rationalCubic=[](double first,double last,const std::vector<double>& xs,const std::vector<double>& ys,const std::vector<double>& ws) {
        TColgp_Array1OfPnt2d p(1,4);TColStd_Array1OfReal w(1,4);for(int i=1;i<=4;++i){p(i)={xs.at(i-1),ys.at(i-1)};w(i)=ws.at(i-1);}
        TColStd_Array1OfReal k(1,2);k(1)=first;k(2)=last;TColStd_Array1OfInteger m(1,2);m(1)=m(2)=4;
        return Handle(Geom2d_BSplineCurve)(new Geom2d_BSplineCurve(p,w,k,m,3));
    };
    const double nineFirst=4.7123889803846897,nineLast=6.2831853071795862,nineRequest=2.3445191737212003e-14;
    const auto nineCurve=rationalCubic(nineFirst,nineLast,{-2.2737367544323206e-13,0.,1.1045694997997089,2.6666666669998449},
        {8.350694645007707,10.79655647937102,12.526041968294749,12.526041968294521},{1.,.8047378541243649,.8047378541243649,1.});remember(nineCurve);
    NativeProjectionInverseFailureWindow nineFailure;double nineParameter=123.;std::string nineReason;
    const bool nineSolved=RefineUniqueOriginalNativeSplineProjectionRoot({nineCurve,nineFirst,nineLast,28},nineRequest,{nineFirst,4.7123894621277058},nineParameter,nineReason,&nineFailure);
    auto nineTarget=sourceBranch(nineCurve,nineFirst,nineLast);nineTarget.EdgeId=28;
    TargetEndpointInverseFailure nineTargetFailure;nineTargetFailure.First=true;nineTargetFailure.FirstWindow=nineFailure;
    Branch nineInner;double nineA=-1.,nineB=-2.;std::string nineWindowReason;
    const bool nineRetry=BuildTargetEndpointNativeWindowRetry(nineTarget,nineRequest,2.6666666669998449,nineTargetFailure,nineInner,nineA,nineB,nineWindowReason);
    double ninePointLo=0.,ninePointHi=0.;std::string ninePointReason;
    check(!nineSolved&&nineParameter==123.&&nineFailure.bValid&&std::nextafter(nineFailure.First,nineFailure.Last)==nineFailure.Last
        &&nineFailure.First>nineFirst&&nineFailure.Last<nineLast&&nineRetry
        &&nineInner.Curve==nineCurve&&nineInner.First>nineFailure.Last&&nineInner.Last==nineLast
        &&nineA>nineFailure.Upper&&nineA>nineRequest&&nineA<nineB&&nineB==2.6666666669998449
        &&nineA==nineCurve->Value(nineInner.First).X()
        &&BoundNativeSplineProjectionPoint({nineCurve,nineFirst,nineLast},nineInner.First,ninePointLo,ninePointHi,ninePointReason)
        &&ninePointLo>nineFailure.Upper&&nineA>=ninePointLo&&nineA<=ninePointHi,
        "case9 exact original rational controls retain the genuine interior adjacent-double inverse gap and choose a distinct native event whose Point enclosure both contains native Value and lies strictly beyond the whole uncertainty band");
    records.emplace_back(ObjectMap{{"name",std::string("case9-target-inverse-window")},{"passed",nineRetry},{"failedS",nineRequest},{"excludedNative",VariantArray{nineFailure.First,nineFailure.Last}},{"outwardBand",VariantArray{nineFailure.Lower,nineFailure.Upper}},{"knownNativeEvent",nineInner.First},{"newSourceS",nineA},{"reason",nineWindowReason}});

    const double sixFirst=1.5707963267948968,sixLast=3.1415926535897931,sixRequest=57.171572875253275;
    const auto sixCurve=rationalCubic(sixFirst,sixLast,{57.171572875253318,57.171572875253545,56.06700337545384,54.504906208253473},
        {6.125734422706955,7.919920495707629,9.188601634636143,9.188601634636143},{1.,.804737854124365,.804737854124365,1.});remember(sixCurve);
    auto sixTarget=sourceBranch(sixCurve,sixFirst,sixLast);sixTarget.EdgeId=5;
    std::string sixCertificateReason;const bool sixUnique=CertifyUniqueOriginalNativeSplineProjectionRoot({sixCurve,sixFirst,sixLast},sixRequest,sixCertificateReason);
    NativeProjectionInverseFailureWindow sixFailure;
    const bool sixWindow=BuildOriginalTargetEndpointInverseFailureWindow(sixTarget,sixRequest,sixCertificateReason,sixFailure);
    TargetEndpointInverseFailure sixTargetFailure;sixTargetFailure.Last=true;sixTargetFailure.LastWindow=sixFailure;
    Branch sixInner{999,sixCurve,-8.,-7.,-6.,-5.,true};double sixA=-1.,sixB=-2.;std::string sixWindowReason="sentinel";
    const bool sixRetry=BuildTargetEndpointNativeWindowRetry(sixTarget,54.504906208253487,sixRequest,sixTargetFailure,sixInner,sixA,sixB,sixWindowReason);
    std::string sixOrderReason;const bool sixOrder=CertifyOriginalTargetProjectionOrder(sixTarget,sixOrderReason);
    check(!sixUnique&&sixCertificateReason=="native-spline-root-count-original-endpoint-intervals-do-not-prove-strict-opposite-signs"
        &&sixCurve->Pole(2).X()>sixCurve->Pole(1).X()&&sixWindow&&sixFailure.First==sixFirst
        &&sixFailure.Last==std::nextafter(sixFirst,sixLast)&&!sixOrder&&!sixRetry
        &&sixInner.EdgeId==999&&sixInner.Curve==sixCurve&&sixInner.First==-8.&&sixInner.Last==-7.
        &&sixInner.SFirst==-6.&&sixInner.SLast==-5.&&sixInner.bConstant&&sixA==-1.&&sixB==-2.&&sixWindowReason=="sentinel",
        "case6 keeps its real original control reversal and positive first-ULP band but refuses target retry without a complete original projection-order proof, preserving the original Pending domain and all output sentinels");
    records.emplace_back(ObjectMap{{"name",std::string("case6-target-endpoint-proof-window-order-refused")},{"passed",!sixRetry&&!sixOrder},{"failedS",sixRequest},{"outwardBand",VariantArray{sixFailure.Lower,sixFailure.Upper}},{"retryEnabled",sixRetry},{"reason",sixOrderReason}});

    // S(u)=u*(4u-3)^2 has a maximum 1 at u=1/4 and a
    // minimum 0 at u=3/4. A new event just after the latter can be above its
    // uncertainty band while its deleted prefix still reaches every retained
    // scalar below 1. Point separation cannot prove this prefix harmless.
    const auto returningPrefix=Cubic({0.,3.,-2.,1.});remember(returningPrefix);
    auto returningTarget=sourceBranch(returningPrefix,0.,1.);returningTarget.EdgeId=55;
    CurveUse returningUse{returningPrefix,0.,1.,55};NativeProjectionInverseFailureWindow returningWindow;
    const bool returningBound=EncloseOriginalTargetInverseFailureWindow(returningUse,.75,std::nextafter(.75,1.),0.,"negative-fixture-internal-window",returningWindow);
    double returningKnownNative=-1.,returningKnownS=-2.;
    const bool returningKnownEvent=target_native_inverse_window::FindKnownOriginalEvent(returningUse,returningWindow,true,true,returningKnownNative,returningKnownS);
    TargetEndpointInverseFailure returningFailure;returningFailure.First=true;returningFailure.FirstWindow=returningWindow;
    Branch returningInner{999,returningPrefix,-8.,-7.,-6.,-5.,true};double returningA=123.,returningB=456.;std::string returningReason="sentinel";
    const bool returningRetry=BuildTargetEndpointNativeWindowRetry(returningTarget,0.,1.,returningFailure,returningInner,returningA,returningB,returningReason);
    std::string returningOrderReason;
    check(returningBound&&returningKnownEvent&&returningKnownNative>returningWindow.Last
        &&returningKnownS>returningWindow.Upper&&returningKnownS<1.
        &&returningPrefix->Value(.25).X()==1.&&returningPrefix->Value(.75).X()==0.
        &&!CertifyOriginalTargetProjectionOrder(returningTarget,returningOrderReason)&&!returningRetry
        &&returningInner.EdgeId==999&&returningInner.First==-8.&&returningInner.Last==-7.
        &&returningInner.SFirst==-6.&&returningInner.SLast==-5.&&returningInner.bConstant
        &&returningA==123.&&returningB==456.&&returningReason=="sentinel",
        "an analytic genuinely returning deleted prefix cannot retry merely because a later native Point lies outside its local failure band; full original projection order is required before retaining a fixed first-hit winner");

    const double conicC=1.6666666672562087,conicA=8.2811627348462392e-10,conicB=-1.6666666670000012;
    const Handle(Geom2d_Circle) conicWindowCurve=new Geom2d_Circle(gp_Ax2d(gp_Pnt2d(conicC,0),gp_Dir2d(conicA,-conicB)),std::hypot(conicA,conicB));remember(conicWindowCurve);
    auto conicTarget=sourceBranch(conicWindowCurve,0.,1.5707963260149043);conicTarget.EdgeId=52;
    const double conicRequest=std::nextafter(conicTarget.SLast,conicTarget.SFirst);
    NativeProjectionInverseFailureWindow conicFailure;
    const bool conicWindow=BuildOriginalTargetEndpointInverseFailureWindow(conicTarget,conicRequest,"native-conic-no-original-range-root",conicFailure);
    TargetEndpointInverseFailure conicTargetFailure;conicTargetFailure.First=true;conicTargetFailure.FirstWindow=conicFailure;
    Branch conicInner;double conicLo=-1.,conicHi=-2.;std::string conicWindowReason;
    const bool conicRetry=BuildTargetEndpointNativeWindowRetry(conicTarget,conicRequest,1.6666666668443142,conicTargetFailure,conicInner,conicLo,conicHi,conicWindowReason);
    std::string conicOrderReason;
    check(conicWindow&&conicFailure.Last==conicTarget.Last&&conicFailure.First==std::nextafter(conicTarget.Last,conicTarget.First)
        &&CertifyOriginalTargetProjectionOrder(conicTarget,conicOrderReason)
        &&conicRetry&&conicInner.Curve==conicWindowCurve&&conicInner.First==conicTarget.First&&conicInner.Last<conicFailure.First
        &&conicLo>conicFailure.Upper&&conicLo==conicWindowCurve->Value(conicInner.Last).X()
        &&conicLo<conicHi&&conicHi==1.6666666668443142,
        "native conic endpoint cancellation uses only an original last-ULP continuous band and a separately verified inner event; no analytic root is clamped back into the original branch");
    const auto foldedConicTarget=sourceBranch(conicWindowCurve,0.,2.);std::string foldedConicOrderReason;
    check(!CertifyOriginalTargetProjectionOrder(foldedConicTarget,foldedConicOrderReason),
        "a conic domain extending beyond the proved sine-cosine quadrant cannot inherit a false whole-domain projection-order certificate across a real turning point");

    const auto rejectedTargetRetry=[&](const Branch& target,double a,double b,const TargetEndpointInverseFailure& failed) {
        Branch sentinel{999,windowCurve,-8.,-7.,-6.,-5.,true};double lo=123.,hi=456.;std::string reason="sentinel";
        return !BuildTargetEndpointNativeWindowRetry(target,a,b,failed,sentinel,lo,hi,reason)
            &&sentinel.EdgeId==999&&sentinel.Curve==windowCurve&&sentinel.First==-8.&&sentinel.Last==-7.
            &&sentinel.SFirst==-6.&&sentinel.SLast==-5.&&sentinel.bConstant&&lo==123.&&hi==456.&&reason=="sentinel";
    };
    auto wrongTargetFailure=nineTargetFailure;wrongTargetFailure.FirstWindow.EdgeId=999;
    auto wrongRequestFailure=nineTargetFailure;wrongRequestFailure.FirstWindow.RequestedS=std::nextafter(nineRequest,1.);
    auto wrongCurveFailure=nineTargetFailure;wrongCurveFailure.FirstWindow.Curve=sixCurve;
    auto wrongDomainFailure=nineTargetFailure;wrongDomainFailure.FirstWindow.OriginalFirst=std::nextafter(nineFirst,nineLast);
    check(rejectedTargetRetry(nineTarget,nineRequest,nineB,{})&&rejectedTargetRetry(nineTarget,nineRequest,nineB,wrongTargetFailure)
        &&rejectedTargetRetry(nineTarget,nineRequest,nineB,wrongRequestFailure)&&rejectedTargetRetry(nineTarget,nineRequest,nineB,wrongCurveFailure)
        &&rejectedTargetRetry(nineTarget,nineRequest,nineB,wrongDomainFailure)
        &&rejectedTargetRetry(nineTarget,nineRequest,std::nextafter(nineTarget.SLast,std::numeric_limits<double>::infinity()),nineTargetFailure)
        &&rejectedTargetRetry(nineTarget,nineRequest,nineRequest,nineTargetFailure),
        "target retries reject absent proof, foreign edge or curve, a different original domain or failed scalar, cells outside original scalar endpoints by even one ULP and collapsed cells without modifying any output sentinel");

    CurveUse preservedUse{inverseCube,0.,1.};const auto preservedFirst=preservedUse.First,preservedLast=preservedUse.Last;const auto inverseOriginal=Snapshot(inverseCube);
    std::vector<double> preservedSeeds{.25,.75};const auto seedsBefore=preservedSeeds;
    check(refined("native-inverse-original-parameter-and-seeds-preserved",preservedUse,.125,preservedSeeds,true)
        &&preservedUse.First==preservedFirst&&preservedUse.Last==preservedLast&&preservedSeeds==seedsBefore&&Snapshot(inverseCube)==inverseOriginal
        &&case90Bytes==G::Persistence::Serialize(case90Model),
        "native inverse refinement preserves the original use domain, kernel seed parameters, control data and original case90 neutral model bytes");

    const auto sharedEdge=BRepBuilderAPI_MakeEdge(quadratic,new Geom_Plane(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1))),0.,1.).Edge();
    const auto shapeBefore=G::Persistence::Serialize(O::ConvertOpenCascadeShapeToBRepGeometry(sharedEdge,"spline-bound-readonly","spline-bound-readonly"));
    const bool lastBound=bound("shared-native-pcurve-readonly",{quadratic,0.,1.},.25,.75,.75,1.);
    bool curvesUnchanged=true;for(const auto& [curve,snapshot]:originals)curvesUnchanged=curvesUnchanged&&Snapshot(curve)==snapshot;
    check(lastBound&&curvesUnchanged&&shapeBefore==G::Persistence::Serialize(O::ConvertOpenCascadeShapeToBRepGeometry(sharedEdge,"spline-bound-readonly","spline-bound-readonly")),
        "native spline bounding leaves every original pole, weight, knot, multiplicity, domain and shared-pcurve neutral shape byte-for-byte unchanged");
    Save(directory/"native-spline-bounds.json",{{"schema",1},{"expectations",std::string("independent polynomial/rational extrema; bounds enclose whole native intervals and are not monotonicity or Through certificates")},{"records",records},{"originalCurvesUnchanged",curvesUnchanged}});
}
}
