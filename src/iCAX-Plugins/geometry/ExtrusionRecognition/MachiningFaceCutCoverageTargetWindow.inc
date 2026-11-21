// A failed inverse is not a parameter. Keep its original positive native
// window and its continuous projection enclosure separate from a retry event.
struct NativeProjectionInverseFailureWindow final
{
    bool bValid=false;
    std::uint64_t EdgeId=0;
    occ::handle<Geom2d_Curve> Curve;
    double OriginalFirst=0.,OriginalLast=0.,First=0.,Last=0.;
    double RequestedS=0.,Lower=0.,Upper=0.;
    std::string Reason;
};

struct TargetEndpointInverseFailure final
{
    bool First=false,Last=false;
    NativeProjectionInverseFailureWindow FirstWindow,LastWindow;
};

bool EncloseOriginalTargetInverseFailureWindow(const CurveUse& use,
    double first,double last,double s,const std::string& cause,
    NativeProjectionInverseFailureWindow& output)
{
    if(use.Curve.IsNull()||!std::isfinite(use.First)||!std::isfinite(use.Last)
        ||!(use.Last>use.First)||!std::isfinite(first)||!std::isfinite(last)
        ||!(last>first)||first<use.First||last>use.Last||!std::isfinite(s))return false;
    double lower=0.,upper=0.;std::string boundReason;
    if(!BoundNativeSplineProjection(use,first,last,lower,upper,boundReason)
        &&!BoundNativeConicProjection(use,first,last,lower,upper,boundReason))return false;
    if(!std::isfinite(lower)||!std::isfinite(upper)||!(upper>lower)||s<lower||s>upper)return false;
    NativeProjectionInverseFailureWindow value;
    value.bValid=true;value.EdgeId=use.EdgeId;value.Curve=use.Curve;
    value.OriginalFirst=use.First;value.OriginalLast=use.Last;
    value.First=first;value.Last=last;value.RequestedS=s;
    value.Lower=lower;value.Upper=upper;value.Reason=cause+";"+boundReason;
    output=std::move(value);return true;
}

bool BuildOriginalTargetEndpointInverseFailureWindow(const Branch& target,double s,
    const std::string& cause,NativeProjectionInverseFailureWindow& output)
{
    try {
        if(target.Curve.IsNull()||target.bConstant||!(target.Last>target.First)
            ||!std::isfinite(s)||!(s>std::min(target.SFirst,target.SLast)
                &&s<std::max(target.SFirst,target.SLast)))return false;
        CurveUse use;use.EdgeId=target.EdgeId;use.Curve=target.Curve;
        use.First=target.First;use.Last=target.Last;
        const double next=std::nextafter(target.First,target.Last);
        const double previous=std::nextafter(target.Last,target.First);
        if(!(next>target.First&&next<target.Last&&previous>target.First&&previous<target.Last))return false;
        NativeProjectionInverseFailureWindow first,last;
        const bool firstMatches=EncloseOriginalTargetInverseFailureWindow(use,target.First,next,s,cause,first);
        const bool lastMatches=EncloseOriginalTargetInverseFailureWindow(use,previous,target.Last,s,cause,last);
        // A bound covering both ends cannot identify a local native window.
        if(firstMatches==lastMatches)return false;
        output=firstMatches?std::move(first):std::move(last);return true;
    } catch(const Standard_Failure&) {return false;}
    catch(const std::exception&) {return false;}
}

namespace target_native_inverse_window
{
bool Point(const CurveUse& use,double parameter,double& lower,double& upper)
{
    std::string reason;
    if(BoundNativeSplineProjectionPoint(use,parameter,lower,upper,reason))return true;
    // The conic enclosure covers an original adjacent positive window that
    // includes this point. It is deliberately not a rounded inverse residual.
    const double adjacent=std::nextafter(parameter,parameter==use.Last?use.First:use.Last);
    if(!std::isfinite(adjacent)||adjacent<use.First||adjacent>use.Last||adjacent==parameter)return false;
    return BoundNativeConicProjection(use,std::min(parameter,adjacent),
        std::max(parameter,adjacent),lower,upper,reason);
}

bool KnownEventOutsideBand(const CurveUse& use,double parameter,
    const NativeProjectionInverseFailureWindow& window,bool lowScalarFailure,double& s)
{
    double lower=0.,upper=0.;
    if(!Point(use,parameter,lower,upper)||!std::isfinite(lower)||!std::isfinite(upper)
        ||upper<lower||(lowScalarFailure?!(lower>window.Upper):!(upper<window.Lower)))return false;
    const double actual=use.Curve->Value(parameter).X();
    // OCCT's rounded Value is not automatically inside an original-control
    // point enclosure. Only a known event satisfying BOTH may become a trim.
    if(!std::isfinite(actual)||actual<lower||actual>upper)return false;
    s=actual;return true;
}

bool FindKnownOriginalEvent(const CurveUse& use,
    const NativeProjectionInverseFailureWindow& window,bool lowScalarFailure,
    bool nativeIncreasing,double& parameter,double& s)
{
    const bool towardsLast=lowScalarFailure==nativeIncreasing;
    const double nativeNear=towardsLast?window.Last:window.First;
    const double nativeFar=towardsLast?use.Last:use.First;
    if(towardsLast?!(nativeFar>nativeNear):!(nativeFar<nativeNear))return false;
    const double adjacent=std::nextafter(nativeNear,nativeFar);
    if(!std::isfinite(adjacent)||adjacent==nativeNear)return false;
    double eventS=0.;
    if(KnownEventOutsideBand(use,adjacent,window,lowScalarFailure,eventS)) {
        parameter=adjacent;s=eventS;return true;
    }

    // Search only for a proved ORIGINAL native event. These queries never
    // return the failed inverse or generate intermediate tool-axis samples.
    // Doubling finds a finite bracket; bisection retains an actually proved
    // event. Validity need not be monotone: no rejected point proves a sign,
    // excludes a root, or authorizes filling the interval before the result.
    double rejected=adjacent,accepted=0.,acceptedS=0.;
    bool found=false;double distance=std::abs(adjacent-nativeNear);
    for(std::size_t i=0;i<2048;++i) {
        const double doubled=distance*2.;
        double trial=nativeFar;
        if(std::isfinite(doubled)&&doubled>distance) {
            const double proposed=towardsLast?nativeNear+doubled:nativeNear-doubled;
            if(std::isfinite(proposed)&&(towardsLast?proposed<nativeFar:proposed>nativeFar))trial=proposed;
        }
        if(trial==rejected)trial=std::nextafter(rejected,nativeFar);
        if(!std::isfinite(trial)||trial==rejected||trial<use.First||trial>use.Last)break;
        if(KnownEventOutsideBand(use,trial,window,lowScalarFailure,eventS)) {
            accepted=trial;acceptedS=eventS;found=true;break;
        }
        if(trial==nativeFar)break;
        rejected=trial;distance=std::abs(trial-nativeNear);
    }
    if(!found)return false;
    for(std::size_t i=0;i<128;++i) {
        const double first=std::min(rejected,accepted),last=std::max(rejected,accepted);
        if(std::nextafter(first,last)==last)break;
        double trial=first+(last-first)*.5;
        if(!std::isfinite(trial)||!(trial>first&&trial<last))break;
        if(KnownEventOutsideBand(use,trial,window,lowScalarFailure,eventS)) {
            accepted=trial;acceptedS=eventS;
        } else rejected=trial;
    }
    parameter=accepted;s=acceptedS;return true;
}
}

bool CertifyOriginalTargetProjectionOrder(const Branch& target,std::string& reason)
{
    try {
        if(target.Curve.IsNull()||!std::isfinite(target.First)||!std::isfinite(target.Last)
            ||!(target.Last>target.First)||!std::isfinite(target.SFirst)||!std::isfinite(target.SLast)
            ||target.SFirst==target.SLast) {
            reason="target-original-projection-order-has-invalid-domain-or-endpoints";return false;
        }
        CurveUse use;use.EdgeId=target.EdgeId;use.Curve=target.Curve;use.First=target.First;use.Last=target.Last;
        const int expected=target.SLast>target.SFirst?1:-1;
        int direction=0;std::string splineReason;
        if(CertifyStrictOriginalPolynomialSplineProjection(use,direction,splineReason)
            ||CertifyStrictOriginalPositiveRationalSplineProjection(use,direction,splineReason)) {
            if(direction!=expected){reason="target-original-projection-order-disagrees-with-native-endpoint-sense";return false;}
            reason="original-target-strict-projection-order;"+splineReason;return true;
        }
        native_conic_projection::Data data;std::string conicReason;
        if(native_conic_projection::Read(use,data,conicReason)==NativeProjectionEventStatus::Complete) {
            int conicDirection=0;
            if(data.bLine)conicDirection=data.A>0.?1:data.A<0.?-1:0;
            else {
                // This exact binary rational is strictly below mathematical
                // pi/2. No rounded atan2 critical event or angular tolerance
                // proves the quadrant. On [0,q], sin>=0 and cos>0; on [-q,0],
                // sin<=0 and cos>0. The coefficient signs therefore prove the
                // sign of S'=-A*sin(u)+B*cos(u) on the WHOLE original domain.
                constexpr double quarterTurnLower=0x1.921fb54442d17p+0;
                if(use.First>=0.&&use.Last<=quarterTurnLower) {
                    if(data.A>=0.&&data.B<=0.&&(data.A>0.||data.B<0.))conicDirection=-1;
                    if(data.A<=0.&&data.B>=0.&&(data.A<0.||data.B>0.))conicDirection=1;
                } else if(use.First>=-quarterTurnLower&&use.Last<=0.) {
                    if(data.A>=0.&&data.B>=0.&&(data.A>0.||data.B>0.))conicDirection=1;
                    if(data.A<=0.&&data.B<=0.&&(data.A<0.||data.B<0.))conicDirection=-1;
                }
            }
            if(conicDirection==expected) {
                reason=data.bLine?"original-target-strict-native-line-projection-order":
                    "original-target-strict-native-conic-projection-order-by-whole-domain-sine-cosine-quadrant-and-coefficient-signs";
                return true;
            }
        }
        reason="target-original-projection-order-unproved;"+splineReason;
        if(!conicReason.empty())reason+=";"+conicReason;
    } catch(const Standard_Failure&) {reason="target-original-projection-order-native-query-failed";}
    catch(const std::exception&) {reason="target-original-projection-order-data-query-failed";}
    return false;
}

bool BuildTargetEndpointNativeWindowRetry(const Branch& target,double a,double b,
    const TargetEndpointInverseFailure& failure,Branch& inner,
    double& clippedA,double& clippedB,std::string& reason)
{
    try {
        if((!failure.First&&!failure.Last)||target.Curve.IsNull()||target.bConstant
            ||!std::isfinite(target.First)||!std::isfinite(target.Last)||!(target.Last>target.First)
            ||!std::isfinite(target.SFirst)||!std::isfinite(target.SLast)||target.SFirst==target.SLast
            ||!std::isfinite(a)||!std::isfinite(b)||!(b>a))return false;
        if(a<std::min(target.SFirst,target.SLast)||b>std::max(target.SFirst,target.SLast))return false;
        if(target.Curve->Value(target.First).X()!=target.SFirst
            ||target.Curve->Value(target.Last).X()!=target.SLast)return false;
        // A valid new Point alone says nothing about a deleted prefix/suffix.
        // Full original projection order proves that every deleted native part
        // stays on its respective scalar side of the retained interval. This
        // deliberately rejects even tiny true folds that old Branches may have
        // omitted as near-end critical events. No retry may hide an earlier hit.
        std::string orderReason;
        if(!CertifyOriginalTargetProjectionOrder(target,orderReason))return false;
        const bool increasing=target.SLast>target.SFirst;
        CurveUse use;use.EdgeId=target.EdgeId;use.Curve=target.Curve;
        use.First=target.First;use.Last=target.Last;
        Branch retry=target;double nextA=a,nextB=b;
        std::ostringstream windows;windows<<std::setprecision(17);
        const auto exclude=[&](bool low,const NativeProjectionInverseFailureWindow& window) {
            const double requested=low?a:b;
            if(!window.bValid||window.EdgeId!=target.EdgeId||window.Curve!=target.Curve
                ||window.OriginalFirst!=target.First||window.OriginalLast!=target.Last
                ||window.RequestedS!=requested||!(window.Last>window.First)
                ||window.First<target.First||window.Last>target.Last
                ||!std::isfinite(window.Lower)||!std::isfinite(window.Upper)||!(window.Upper>window.Lower)
                ||requested<window.Lower||requested>window.Upper)return false;
            double nativeEvent=0.,scalarEvent=0.;
            if(!target_native_inverse_window::FindKnownOriginalEvent(use,window,low,increasing,nativeEvent,scalarEvent)
                ||!(scalarEvent>a&&scalarEvent<b))return false;
            if(low){nextA=scalarEvent;if(increasing){retry.First=nativeEvent;retry.SFirst=scalarEvent;}
                else {retry.Last=nativeEvent;retry.SLast=scalarEvent;}}
            else {nextB=scalarEvent;if(increasing){retry.Last=nativeEvent;retry.SLast=scalarEvent;}
                else {retry.First=nativeEvent;retry.SFirst=scalarEvent;}}
            windows<<";failed-"<<(low?"first":"last")<<"-s="<<requested
                <<";excluded-target-native=["<<window.First<<","<<window.Last
                <<"];outward-band=["<<window.Lower<<","<<window.Upper
                <<"];known-native-event="<<nativeEvent<<";known-event-s="<<scalarEvent
                <<";window-proof="<<window.Reason;
            return true;
        };
        if(failure.First&&!exclude(true,failure.FirstWindow))return false;
        if(failure.Last&&!exclude(false,failure.LastWindow))return false;
        if(!(retry.Last>retry.First)||!(nextB>nextA)
            ||retry.First<target.First||retry.Last>target.Last
            ||!(retry.SLast!=retry.SFirst)||((retry.SLast>retry.SFirst)!=increasing)
            ||nextA<a||nextB>b||(nextA==a&&nextB==b))return false;
        std::ostringstream detail;detail<<std::setprecision(17)
            <<"target-native-inverse-local-window-excluded-for-inner-retry"
            <<";target-edge="<<target.EdgeId<<";original-target-native=["<<target.First<<","<<target.Last
            <<"];inner-target-native=["<<retry.First<<","<<retry.Last
            <<"];original-source-cell=["<<a<<","<<b<<"];inner-source-cell=["<<nextA<<","<<nextB
            <<"];same-original-curve-and-cell-winner;window-and-event-gap-remain-unmapped"
            <<";deleted-native-parts-cannot-reenter-retained-scalar-cell="<<orderReason<<windows.str();
        auto completedReason=detail.str();
        inner=std::move(retry);clippedA=nextA;clippedB=nextB;reason.swap(completedReason);return true;
    } catch(const Standard_Failure&) {return false;}
    catch(const std::exception&) {return false;}
}
