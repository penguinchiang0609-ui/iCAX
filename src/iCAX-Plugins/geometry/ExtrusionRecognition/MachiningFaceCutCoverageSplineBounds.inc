// Included after CurveUse is declared, in the coverage Work's private
// namespace. This is an enclosure of the unchanged native pcurve, not a
// monotonicity certificate, replacement curve, or sampled approximation.
namespace native_spline_projection_bounds
{
struct Interval final {double Lo=0.,Hi=0.;};
struct Homogeneous final {Interval N,W;};

double Down(double value){return std::nextafter(value,-std::numeric_limits<double>::infinity());}
double Up(double value){return std::nextafter(value,std::numeric_limits<double>::infinity());}
Interval Exact(double value){return {value,value};}
bool Finite(Interval value){return std::isfinite(value.Lo)&&std::isfinite(value.Hi)&&value.Lo<=value.Hi;}
Interval Checked(Interval value)
{
    if(!Finite(value))throw std::runtime_error("native-spline-projection-interval-arithmetic-is-not-finite");
    return value;
}
Interval Add(Interval a,Interval b){return Checked({Down(a.Lo+b.Lo),Up(a.Hi+b.Hi)});}
Interval Subtract(Interval a,Interval b){return Checked({Down(a.Lo-b.Hi),Up(a.Hi-b.Lo)});}
Interval Multiply(Interval a,Interval b)
{
    const double values[]{a.Lo*b.Lo,a.Lo*b.Hi,a.Hi*b.Lo,a.Hi*b.Hi};
    for(const auto value:values)if(!std::isfinite(value))
        throw std::runtime_error("native-spline-projection-interval-product-is-not-finite");
    return Checked({Down(*std::min_element(values,values+4)),Up(*std::max_element(values,values+4))});
}
Interval DividePositive(Interval a,Interval b)
{
    if(!(b.Lo>0.))throw std::runtime_error("native-spline-projection-interval-denominator-is-not-proven-positive");
    const double values[]{a.Lo/b.Lo,a.Lo/b.Hi,a.Hi/b.Lo,a.Hi/b.Hi};
    for(const auto value:values)if(!std::isfinite(value))
        throw std::runtime_error("native-spline-projection-interval-quotient-is-not-finite");
    return Checked({Down(*std::min_element(values,values+4)),Up(*std::max_element(values,values+4))});
}
Interval Intersect(Interval a,Interval b)
{
    return Checked({std::max(a.Lo,b.Lo),std::min(a.Hi,b.Hi)});
}
Interval ConvexBlend(Interval a,Interval b,Interval alpha)
{
    const auto value=Add(Multiply(Subtract(Exact(1.),alpha),a),Multiply(alpha,b));
    // The same true alpha lies in [0,1]. Independently of interval dependency,
    // the true blend is also in the convex hull of its two input intervals.
    // Intersecting those two proven enclosures prevents a positive weight
    // from spuriously reaching zero when alpha spans the complete [0,1].
    return Intersect(value,{std::min(a.Lo,b.Lo),std::max(a.Hi,b.Hi)});
}
Homogeneous Blend(const Homogeneous& a,const Homogeneous& b,Interval alpha)
{
    return {ConvexBlend(a.N,b.N,alpha),ConvexBlend(a.W,b.W,alpha)};
}
template<class Curve> std::vector<Homogeneous> OriginalPoles(const Curve& curve)
{
    if(curve->NbPoles()<2)throw std::runtime_error("native-spline-projection-has-too-few-original-poles");
    std::vector<Homogeneous> poles;poles.reserve(curve->NbPoles());
    for(int i=1;i<=curve->NbPoles();++i) {
        const auto point=curve->Pole(i);const double weight=curve->Weight(i);
        if(!std::isfinite(point.X())||!std::isfinite(point.Y())||!std::isfinite(weight)||!(weight>0.))
            throw std::runtime_error("native-spline-projection-requires-finite-original-poles-and-positive-weights");
        poles.push_back({Multiply(Exact(point.X()),Exact(weight)),Exact(weight)});
    }
    return poles;
}
Interval Project(const Homogeneous& value){return DividePositive(value.N,value.W);}

Interval Bezier(const occ::handle<Geom2d_BezierCurve>& curve,double first,double last,bool pointQuery=false)
{
    const double nativeFirst=curve->FirstParameter(),nativeLast=curve->LastParameter();
    if(!std::isfinite(nativeFirst)||!std::isfinite(nativeLast)||!(nativeLast>nativeFirst)
        ||first<nativeFirst||last>nativeLast||(pointQuery?first!=last:!(last>first)))
        throw std::runtime_error("native-bezier-projection-bound-exceeds-original-domain");
    auto work=OriginalPoles(curve);
    // Evaluate the entire ORIGINAL parameter interval at once. de Casteljau
    // is evaluated in homogeneous (X*W,W), so it also encloses rational curves.
    // [0,1] here follows from the unchanged native-domain inclusion above;
    // intersecting alpha with it never moves a requested curve parameter.
    const auto alpha=Intersect(DividePositive(Subtract({first,last},Exact(nativeFirst)),
        Subtract(Exact(nativeLast),Exact(nativeFirst))),{0.,1.});
    for(std::size_t remaining=work.size()-1;remaining>0;--remaining)
        for(std::size_t i=0;i<remaining;++i)work[i]=Blend(work[i],work[i+1],alpha);
    return Project(work.front());
}

Interval BSpline(const occ::handle<Geom2d_BSplineCurve>& curve,double first,double last,bool pointQuery=false)
{
    if(curve->IsPeriodic())throw std::runtime_error("native-bspline-projection-periodic-domain-is-not-supported");
    const int degree=curve->Degree(),poleCount=curve->NbPoles();
    const double nativeFirst=curve->FirstParameter(),nativeLast=curve->LastParameter();
    if(degree<1||degree>=poleCount||!std::isfinite(nativeFirst)||!std::isfinite(nativeLast)
        ||!(nativeLast>nativeFirst)||first<nativeFirst||last>nativeLast||(pointQuery?first!=last:!(last>first)))
        throw std::runtime_error("native-bspline-projection-bound-exceeds-original-domain-or-degree");
    for(int i=2;i<curve->NbKnots();++i)
        if(curve->Knot(i)>=first&&curve->Knot(i)<=last&&curve->Multiplicity(i)>degree)
            throw std::runtime_error("native-bspline-projection-original-domain-has-an-unproven-discontinuity");
    const auto poles=OriginalPoles(curve);
    const auto& originalKnots=curve->KnotSequence();
    if(originalKnots.Length()!=poleCount+degree+1)
        throw std::runtime_error("native-bspline-projection-flat-knot-and-pole-counts-disagree");
    std::vector<double> knots;knots.reserve(originalKnots.Length());
    for(int i=originalKnots.Lower();i<=originalKnots.Upper();++i) {
        const double knot=originalKnots(i);
        if(!std::isfinite(knot)||(!knots.empty()&&knot<knots.back()))
            throw std::runtime_error("native-bspline-projection-original-flat-knots-are-invalid");
        knots.push_back(knot);
    }
    if(knots.at(degree)!=nativeFirst||knots.at(poleCount)!=nativeLast)
        throw std::runtime_error("native-bspline-projection-original-flat-knot-domain-is-inconsistent");
    bool found=false;Interval result;double covered=first;
    for(int k=degree;k<poleCount;++k) {
        // Repeated knots never define an evaluable positive native span,
        // including a point query exactly at that shared knot. Only the
        // neighboring ORIGINAL nonzero spans may enter de Boor below.
        if(!(knots[k+1]>knots[k]))continue;
        const double lo=std::max(first,knots[k]),hi=std::min(last,knots[k+1]);
        if(pointQuery?hi<lo:!(hi>lo))continue;
        if(lo>covered)throw std::runtime_error("native-bspline-projection-original-knot-spans-do-not-cover-request");
        std::vector<Homogeneous> work;work.reserve(degree+1);
        for(int j=0;j<=degree;++j)work.push_back(poles.at(k-degree+j));
        // On this original nonzero knot span, U=[lo,hi] is entirely inside
        // [K_k,K_(k+1)]. Every de Boor denominator is positive, and every TRUE
        // alpha=(U-K_i)/(K_(i+p-r+1)-K_i) is in [0,1]. Outward interval
        // arithmetic encloses the whole family of values, never just selected
        // points. No Segment(), knot insertion, curve copy, fitting or repair
        // is used, and the requested native interval is never enlarged.
        // For a separate point query U=[u,u], both neighboring nonzero spans
        // participate at a shared knot. Their continuous one-sided values
        // are enclosed independently; discontinuities above are still rejected.
        for(int r=1;r<=degree;++r)for(int j=degree;j>=r;--j) {
            const int i=k-degree+j;
            const auto denominator=Subtract(Exact(knots.at(i+degree-r+1)),Exact(knots.at(i)));
            const auto alpha=Intersect(DividePositive(Subtract({lo,hi},Exact(knots.at(i))),denominator),{0.,1.});
            work[j]=Blend(work[j-1],work[j],alpha);
        }
        const auto piece=Project(work.back());
        if(!found){result=piece;found=true;}
        else result={std::min(result.Lo,piece.Lo),std::max(result.Hi,piece.Hi)};
        covered=std::max(covered,hi);
    }
    if(!found||covered<last)throw std::runtime_error("native-bspline-projection-original-knot-spans-do-not-cover-request");
    return Checked(result);
}

template<class Curve> int PositiveWeightPoleOrder(const Curve& curve,bool requireEqualWeights)
{
    const std::string prefix=requireEqualWeights?"native-polynomial-spline-projection-":"native-positive-rational-spline-projection-";
    if(curve->NbPoles()<2)throw std::runtime_error(prefix+"has-too-few-original-poles");
    const double weight=curve->Weight(1);
    if(!std::isfinite(weight)||!(weight>0.))
        throw std::runtime_error(prefix+(requireEqualWeights?"requires-equal-positive-original-weights":"requires-positive-original-weights"));
    int direction=0;double previous=0.;
    for(int i=1;i<=curve->NbPoles();++i) {
        const auto point=curve->Pole(i);const double currentWeight=curve->Weight(i);
        if(!std::isfinite(point.X())||!std::isfinite(point.Y())||!std::isfinite(currentWeight)||!(currentWeight>0.)
            ||(requireEqualWeights&&currentWeight!=weight))
            throw std::runtime_error(prefix+(requireEqualWeights?"requires-finite-poles-and-exactly-equal-original-weights":
                "requires-finite-poles-and-positive-original-weights"));
        if(i>1) {
            const int step=point.X()>previous?1:point.X()<previous?-1:0;
            if(step&&direction&&step!=direction)
                throw std::runtime_error(prefix+"original-adjacent-S-poles-reverse-direction");
            if(step)direction=step;
        }
        previous=point.X();
    }
    return direction;
}
}

bool BoundNativeSplineProjection(const CurveUse& use,double first,double last,
    double& lower,double& upper,std::string& reason)
{
    try {
        if(use.Curve.IsNull()||!std::isfinite(use.First)||!std::isfinite(use.Last)||!(use.Last>use.First)
            ||!std::isfinite(first)||!std::isfinite(last)||!(last>first)||first<use.First||last>use.Last) {
            reason="native-spline-projection-bound-is-outside-original-positive-interval";return false;
        }
        Geom2dAdaptor_Curve native(use.Curve,use.First,use.Last);
        native_spline_projection_bounds::Interval bound;
        if(native.GetType()==GeomAbs_BSplineCurve) {
            const auto curve=native.BSpline();
            if(use.First<curve->FirstParameter()||use.Last>curve->LastParameter()) {
                reason="native-bspline-projection-use-exceeds-original-support-domain";return false;
            }
            bound=native_spline_projection_bounds::BSpline(curve,first,last);
        } else if(native.GetType()==GeomAbs_BezierCurve) {
            const auto curve=native.Bezier();
            if(use.First<curve->FirstParameter()||use.Last>curve->LastParameter()) {
                reason="native-bezier-projection-use-exceeds-original-support-domain";return false;
            }
            bound=native_spline_projection_bounds::Bezier(curve,first,last);
        }
        else {reason="native-spline-projection-bound-support-is-not-bspline-or-bezier";return false;}
        if(!native_spline_projection_bounds::Finite(bound)) {
            reason="native-spline-projection-original-interval-bound-is-not-finite";return false;
        }
        lower=bound.Lo;upper=bound.Hi;
        reason="native-spline-original-interval-projection-enclosed-by-outward-homogeneous-de-boor-or-de-casteljau-on-original-positive-knot-spans";
        return true;
    } catch(const Standard_Failure&) {
        reason="native-spline-projection-original-curve-query-failed";
    } catch(const std::exception& error) {
        reason=error.what();
    }
    return false;
}

// A point enclosure is deliberately a separate API. It never admits a
// zero-width request to the positive-window coverage/uncertainty-band API.
bool BoundNativeSplineProjectionPoint(const CurveUse& use,double parameter,
    double& lower,double& upper,std::string& reason)
{
    try {
        if(use.Curve.IsNull()||!std::isfinite(use.First)||!std::isfinite(use.Last)||!(use.Last>use.First)
            ||!std::isfinite(parameter)||parameter<use.First||parameter>use.Last) {
            reason="native-spline-projection-point-is-outside-unchanged-original-domain";return false;
        }
        Geom2dAdaptor_Curve native(use.Curve,use.First,use.Last);
        native_spline_projection_bounds::Interval bound;
        if(native.GetType()==GeomAbs_BSplineCurve) {
            const auto curve=native.BSpline();
            if(use.First<curve->FirstParameter()||use.Last>curve->LastParameter()) {
                reason="native-bspline-projection-point-use-exceeds-original-support-domain";return false;
            }
            bound=native_spline_projection_bounds::BSpline(curve,parameter,parameter,true);
        } else if(native.GetType()==GeomAbs_BezierCurve) {
            const auto curve=native.Bezier();
            if(use.First<curve->FirstParameter()||use.Last>curve->LastParameter()) {
                reason="native-bezier-projection-point-use-exceeds-original-support-domain";return false;
            }
            bound=native_spline_projection_bounds::Bezier(curve,parameter,parameter,true);
        } else {reason="native-spline-projection-point-support-is-not-bspline-or-bezier";return false;}
        if(!native_spline_projection_bounds::Finite(bound)) {
            reason="native-spline-projection-point-enclosure-is-not-finite";return false;
        }
        lower=bound.Lo;upper=bound.Hi;
        reason="unchanged-original-native-parameter-projection-enclosed-by-outward-homogeneous-spline-point-evaluation";
        return true;
    } catch(const Standard_Failure&) {
        reason="native-spline-projection-point-original-curve-query-failed";
    } catch(const std::exception& error) {reason=error.what();}
    return false;
}

bool CertifyStrictOriginalSplineProjection(const CurveUse& use,
    int& direction,std::string& reason,bool requireEqualWeights)
{
    const std::string prefix=requireEqualWeights?"native-polynomial-":"native-positive-rational-";
    try {
        if(use.Curve.IsNull()||!std::isfinite(use.First)||!std::isfinite(use.Last)||!(use.Last>use.First)) {
            reason=prefix+"spline-projection-has-invalid-original-domain";return false;
        }
        Geom2dAdaptor_Curve native(use.Curve,use.First,use.Last);int certified=0;
        if(native.GetType()==GeomAbs_BSplineCurve) {
            const auto curve=native.BSpline();
            const double first=curve->FirstParameter(),last=curve->LastParameter();
            if(curve->IsPeriodic()||!std::isfinite(first)||!std::isfinite(last)||!(last>first)
                ||use.First<first||use.Last>last) {
                reason=prefix+"bspline-projection-requires-nonperiodic-original-support-domain";return false;
            }
            const int degree=curve->Degree();
            if(degree<1||degree>=curve->NbPoles()) {
                reason=prefix+"bspline-projection-original-degree-is-invalid";return false;
            }
            for(int i=2;i<curve->NbKnots();++i)
                if(curve->Knot(i)>first&&curve->Knot(i)<last&&curve->Multiplicity(i)>degree) {
                    reason=prefix+"bspline-projection-original-support-has-a-discontinuity";return false;
                }
            certified=native_spline_projection_bounds::PositiveWeightPoleOrder(curve,requireEqualWeights);
            const auto& knots=curve->KnotSequence();
            if(knots.Length()!=curve->NbPoles()+degree+1) {
                reason=prefix+"bspline-projection-original-flat-knot-count-is-invalid";return false;
            }
            for(int i=knots.Lower();i<=knots.Upper();++i)
                if(!std::isfinite(knots(i))||(i>knots.Lower()&&knots(i)<knots(i-1))) {
                    reason=prefix+"bspline-projection-original-flat-knots-are-invalid";return false;
                }
            if(knots(knots.Lower()+degree)!=first||knots(knots.Lower()+curve->NbPoles())!=last) {
                reason=prefix+"bspline-projection-original-flat-knot-domain-is-inconsistent";return false;
            }
            bool active=false;
            for(int k=degree;k<curve->NbPoles();++k) {
                const double lo=std::max(use.First,knots(knots.Lower()+k));
                const double hi=std::min(use.Last,knots(knots.Lower()+k+1));
                if(!(hi>lo))continue;
                active=true;
                // Global pole order is already proven. Thus the p+1 active
                // poles are all equal iff their first and last S are equal.
                // A real positive-width platform is never accepted, even if
                // some other part of the original support is nonconstant.
                if(curve->Pole(k-degree+1).X()==curve->Pole(k+1).X()) {
                    reason=prefix+"bspline-projection-original-active-knot-span-is-a-real-platform";return false;
                }
            }
            if(!active) {reason=prefix+"bspline-projection-original-domain-has-no-positive-active-span";return false;}
        } else if(native.GetType()==GeomAbs_BezierCurve) {
            const auto curve=native.Bezier();
            const double first=curve->FirstParameter(),last=curve->LastParameter();
            if(!std::isfinite(first)||!std::isfinite(last)||!(last>first)||use.First<first||use.Last>last) {
                reason=prefix+"bezier-projection-use-exceeds-original-support-domain";return false;
            }
            certified=native_spline_projection_bounds::PositiveWeightPoleOrder(curve,requireEqualWeights);
        } else {reason=prefix+"spline-projection-support-is-not-bspline-or-bezier";return false;}
        // Polynomial mode still requires EXACTLY equal positive weights,
        // which cancel without changing the original polynomial spline.
        // The separate positive-rational mode uses the native B-spline (or
        // Bernstein) basis B_i, positive original w_i, and W=sum(w_i*B_i):
        //   S' = sum_(i<j) w_i*w_j*(S_j-S_i)
        //          *(B_i*B'_j-B'_i*B_j) / W^2.
        // B-spline bases are TP2, so their two-point determinants are >=0;
        // taking the positive parameter increment to zero gives each bracket
        // >=0 inside an original nonzero knot span. Thus the exact original
        // pole order proves a nonnegative/nonpositive derivative there for
        // ANY positive weights, not just equal weights.
        // Strictness is a separate condition: no active span has all its
        // p+1 original S poles equal. If S were constant on a positive-width
        // subinterval, analyticity on that span would make
        // sum(w_i*(S_i-c)*B_i) identically zero. Local linear independence
        // forces every active S_i=c, contradicting the no-platform check.
        // Monotonicity plus absence of any such plateau proves strict order;
        // isolated zero derivatives remain legal. C0 knot joins preserve the
        // same order. For Bezier, its one original active span is nonconstant
        // exactly when at least one original adjacent pole step is strict.
        // Repeated adjacent poles and a zero endpoint derivative are allowed;
        // a real active BSP platform or a reversal of any size is rejected.
        // No trimming, reparameterization, approximate pole-order tolerance,
        // derivative samples, or replacement polynomial is introduced.
        if(!certified) {reason=prefix+"spline-projection-has-no-strict-original-pole-direction";return false;}
        direction=certified;
        reason=requireEqualWeights?
            "strict-original-polynomial-spline-projection-by-exactly-equal-positive-weights-and-same-way-original-S-poles-with-no-active-native-platform":
            "strict-original-positive-rational-spline-projection-by-TP2-positive-original-weights-and-same-way-original-S-poles-with-no-active-native-platform";
        return true;
    } catch(const Standard_Failure&) {
        reason=prefix+"spline-projection-original-curve-query-failed";
    } catch(const std::exception& error) {reason=error.what();}
    return false;
}

bool CertifyStrictOriginalPolynomialSplineProjection(const CurveUse& use,
    int& direction,std::string& reason)
{
    return CertifyStrictOriginalSplineProjection(use,direction,reason,true);
}

bool CertifyStrictOriginalPositiveRationalSplineProjection(const CurveUse& use,
    int& direction,std::string& reason)
{
    return CertifyStrictOriginalSplineProjection(use,direction,reason,false);
}
