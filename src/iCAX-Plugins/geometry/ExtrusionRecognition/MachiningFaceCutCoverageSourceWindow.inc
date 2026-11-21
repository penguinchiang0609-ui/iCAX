// Included after Branch, CurveUse and the original native spline-bound helper.
// First/Last describe the scalar cell endpoints a/b, not native curve ends.
struct SourceEndpointInverseFailure final
{
    bool First=false,Last=false;
};

bool BuildSourceEndpointNativeWindowRetry(const Branch& source,double a,double b,
    const SourceEndpointInverseFailure& failure,Branch& inner,
    double& clippedA,double& clippedB,std::string& reason)
{
    try {
        if((!failure.First&&!failure.Last)||source.Curve.IsNull()||source.bConstant
            ||!std::isfinite(source.First)||!std::isfinite(source.Last)||!(source.Last>source.First)
            ||!std::isfinite(source.SFirst)||!std::isfinite(source.SLast)||source.SFirst==source.SLast
            ||!std::isfinite(a)||!std::isfinite(b)||!(b>a))return false;

        // These are the unchanged native curve values. A stored chart event
        // from some other curve or a normalized endpoint is not admissible.
        const double originalFirstS=source.Curve->Value(source.First).X();
        const double originalLastS=source.Curve->Value(source.Last).X();
        if(!std::isfinite(originalFirstS)||!std::isfinite(originalLastS)
            ||originalFirstS!=source.SFirst||originalLastS!=source.SLast)return false;
        const bool increasing=originalLastS>originalFirstS;

        CurveUse original;original.EdgeId=source.EdgeId;original.Curve=source.Curve;
        original.First=source.First;original.Last=source.Last;
        double nativeFirst=source.First,nativeLast=source.Last;
        bool removedFirst=false,removedLast=false;
        const auto excludeWindow=[&](bool nativeStart,double failedS) {
            const double adjacent=std::nextafter(nativeStart?source.First:source.Last,
                nativeStart?source.Last:source.First);
            if(!std::isfinite(adjacent)||!(adjacent>source.First&&adjacent<source.Last))return false;
            const double first=nativeStart?source.First:adjacent;
            const double last=nativeStart?adjacent:source.Last;
            double lower=0.,upper=0.;std::string boundReason;
            // The whole original one-ULP parameter window is enclosed. Neither
            // a distance to an endpoint nor a point evaluation proves this.
            if(!BoundNativeSplineProjection(original,first,last,lower,upper,boundReason)
                ||!std::isfinite(lower)||!std::isfinite(upper)||upper<lower
                ||failedS<lower||failedS>upper)return false;
            if(nativeStart){nativeFirst=adjacent;removedFirst=true;}
            else {nativeLast=adjacent;removedLast=true;}
            return true;
        };

        // The low scalar endpoint can only be rescued by excluding the low-S
        // native end, and the high scalar endpoint by excluding the high-S
        // native end. This avoids using an overlapping coarse window bound
        // at the unrelated end of an otherwise nonmonotone source curve.
        if(failure.First&&!excludeWindow(increasing,a))return false;
        if(failure.Last&&!excludeWindow(!increasing,b))return false;
        if(!(nativeLast>nativeFirst)||(nativeFirst==source.First&&nativeLast==source.Last))return false;

        Branch retry=source;retry.First=nativeFirst;retry.Last=nativeLast;
        retry.SFirst=source.Curve->Value(nativeFirst).X();
        retry.SLast=source.Curve->Value(nativeLast).X();
        if(!std::isfinite(retry.SFirst)||!std::isfinite(retry.SLast)||retry.SFirst==retry.SLast
            ||(retry.SLast>retry.SFirst)!=increasing)return false;

        const double nextA=std::max(a,std::min(retry.SFirst,retry.SLast));
        const double nextB=std::min(b,std::max(retry.SFirst,retry.SLast));
        if(!std::isfinite(nextA)||!std::isfinite(nextB)||!(nextB>nextA)
            ||(nextA==a&&nextB==b))return false;

        std::ostringstream detail;detail<<std::setprecision(17)
            <<"source-native-endpoint-inverse-one-ulp-window-excluded-for-inner-retry"
            <<";source-edge="<<source.EdgeId<<";original-native=["<<source.First<<","<<source.Last
            <<"];inner-native=["<<retry.First<<","<<retry.Last<<"];excluded-native-first="<<removedFirst
            <<";excluded-native-last="<<removedLast<<";original-source-s=["<<a<<","<<b
            <<"];inner-source-s=["<<nextA<<","<<nextB
            <<"];coordinates=unchanged-original-native-curve;excluded-window-remains-unmapped";
        auto nextReason=detail.str();
        // Commit only the retry description. No point is returned as an
        // inverse, and every false path preserves all caller output values.
        inner=std::move(retry);clippedA=nextA;clippedB=nextB;reason.swap(nextReason);
        return true;
    } catch(const Standard_Failure&) {return false;}
    catch(const std::exception&) {return false;}
}
