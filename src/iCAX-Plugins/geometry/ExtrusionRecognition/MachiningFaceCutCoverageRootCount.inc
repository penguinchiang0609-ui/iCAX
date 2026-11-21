// A request-specific certificate. The original projection S(u) may have a
// genuine fold: this proves uniqueness only for the requested scalar s.
namespace original_spline_root_count
{
template<class Curve> bool OneOriginalSignChange(const Curve& curve,double s,
    std::string& reason)
{
    if(curve->NbPoles()<2){reason="native-spline-root-count-has-too-few-original-poles";return false;}
    int previous=0,variations=0;
    for(int i=1;i<=curve->NbPoles();++i) {
        const auto pole=curve->Pole(i);const double weight=curve->Weight(i);
        if(!std::isfinite(pole.X())||!std::isfinite(pole.Y())||!std::isfinite(weight)||!(weight>0.)) {
            reason="native-spline-root-count-requires-finite-original-poles-and-positive-weights";return false;
        }
        // c_i=w_i*(X_i-s). Its EXACT real sign is this comparison of the
        // stored doubles, since w_i>0. No rounded subtraction/product, epsilon
        // zero test, approximate root merging or replacement control net.
        const int sign=pole.X()>s?1:pole.X()<s?-1:0;
        if(!sign)continue;
        if(previous&&sign!=previous)++variations;
        previous=sign;
    }
    if(variations!=1){reason="native-spline-root-count-original-coefficient-sign-variation-is-not-one";return false;}
    return true;
}
}

bool CertifyUniqueOriginalNativeSplineProjectionRoot(const CurveUse& use,
    double s,std::string& reason)
{
    try {
        if(use.Curve.IsNull()||!std::isfinite(use.First)||!std::isfinite(use.Last)
            ||!(use.Last>use.First)||!std::isfinite(s)) {
            reason="native-spline-root-count-has-invalid-original-domain-or-request";return false;
        }
        Geom2dAdaptor_Curve native(use.Curve,use.First,use.Last);
        if(native.GetType()==GeomAbs_BSplineCurve) {
            const auto curve=native.BSpline();const int degree=curve->Degree(),count=curve->NbPoles();
            const double first=curve->FirstParameter(),last=curve->LastParameter();
            if(curve->IsPeriodic()||degree<1||degree>=count||!std::isfinite(first)||!std::isfinite(last)
                ||!(last>first)||use.First<first||use.Last>last) {
                reason="native-spline-root-count-requires-an-unchanged-nonperiodic-original-support-domain";return false;
            }
            const auto& knots=curve->KnotSequence();
            if(knots.Length()!=count+degree+1){reason="native-spline-root-count-original-flat-knot-count-is-invalid";return false;}
            for(int i=knots.Lower();i<=knots.Upper();++i)
                if(!std::isfinite(knots(i))||(i>knots.Lower()&&knots(i)<knots(i-1))) {
                    reason="native-spline-root-count-original-flat-knots-are-invalid";return false;
                }
            if(knots(knots.Lower()+degree)!=first||knots(knots.Lower()+count)!=last) {
                reason="native-spline-root-count-original-flat-knot-domain-is-inconsistent";return false;
            }
            for(int i=2;i<curve->NbKnots();++i)
                if(curve->Multiplicity(i)>degree) {
                    reason="native-spline-root-count-original-support-is-not-continuous";return false;
                }
            if(!original_spline_root_count::OneOriginalSignChange(curve,s,reason))return false;
            for(int k=degree;k<count;++k) {
                const double a=std::max(use.First,knots(knots.Lower()+k));
                const double b=std::min(use.Last,knots(knots.Lower()+k+1));
                if(!(b>a))continue;
                bool nonzero=false;
                for(int j=k-degree+1;j<=k+1;++j)nonzero=nonzero||curve->Pole(j).X()!=s;
                if(!nonzero){reason="native-spline-root-count-has-an-original-active-zero-platform";return false;}
            }
            // A knot contact can be a zero without a sign crossing. The
            // linear spline [+1,0,+1,-1] is the counterexample to merely
            // counting nonzero coefficient signs. Prove EVERY internal knot
            // in this requested domain nonzero, not just selected knots.
            for(int i=2;i<curve->NbKnots();++i) {
                const double knot=curve->Knot(i);if(!(knot>use.First&&knot<use.Last))continue;
                double lo=0.,hi=0.;std::string pointReason;
                if(!BoundNativeSplineProjectionPoint(use,knot,lo,hi,pointReason)||!(s<lo||s>hi)) {
                    reason="native-spline-root-count-original-internal-knot-is-not-strictly-separated-from-request";return false;
                }
            }
        } else if(native.GetType()==GeomAbs_BezierCurve) {
            const auto curve=native.Bezier();const double first=curve->FirstParameter(),last=curve->LastParameter();
            if(!std::isfinite(first)||!std::isfinite(last)||!(last>first)||use.First<first||use.Last>last) {
                reason="native-spline-root-count-request-exceeds-original-bezier-support";return false;
            }
            if(!original_spline_root_count::OneOriginalSignChange(curve,s,reason))return false;
        } else {reason="native-spline-root-count-support-is-not-bspline-or-bezier";return false;}

        double firstLo=0.,firstHi=0.,lastLo=0.,lastHi=0.;std::string pointReason;
        if(!BoundNativeSplineProjectionPoint(use,use.First,firstLo,firstHi,pointReason)
            ||!BoundNativeSplineProjectionPoint(use,use.Last,lastLo,lastHi,pointReason)
            ||!((firstHi<s&&lastLo>s)||(firstLo>s&&lastHi<s))) {
            reason="native-spline-root-count-original-endpoint-intervals-do-not-prove-strict-opposite-signs";return false;
        }
        // Let F=sum c_i B_i and H=sum |c_i| B_i. Dropping exact-zero columns
        // and positive scaling preserve TP2. With one exact sign transition,
        // F/H is a rational spline with monotone control values -1/+1 (or
        // +1/-1), hence monotone wherever H>0. Each open original knot span
        // has independent local basis functions: F cannot vanish on a
        // positive subinterval unless all its active c_i vanish, rejected
        // above. On such spans H>0. Strictly nonzero internal knots ensure
        // H>0 there as well; the ratio is continuous over the entire request.
        // Therefore its zero level cannot contain two points or a platform.
        // The rigorous, strictly opposite endpoint intervals give existence
        // by IVT. This proves ONE interior root for THIS s, while preserving
        // every real projection fold and every original native parameter.
        reason="one-original-native-spline-root-by-one-exact-control-sign-change-no-zero-knot-or-platform-and-strict-endpoint-interval-signs";
        return true;
    } catch(const Standard_Failure&) {
        reason="native-spline-root-count-original-curve-query-failed";
    } catch(const std::exception&) {reason="native-spline-root-count-original-data-query-failed";}
    return false;
}
