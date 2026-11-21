#include "ordinary_predefined_validation.hpp"

namespace study {
namespace {
using Identity=std::pair<size_t,size_t>;
Point location(const Placement &p){return {std::isfinite(p.exactX)?p.exactX:double(p.x)/1e6,
                                         std::isfinite(p.exactY)?p.exactY:double(p.y)/1000.};}
Pose actualPose(const Input &input,const Placement &p){auto pose=input.parts.at(p.group).poses.at(p.poseIndex);if(std::isfinite(p.solvedAngle))pose.angle=p.solvedAngle;return pose;}
bool same(const Input &input,const Placement &a,const Placement &b){const auto x=location(a),y=location(b);const auto p=actualPose(input,a),q=actualPose(input,b);return x.x==y.x&&x.y==y.y&&p.angle==q.angle&&p.flip==q.flip;}
bool validIdentity(const Input &input,const Placement &p){return p.group<input.parts.size()&&input.parts[p.group].quantity>=0&&p.ordinal<size_t(input.parts[p.group].quantity)&&p.poseIndex<input.parts[p.group].poses.size();}
}
PredefinedContinuationValidation validatePredefinedContinuation(const Input &input,const Output &original,const Output &candidate){
    PredefinedContinuationValidation result;
    const auto fail=[&](PredefinedContinuationFailure reason){result.failure=reason;return result;};
    try{
        std::map<Identity,const Placement *> initial,final;
        for(const auto &p:original.placements){if(!validIdentity(input,p))return fail(PredefinedContinuationFailure::InvalidOriginalInstance);
            if(!initial.emplace(Identity{p.group,p.ordinal},&p).second)return fail(PredefinedContinuationFailure::DuplicateOriginalInstance);}
        std::vector<bool> unchanged;unchanged.reserve(candidate.placements.size());
        for(const auto &p:candidate.placements){if(!validIdentity(input,p))return fail(PredefinedContinuationFailure::InvalidCandidateInstance);
            const Identity id{p.group,p.ordinal};if(!final.emplace(id,&p).second)return fail(PredefinedContinuationFailure::DuplicateCandidateInstance);
            const auto found=initial.find(id);const bool retained=found!=initial.end()&&same(input,*found->second,p);unchanged.push_back(retained);
            if(retained)++result.preservedOriginal;else if(found!=initial.end())++result.movedOriginal;else ++result.added;
            if(!retained&&!valid_layout(input,Output{{p},candidate.pitchX}))return fail(PredefinedContinuationFailure::ChangedPartOutsideSheet);
        }
        for(const auto &[id,p]:initial)if(!final.contains(id))return fail(PredefinedContinuationFailure::MissingOriginalInstance);
        double minX=0,minY=0,maxX=input.width,maxY=input.height;
        if(!std::isfinite(maxX)||!std::isfinite(maxY)||maxX<=0||maxY<=0)return fail(PredefinedContinuationFailure::InvalidGeometry);
        for(const auto &p:candidate.placements){const auto position=location(p);const auto box=bounds(transform(input.parts[p.group].polygon,input.parts[p.group].pivot,actualPose(input,p)));
            if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(box.x0)||!std::isfinite(box.x1)||!std::isfinite(box.y0)||!std::isfinite(box.y1))return fail(PredefinedContinuationFailure::InvalidGeometry);
            minX=std::min(minX,position.x+box.x0);minY=std::min(minY,position.y+box.y0);maxX=std::max(maxX,position.x+box.x1);maxY=std::max(maxY,position.y+box.y1);}
        Input expanded=input;const Point shift{1.-std::floor(minX),1.-std::floor(minY)};expanded.width=std::ceil(maxX)+shift.x+1.;expanded.height=std::ceil(maxY)+shift.y+1.;
        auto translated=candidate.placements;for(auto &p:translated){const auto position=location(p);p.exactX=position.x+shift.x;p.exactY=position.y+shift.y;}
        for(size_t first=0;first<translated.size();++first)for(size_t second=first+1;second<translated.size();++second){
            if(unchanged[first]&&unchanged[second]){++result.preservedPairs;continue;}
            if(!valid_layout(expanded,Output{{translated[first],translated[second]},candidate.pitchX}))return fail(PredefinedContinuationFailure::NewMaterialCollision);
        }
        size_t required=0;for(const auto &part:input.parts){if(part.quantity<0)return fail(PredefinedContinuationFailure::InvalidGeometry);required+=size_t(part.quantity);}
        result.complete=final.size()==required;result.valid=true;return result;
    }catch(...){return fail(PredefinedContinuationFailure::InvalidGeometry);}
}
}
