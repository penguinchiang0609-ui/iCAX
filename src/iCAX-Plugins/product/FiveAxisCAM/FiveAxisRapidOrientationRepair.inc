// Worker-only direction search. Positions and both endpoint directions are
// immutable; the owner supplies the actual transport and collision predicates.
namespace {
namespace RapidOrientationRepair {
    using Point=::iCAX::GeometryServices::TravelPoint;
    using Pose=::iCAX::GeometryServices::TravelPose;
    using DirectionField=std::vector<Point>;
    namespace Math=::iCAX::GeometryServices::RigidMotion;
    struct Result {
        std::optional<DirectionField> Directions;
        std::size_t Candidates=0;
        bool TimedOut=false;
    };
    inline Result Search(const std::vector<Pose>& original,std::size_t selectedPoint,
        const std::function<std::optional<double>(const DirectionField&)>& closureError,
        const std::function<bool(const DirectionField&)>& accept,
        const std::function<bool()>& cancelled,
        const std::function<void(std::size_t)>& progress={})
    {
        Result output;if(original.size()<3||selectedPoint==0||selectedPoint+1>=original.size())return output;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
        const auto stopped=[&]{if(cancelled&&cancelled())return true;if(std::chrono::steady_clock::now()>=deadline){output.TimedOut=true;return true;}return false;};
        if(stopped())return output;
        std::vector<double> distances(original.size());std::vector<Point> axes;axes.reserve(original.size());
        for(std::size_t i=0;i<original.size();++i){
            for(const auto value:original[i].Point)if(!std::isfinite(value))throw std::invalid_argument("CAM_RAPID_ORIENTATION_POINT_INVALID");
            axes.push_back(Math::Unit(original[i].Direction));
            if(i)distances[i]=distances[i-1]+Math::Length(Math::Sub(original[i].Point,original[i-1].Point));
        }
        // An arclength profile avoids discontinuous weights on unevenly
        // spaced geometry. Coincident stations use the same smooth index rule.
        const bool indexed=distances[selectedPoint]<1e-12||distances.back()-distances[selectedPoint]<1e-12;
        if(indexed)for(std::size_t i=0;i<distances.size();++i)distances[i]=static_cast<double>(i);
        const auto total=distances.back(),peak=distances[selectedPoint];std::vector<double> weights(original.size());
        for(std::size_t i=1;i+1<original.size();++i){const auto t=std::clamp(i<=selectedPoint?distances[i]/peak:(total-distances[i])/(total-peak),0.,1.);weights[i]=t*t*(3-2*t);}
        const auto axis=axes[selectedPoint];int smallest=0;for(int i=1;i<3;++i)if(std::abs(axis[i])<std::abs(axis[smallest]))smallest=i;
        Point basis{};basis[smallest]=1.;const auto u=Math::Unit(Math::Sub(basis,Math::Scale(axis,Math::Dot(axis,basis)))),v=Math::Cross(axis,u);
        struct Trial { DirectionField Directions;double Error=0; };
        const auto trial=[&](double angle,double azimuth)->std::optional<Trial>{
            if(stopped())return {};Trial candidate;candidate.Directions.reserve(original.size());
            const auto tangent=Math::Add(Math::Scale(u,std::cos(azimuth)),Math::Scale(v,std::sin(azimuth)));
            const auto rotationAxis=Math::Unit(Math::Cross(axis,tangent));
            for(std::size_t i=0;i<original.size();++i){
                if(weights[i]==0){candidate.Directions.push_back(original[i].Direction);continue;}
                const auto half=.5*angle*weights[i],s=std::sin(half);const Math::Quaternion rotation{rotationAxis[0]*s,rotationAxis[1]*s,rotationAxis[2]*s,std::cos(half)};
                candidate.Directions.push_back(Math::Unit(Math::Rotate(rotation,axes[i])));
            }
            ++output.Candidates;if(progress)progress(output.Candidates);if(stopped())return {};
            const auto error=closureError(candidate.Directions);if(!error||!std::isfinite(*error)||stopped())return {};candidate.Error=*error;return candidate;
        };
        const auto acceptClosed=[&](const std::optional<Trial>& candidate){
            if(!candidate||std::abs(candidate->Error)>1e-14||stopped())return false;
            if(!accept(candidate->Directions)||stopped())return false;output.Directions=candidate->Directions;return true;
        };
        constexpr double pi=3.14159265358979323846;
        for(const auto degrees:{2.,5.,10.,15.,30.,45.,60.,90.,120.,150.,170.}){
            if(stopped())break;const auto angle=degrees*pi/180.;double previousAzimuth=0;auto previous=trial(angle,0);if(acceptClosed(previous))return output;
            for(unsigned i=1;i<=8&&!stopped();++i){
                const auto azimuth=i*pi/4.;auto next=trial(angle,azimuth);if(acceptClosed(next))return output;
                if(previous&&next&&previous->Error*next->Error<0){
                    double low=previousAzimuth,high=azimuth,lowError=previous->Error;
                    for(unsigned iteration=0;iteration<48&&!stopped();++iteration){
                        const auto middle=(low+high)/2.;auto candidate=trial(angle,middle);if(!candidate)break;if(acceptClosed(candidate))return output;
                        if(candidate->Error*lowError<=0)high=middle;else{low=middle;lowError=candidate->Error;}
                    }
                }
                previous=std::move(next);previousAzimuth=azimuth;
            }
        }
        return output;
    }
}
}
