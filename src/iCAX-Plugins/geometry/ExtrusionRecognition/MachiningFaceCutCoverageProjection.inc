// Included before Branches in the coverage Work's private namespace.
// All projection events retain the unchanged native pcurve parameter. These
// helpers neither sample an intermediate point nor fit a replacement curve.
enum class NativeProjectionEventStatus { Unsupported, Complete, Invalid };

namespace native_conic_projection
{
struct Data final
{
    double C=0.,A=0.,B=0.,DerivativeBound=0.;
    bool bLine=false;
};

double Up(double value)
{
    return std::nextafter(value,std::numeric_limits<double>::infinity());
}

NativeProjectionEventStatus Read(const CurveUse& use,Data& result,std::string& reason)
{
    if(use.Curve.IsNull()||!std::isfinite(use.First)||!std::isfinite(use.Last)||!(use.Last>use.First)) {
        reason="native-conic-projection-has-invalid-original-domain";
        return NativeProjectionEventStatus::Invalid;
    }
    Geom2dAdaptor_Curve native(use.Curve,use.First,use.Last);
    switch(native.GetType()) {
    case GeomAbs_Line: {
        const auto line=native.Line();result.bLine=true;
        result.C=line.Location().X();result.A=line.Direction().X();
        result.DerivativeBound=Up(std::abs(result.A));
        break;
    }
    case GeomAbs_Circle: {
        const auto circle=native.Circle();const double radius=circle.Radius();
        if(!std::isfinite(radius)||!(radius>0.)) {
            reason="native-circle-projection-has-invalid-radius";return NativeProjectionEventStatus::Invalid;
        }
        result.C=circle.Location().X();
        result.A=radius*circle.Position().XDirection().X();
        result.B=radius*circle.Position().YDirection().X();
        // |S'| <= |A|+|B| <= 2R on the full native circle.
        result.DerivativeBound=Up(Up(radius)+Up(radius));
        break;
    }
    case GeomAbs_Ellipse: {
        const auto ellipse=native.Ellipse();
        const double major=ellipse.MajorRadius(),minor=ellipse.MinorRadius();
        if(!std::isfinite(major)||!std::isfinite(minor)||!(minor>0.)||major<minor) {
            reason="native-ellipse-projection-has-invalid-radii";return NativeProjectionEventStatus::Invalid;
        }
        result.C=ellipse.Location().X();
        result.A=major*ellipse.Axis().XDirection().X();
        result.B=minor*ellipse.Axis().YDirection().X();
        // |S'| <= |A|+|B| <= major+minor on the full native ellipse.
        result.DerivativeBound=Up(Up(major)+Up(minor));
        break;
    }
    default:
        return NativeProjectionEventStatus::Unsupported;
    }
    if(!std::isfinite(result.C)||!std::isfinite(result.A)||!std::isfinite(result.B)
        ||!std::isfinite(result.DerivativeBound)||result.DerivativeBound<0.
        ||(!result.bLine&&result.A==0.&&result.B==0.)) {
        reason="native-conic-projection-coefficients-are-not-finite-or-regular";
        return NativeProjectionEventStatus::Invalid;
    }
    return NativeProjectionEventStatus::Complete;
}
}

NativeProjectionEventStatus AppendNativeConicProjectionEvents(
    const CurveUse& use,std::vector<double>& events,std::string& reason)
{
    try {
        native_conic_projection::Data data;
        const auto status=native_conic_projection::Read(use,data,reason);
        if(status!=NativeProjectionEventStatus::Complete)return status;
        if(data.bLine) {
            reason="native-line-projection-has-no-interior-critical-parameters";
            return NativeProjectionEventStatus::Complete;
        }
        // S(u)=C+A cos(u)+B sin(u), so S'(u)=0 exactly at alpha+k*pi.
        // The two adjacent k candidates only guard floating-point quotient
        // rounding. An event is appended solely inside the ORIGINAL domain.
        const double pi=std::acos(-1.),alpha=std::atan2(data.B,data.A);
        using Wide=long double;
        const Wide firstIndex=std::floor((static_cast<Wide>(use.First)-alpha)/pi)-1.;
        const Wide lastIndex=std::ceil((static_cast<Wide>(use.Last)-alpha)/pi)+1.;
        constexpr Wide maximumExactIndex=4503599627370496.; // 2^52
        constexpr Wide maximumCandidates=65536.;
        if(!std::isfinite(firstIndex)||!std::isfinite(lastIndex)||lastIndex<firstIndex
            ||lastIndex-firstIndex+1.>maximumCandidates
            ||std::abs(firstIndex)>maximumExactIndex||std::abs(lastIndex)>maximumExactIndex) {
            reason="native-conic-projection-critical-parameter-count-or-index-is-not-representable";
            return NativeProjectionEventStatus::Invalid;
        }
        const auto first=static_cast<long long>(firstIndex),last=static_cast<long long>(lastIndex);
        std::vector<double> nativeEvents;
        for(long long k=first;k<=last;++k) {
            const double parameter=std::fma(static_cast<double>(k),pi,alpha);
            if(!std::isfinite(parameter)) {
                reason="native-conic-projection-critical-parameter-is-not-finite";
                return NativeProjectionEventStatus::Invalid;
            }
            if(!(parameter>use.First&&parameter<use.Last))continue;
            if(!nativeEvents.empty()&&!(parameter>nativeEvents.back())) {
                reason="native-conic-projection-distinct-critical-parameters-cannot-be-represented";
                return NativeProjectionEventStatus::Invalid;
            }
            nativeEvents.push_back(parameter);
        }
        events.insert(events.end(),nativeEvents.begin(),nativeEvents.end());
        reason="native-conic-projection-analytic-alpha-plus-k-pi-events-in-original-domain";
        return NativeProjectionEventStatus::Complete;
    } catch(const Standard_Failure&) {
        reason="native-conic-projection-original-curve-query-failed";
    } catch(const std::exception&) {
        reason="native-conic-projection-original-curve-query-failed";
    }
    return NativeProjectionEventStatus::Invalid;
}

bool BoundNativeConicProjection(const CurveUse& use,double first,double last,
    double& lower,double& upper,std::string& reason)
{
    try {
        native_conic_projection::Data data;
        if(native_conic_projection::Read(use,data,reason)!=NativeProjectionEventStatus::Complete)return false;
        if(!std::isfinite(first)||!std::isfinite(last)||!(last>first)||first<use.First||last>use.Last) {
            reason="native-conic-projection-bound-is-outside-original-positive-interval";return false;
        }
        // Two endpoint evaluations only. For every interior u, the full-domain
        // derivative bound gives |S(u)-S(endpoint)| <= M*(last-first).
        // Do not infer constancy merely because the endpoint S values round
        // to the same double; the positive native interval remains intact.
        const double a=use.Curve->Value(first).X(),b=use.Curve->Value(last).X();
        if(!std::isfinite(a)||!std::isfinite(b)) {
            reason="native-conic-projection-bound-endpoint-is-not-finite";return false;
        }
        const double span=native_conic_projection::Up(last-first);
        const double excursion=native_conic_projection::Up(data.DerivativeBound*span);
        const double scale=std::max({std::abs(data.C),std::abs(data.A),std::abs(data.B),
            data.DerivativeBound,std::abs(a),std::abs(b),std::numeric_limits<double>::min()});
        const double evaluationError=native_conic_projection::Up(
            64.*std::numeric_limits<double>::epsilon()*scale);
        const double pad=native_conic_projection::Up(excursion+evaluationError);
        const double lo=std::nextafter(std::min(a,b)-pad,-std::numeric_limits<double>::infinity());
        const double hi=native_conic_projection::Up(std::max(a,b)+pad);
        if(!std::isfinite(span)||!std::isfinite(excursion)||!std::isfinite(pad)
            ||!std::isfinite(lo)||!std::isfinite(hi)||!(hi>lo)) {
            reason="native-conic-projection-outward-bound-is-not-finite";return false;
        }
        lower=lo;upper=hi;
        reason="native-conic-original-interval-projection-enclosed-by-two-endpoints-and-global-derivative-bound-with-outward-rounding";
        return true;
    } catch(const Standard_Failure&) {
        reason="native-conic-projection-bound-original-curve-query-failed";
    } catch(const std::exception&) {
        reason="native-conic-projection-bound-original-curve-query-failed";
    }
    return false;
}

NativeProjectionEventStatus SolveNativeConicProjectionParameter(
    const Branch& branch,double s,double& parameter,std::string& reason)
{
    try {
        CurveUse use;use.EdgeId=branch.EdgeId;use.Curve=branch.Curve;
        use.First=branch.First;use.Last=branch.Last;
        native_conic_projection::Data data;
        const auto status=native_conic_projection::Read(use,data,reason);
        if(status!=NativeProjectionEventStatus::Complete)return status;
        if(!std::isfinite(s)) {
            reason="native-conic-projection-inverse-coordinate-is-not-finite";
            return NativeProjectionEventStatus::Invalid;
        }
        std::vector<double> roots;
        if(data.bLine) {
            if(data.A==0.) {
                reason="native-line-projection-inverse-is-not-single-valued";
                return NativeProjectionEventStatus::Invalid;
            }
            const double root=(s-data.C)/data.A;
            if(std::isfinite(root)&&root>=branch.First&&root<=branch.Last)roots.push_back(root);
        } else {
            // S=C+M*cos(u-alpha). An out-of-range ratio is not clamped:
            // uncertainty must remain an explicit local projection band.
            const double amplitude=std::hypot(data.A,data.B),ratio=(s-data.C)/amplitude;
            if(!std::isfinite(amplitude)||!(amplitude>0.)||!std::isfinite(ratio)||ratio< -1.||ratio>1.) {
                reason="native-conic-projection-inverse-has-no-real-native-root";
                return NativeProjectionEventStatus::Invalid;
            }
            const double pi=std::acos(-1.),period=2.*pi;
            const double alpha=std::atan2(data.B,data.A),phase=std::acos(ratio);
            using Wide=long double;
            constexpr Wide maximumExactIndex=4503599627370496.;
            constexpr Wide maximumCandidates=65536.;
            const auto appendFamily=[&](double base) {
                const Wide firstIndex=std::floor((static_cast<Wide>(branch.First)-base)/period)-1.;
                const Wide lastIndex=std::ceil((static_cast<Wide>(branch.Last)-base)/period)+1.;
                if(!std::isfinite(firstIndex)||!std::isfinite(lastIndex)||lastIndex<firstIndex
                    ||lastIndex-firstIndex+1.>maximumCandidates
                    ||std::abs(firstIndex)>maximumExactIndex||std::abs(lastIndex)>maximumExactIndex) {
                    reason="native-conic-projection-inverse-root-count-or-index-is-not-representable";
                    return false;
                }
                const auto first=static_cast<long long>(firstIndex),last=static_cast<long long>(lastIndex);
                for(long long k=first;k<=last;++k) {
                    const double root=std::fma(static_cast<double>(k),period,base);
                    if(!std::isfinite(root)) {
                        reason="native-conic-projection-inverse-root-is-not-finite";return false;
                    }
                    if(root>=branch.First&&root<=branch.Last)roots.push_back(root);
                }
                return true;
            };
            if(!appendFamily(alpha+phase))return NativeProjectionEventStatus::Invalid;
            // At exactly +/-1 the two analytic families are the SAME root
            // family. For every other ratio retain both families separately;
            // two positive-separated roots may never be tolerance-deduplicated.
            if(phase!=0.&&phase!=pi&&!appendFamily(alpha-phase))return NativeProjectionEventStatus::Invalid;
        }
        if(roots.size()!=1) {
            reason=roots.empty()?"native-conic-projection-inverse-has-no-root-in-original-branch":
                "native-conic-projection-inverse-has-multiple-roots-in-original-branch";
            return NativeProjectionEventStatus::Invalid;
        }
        const double root=roots.front(),actual=branch.Curve->Value(root).X();
        // This verifies the single analytically selected native root. The
        // allowance accounts only for coefficient/argument evaluation and
        // inverse arithmetic roundoff; it is never a geometric position
        // tolerance used to choose a root or expand the original branch.
        const double argumentScale=data.DerivativeBound*std::abs(root);
        const double scale=std::max({std::abs(data.C),std::abs(data.A),std::abs(data.B),
            std::abs(s),std::abs(actual),argumentScale,std::numeric_limits<double>::min()});
        const double error=native_conic_projection::Up(128.*std::numeric_limits<double>::epsilon()*scale);
        const double residual=native_conic_projection::Up(std::abs(actual-s));
        if(!std::isfinite(actual)||!std::isfinite(argumentScale)||!std::isfinite(error)
            ||!std::isfinite(residual)||residual>error) {
            reason="native-conic-projection-inverse-root-exceeds-floating-point-evaluation-error";
            return NativeProjectionEventStatus::Invalid;
        }
        parameter=root;
        reason="unique-native-line-or-conic-analytic-projection-root-in-unchanged-original-branch";
        return NativeProjectionEventStatus::Complete;
    } catch(const Standard_Failure&) {
        reason="native-conic-projection-inverse-original-curve-query-failed";
    } catch(const std::exception&) {
        reason="native-conic-projection-inverse-original-curve-query-failed";
    }
    return NativeProjectionEventStatus::Invalid;
}
