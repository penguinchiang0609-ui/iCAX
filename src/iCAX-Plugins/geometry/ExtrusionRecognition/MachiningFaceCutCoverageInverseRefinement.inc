// Included after the original spline point/uniqueness helpers. This refines
// a native inverse only after independent uniqueness and existence proofs.
// Kernel candidates are seeds, never permission to expand a native domain.
namespace unique_native_inverse_refinement
{
bool PointSign(const CurveUse& use,double parameter,double s,
    double& lower,double& upper,int& sign,std::string& reason)
{
    if(!BoundNativeSplineProjectionPoint(use,parameter,lower,upper,reason)
        ||!std::isfinite(lower)||!std::isfinite(upper)||upper<lower)return false;
    sign=upper<s?-1:lower>s?1:0;
    return true;
}

bool NativeD1(const CurveUse& use,double parameter,double s,
    double& residual,double& derivative)
{
    try {
        gp_Pnt2d point;gp_Vec2d tangent;use.Curve->D1(parameter,point,tangent);
        residual=point.X()-s;derivative=tangent.X();
        return std::isfinite(residual)&&std::isfinite(derivative);
    } catch(const Standard_Failure&) {return false;}
    catch(const std::exception&) {return false;}
}

bool InteriorMiddle(double first,double last,double& middle)
{
    // No tolerance or endpoint snapping. Adjacent doubles cannot supply an
    // admissible interior inverse, even if the real root lies between them.
    const double adjacent=std::nextafter(first,last);
    if(!std::isfinite(adjacent)||!(adjacent>first&&adjacent<last))return false;
    double result=first+(last-first)*.5;
    if(!std::isfinite(result))result=first*.5+last*.5;
    if(!(result>first&&result<last))result=adjacent;
    if(!std::isfinite(result)||!(result>first&&result<last))return false;
    middle=result;return true;
}

bool NewtonTrial(const CurveUse& use,double seed,double s,
    double first,double last,double& trial)
{
    double residual=0.,derivative=0.;
    if(!NativeD1(use,seed,s,residual,derivative)||derivative==0.)return false;
    const double step=residual/derivative,candidate=seed-step;
    if(!std::isfinite(step)||!std::isfinite(candidate)||candidate==seed
        ||!(candidate>first&&candidate<last))return false;
    trial=candidate;return true;
}
}

bool RefineUniqueOriginalNativeSplineProjectionRoot(const CurveUse& use,double s,
    const std::vector<double>& kernelSeeds,double& parameter,std::string& reason,
    NativeProjectionInverseFailureWindow* failureWindow=nullptr)
{
    if(failureWindow)*failureWindow={};
    try {
        if(use.Curve.IsNull()||!std::isfinite(use.First)||!std::isfinite(use.Last)
            ||!(use.Last>use.First)||!std::isfinite(s)) {
            reason="native-unique-spline-inverse-refinement-has-invalid-original-domain-or-request";return false;
        }
        std::vector<double> seeds;
        for(const auto seed:kernelSeeds)
            if(std::isfinite(seed)&&seed>=use.First&&seed<=use.Last)seeds.push_back(seed);
        if(seeds.empty()) {
            reason="native-unique-spline-inverse-refinement-requires-an-original-in-range-kernel-seed";return false;
        }
        double firstLo=0.,firstHi=0.,lastLo=0.,lastHi=0.;int firstSign=0,lastSign=0;
        std::string pointReason;
        if(!unique_native_inverse_refinement::PointSign(use,use.First,s,firstLo,firstHi,firstSign,pointReason)
            ||!unique_native_inverse_refinement::PointSign(use,use.Last,s,lastLo,lastHi,lastSign,pointReason)
            ||!firstSign||lastSign!=-firstSign) {
            reason="native-unique-spline-inverse-refinement-original-endpoint-intervals-are-not-strictly-opposite";return false;
        }
        int direction=0;std::string proofReason;
        const bool monotone=CertifyStrictOriginalPolynomialSplineProjection(use,direction,proofReason)
            ||CertifyStrictOriginalPositiveRationalSplineProjection(use,direction,proofReason);
        if(!(monotone&&direction==-firstSign)
            &&!CertifyUniqueOriginalNativeSplineProjectionRoot(use,s,proofReason)) {
            reason="native-unique-spline-inverse-refinement-requires-independent-original-root-uniqueness";return false;
        }

        double first=use.First,last=use.Last,bestSeed=seeds.front();
        double bestResidual=std::numeric_limits<double>::infinity();
        const auto commit=[&](double value,double lo,double hi,std::size_t iteration) {
            if(!(value>use.First&&value<use.Last)||s<lo||s>hi)return false;
            std::ostringstream detail;detail<<std::setprecision(17)
                <<"native-unique-spline-inverse-refined-inside-original-domain-by-D1-newton-and-outward-interval-sign-bisection"
                <<";edge="<<use.EdgeId<<";original-native=["<<use.First<<","<<use.Last
                <<"];inverse-native="<<value<<";point-projection=["<<lo<<","<<hi
                <<"];requested-s="<<s<<";iterations="<<iteration;
            auto completedReason=detail.str();reason.swap(completedReason);parameter=value;return true;
        };
        // Retain only signs proved by original homogeneous point enclosures.
        // D1 evaluations below suggest trial parameters; their rounded value
        // or residual alone can never establish a sign or accept an inverse.
        for(const auto seed:seeds) {
            double lo=0.,hi=0.;int sign=0;
            if(unique_native_inverse_refinement::PointSign(use,seed,s,lo,hi,sign,pointReason)) {
                if(!sign&&commit(seed,lo,hi,0))return true;
                if(seed>first&&seed<last) {
                    if(sign==firstSign)first=seed;
                    else if(sign==lastSign)last=seed;
                }
            }
            double residual=0.,derivative=0.;
            if(unique_native_inverse_refinement::NativeD1(use,seed,s,residual,derivative)
                &&std::abs(residual)<bestResidual) {
                bestResidual=std::abs(residual);bestSeed=seed;
            }
        }

        constexpr std::size_t maximumIterations=256;
        double previous=bestSeed;
        for(std::size_t iteration=0;iteration<maximumIterations;++iteration) {
            double trial=0.;
            // At least every second pass bisects the CERTIFIED sign bracket.
            // Thus even a poor/zero native derivative cannot prevent bracket
            // contraction; no geometric or parameter tolerance is introduced.
            const bool newton=iteration%2==0
                &&unique_native_inverse_refinement::NewtonTrial(use,previous,s,first,last,trial);
            if(!newton&&!unique_native_inverse_refinement::InteriorMiddle(first,last,trial)) {
                if(failureWindow&&std::nextafter(first,last)==last)
                    EncloseOriginalTargetInverseFailureWindow(use,first,last,s,
                        "certified-unique-native-root-between-original-adjacent-double-parameters",*failureWindow);
                reason="native-unique-spline-inverse-refinement-has-no-representable-interior-parameter";return false;
            }
            if(!(trial>first&&trial<last)) {
                reason="native-unique-spline-inverse-refinement-trial-is-not-strictly-interior";return false;
            }
            double lo=0.,hi=0.;int sign=0;
            if(!unique_native_inverse_refinement::PointSign(use,trial,s,lo,hi,sign,pointReason)) {
                reason="native-unique-spline-inverse-refinement-original-point-enclosure-failed";return false;
            }
            if(!sign&&commit(trial,lo,hi,iteration+1))return true;
            if(sign==firstSign)first=trial;
            else if(sign==lastSign)last=trial;
            else {reason="native-unique-spline-inverse-refinement-point-sign-is-unproved";return false;}
            previous=trial;
        }
        reason="native-unique-spline-inverse-refinement-exhausted-iterations-with-positive-native-bracket";
    } catch(const Standard_Failure&) {
        reason="native-unique-spline-inverse-refinement-original-curve-query-failed";
    } catch(const std::exception&) {
        reason="native-unique-spline-inverse-refinement-original-data-query-failed";
    }
    return false;
}
