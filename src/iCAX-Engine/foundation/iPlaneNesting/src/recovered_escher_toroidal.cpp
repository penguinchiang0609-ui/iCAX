#include "recovered_escher_toroidal.hpp"
#include "recovered_escher_integer.hpp"
#include "recovered_escher_candidate.hpp"
#include <bit>
#include <numeric>
namespace study::recovered {
namespace {
int64_t add64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
double norm(EscherIntegerPoint p){const double y=double(p[1]),x=double(p[0]);return y*y+x*x;}
double determinant(ToroidalBasis b){return b[0][0]*b[1][1]-b[0][1]*b[1][0];}
double unsignedDouble(uint64_t value){return value>uint64_t(INT64_MAX)?double((value>>1)|(value&1))*2.:double(int64_t(value));}
int64_t integer(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int32_t integer32(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
int64_t roundedInteger(double value){return integer(value<0.?value-.5:value+.5);}
}
EscherLatticeBasis toroidalReduceIntegerBasis(EscherLatticeBasis basis){
    for(;;){
        const auto a=basis[0],b=basis[1];const auto bn=norm(b);
        if(bn<norm(a)){std::swap(basis[0],basis[1]);continue;}
        EscherIntegerPoint next{add64(b[0],a[0]),add64(b[1],a[1])};
        if(!(norm(next)<bn)){
            next={sub64(a[0],b[0]),sub64(a[1],b[1])};
            if(!(norm(next)<bn))return basis;
        }
        basis[1]=next;
    }
}
ToroidalBasis toroidalReduceBasis(ToroidalBasis basis){
    const ConvolutionPolygon points{ConvolutionRing{basis[0],basis[1]}};
    const std::array<ConvolutionPolygon,1> parts{points};
    const double scale=escherIntegerScale(parts,{},INT32_MAX,0.);
    EscherLatticeBasis source;
    for(size_t side=0;side<2;++side)for(size_t axis=0;axis<2;++axis)source[side][axis]=roundedInteger(basis[side][axis]*scale);
    const auto reduced=toroidalReduceIntegerBasis(source);
    if(reduced==source||reduced==EscherLatticeBasis{source[1],source[0]})return basis;
    const double inverse=1./scale;
    for(size_t side=0;side<2;++side)for(size_t axis=0;axis<2;++axis)basis[side][axis]=double(reduced[side][axis])*inverse;
    return basis;
}
ToroidalBasis toroidalCanonicalDirections(ToroidalBasis basis){
    basis=toroidalReduceBasis(basis);if(determinant(basis)<0.)std::swap(basis[0],basis[1]);return basis;
}
double toroidalAreaLineSearch(ToroidalBasis previous,ToroidalBasis proposed){
    const double area=determinant(previous);
    if(!(0.<=area))return 1.-toroidalAreaLineSearch({previous[1],previous[0]},{proposed[1],proposed[0]});
    const auto a=previous[0],b=previous[1];
    const ShakerPoint db{proposed[1][0]-b[0],proposed[1][1]-b[1]},da{proposed[0][0]-a[0],proposed[0][1]-a[1]};
    const double stationary=(((db[1]*a[0]-db[0]*a[1])+(da[0]*b[1]-da[1]*b[0]))*-.5)/(db[1]*da[0]-db[0]*da[1]);
    double middle=(stationary*db[1]+b[1])*(stationary*da[0]+a[0])-(stationary*db[0]+b[0])*(stationary*da[1]+a[1]);
    if(stationary<0.||1.<stationary)middle=area*10.;
    const std::array<ShakerPoint,3> candidates{{{area,0.},{middle,stationary},{determinant(proposed),1.}}};
    const auto before=[](ShakerPoint a,ShakerPoint b){if(a[0]<b[0])return true;if(a[0]==b[0])return a[1]<b[1];return false;};
    auto best=candidates.front();for(size_t n=1;n<candidates.size();++n)if(before(candidates[n],best))best=candidates[n];return best[1];
}
ShakerGeometryBox toroidalSceneProjection(const ShakerInputScene &scene,std::span<const size_t> parts,ToroidalBasis basis){
    ShakerGeometryBox result;
    for(const auto index:parts){const auto box=shakerPartProjection(scene.parts,scene.geometry,index,basis[0],basis[1]);
        if(result.empty){result=box;result.empty=false;}
        else{if(box.value[0]<result.value[0])result.value[0]=box.value[0];if(box.value[1]<result.value[1])result.value[1]=box.value[1];
            if(result.value[2]<box.value[2])result.value[2]=box.value[2];if(result.value[3]<box.value[3])result.value[3]=box.value[3];}
    }
    return result;
}
std::array<int32_t,2> toroidalLayerCounts(const ShakerInputScene &scene,ToroidalBasis basis){
    std::vector<size_t> ids(scene.parts.size());std::iota(ids.begin(),ids.end(),0);
    const auto box=toroidalSceneProjection(scene,ids,basis).value;
    return {integer32((box[2]-box[0])/std::sqrt(basis[0][1]*basis[0][1]+basis[0][0]*basis[0][0])+1.),
        integer32((box[3]-box[1])/std::sqrt(basis[1][1]*basis[1][1]+basis[1][0]*basis[1][0])+1.)};
}
ToroidalBoxes toroidalMergeBoxes(ToroidalBoxes first,const ToroidalBoxes &second){
    for(const auto &[key,box]:second){auto &target=first.at(key);
        if(target.empty){if(!box.empty)target=box;}
        else{for(size_t axis=0;axis<2;++axis){if(box.value[axis]<target.value[axis])target.value[axis]=box.value[axis];if(target.value[axis+2]<box.value[axis+2])target.value[axis+2]=box.value[axis+2];}}
    }
    return first;
}
ToroidalBoxes toroidalShiftBoxes(ToroidalBoxes boxes,std::array<int32_t,2> shift,ToroidalBasis basis){
    const ShakerPoint delta{double(shift[0])*basis[0][0]+double(shift[1])*basis[1][0],double(shift[0])*basis[0][1]+double(shift[1])*basis[1][1]};
    for(auto &[key,box]:boxes){(void)key;for(size_t axis=0;axis<2;++axis){const double a=box.value[axis+2]+delta[axis],b=box.value[axis]+delta[axis];box.value[axis]=b<=a?b:a;box.value[axis+2]=a<=b?b:a;}box.empty=false;}
    return boxes;
}
namespace {
double boxesArea(const ToroidalBoxes &boxes){double result=0.;for(const auto &[key,box]:boxes){(void)key;result+=(box.value[3]-box.value[1])*(box.value[2]-box.value[0]);}return result;}
ToroidalBoxes partBoxes(ShakerInputScene &scene,size_t part){
    shakerRefreshPart(scene.parts,scene.geometry,part);ToroidalBoxes result;const auto &input=scene.inputParts.at(part);
    require(input.shape.has_value(),PN2S_INVALID,"Toroidal wrapping requires actual component geometry");
    const auto &components=scene.geometry.parts.at(part).components;const auto &source=scene.shapes.at(*input.shape).components;
    for(size_t n=0;n<components.size();++n)result[source.at(n).type]=scene.geometry.components.at(components[n]).bounds;
    return result;
}
}
std::array<int32_t,2> toroidalWrapShift(const ToroidalBoxes &fixed,ToroidalBoxes moving,ToroidalBasis basis,const std::function<void()> &check){
    std::array<int32_t,2> total{};
    for(;;){
        auto area=boxesArea(toroidalMergeBoxes(fixed,moving));std::array<int32_t,2> shift{};
        for(int32_t a=-3;a<4;++a)for(int32_t b=-3;b<4;++b){if(check)check();const auto next=boxesArea(toroidalMergeBoxes(fixed,toroidalShiftBoxes(moving,{a,b},basis)));if(next<area){area=next;shift={a,b};}}
        if(shift==std::array<int32_t,2>{})return total;
        total[0]=std::bit_cast<int32_t>(uint32_t(total[0])+uint32_t(shift[0]));total[1]=std::bit_cast<int32_t>(uint32_t(total[1])+uint32_t(shift[1]));moving=toroidalShiftBoxes(std::move(moving),shift,basis);
    }
}
void toroidalWrapParts(ShakerInputScene &scene,ToroidalBasis basis,const std::function<void()> &check){
    require(!scene.parts.empty(),PN2S_INVALID,"Toroidal wrapping requires a nonempty actual scene");
    auto original=partBoxes(scene,0),wrapped=original;std::vector<std::array<int32_t,2>> shifts(1);
    for(size_t n=1;n<scene.parts.size();++n){auto boxes=partBoxes(scene,n);original=toroidalMergeBoxes(std::move(original),boxes);const auto shift=toroidalWrapShift(wrapped,boxes,basis,check);shifts.push_back(shift);wrapped=toroidalMergeBoxes(std::move(wrapped),toroidalShiftBoxes(std::move(boxes),shift,basis));}
    if(boxesArea(wrapped)<boxesArea(original))for(size_t n=0;n<scene.parts.size();++n){if(check)check();auto &part=scene.parts[n];const auto shift=shifts[n];part.geometryValid30=0;
        part.position[0]=double(shift[1])*basis[1][0]+double(shift[0])*basis[0][0]+part.position[0];part.position[1]=double(shift[1])*basis[1][1]+double(shift[0])*basis[0][1]+part.position[1];shakerRefreshPart(scene.parts,scene.geometry,n);}
}
std::vector<ToroidalDuplicate> toroidalDuplicateParts(ShakerInputScene &scene,size_t originals,
    ToroidalBasis basis,std::array<int32_t,2> layers,std::span<const size_t> imageShapes,const std::function<void()> &check){
    require(originals<=scene.parts.size()&&imageShapes.size()==originals&&layers[0]>=0&&layers[1]>=0,PN2S_INVALID,"Toroidal source/layer count");
    std::vector<ToroidalDuplicate> result;
    for(size_t n=0;n<originals;++n){const auto part=scene.parts[n];const auto input=scene.inputParts[n];
        require(input.shape.has_value(),PN2S_INVALID,"Toroidal image has no actual source shape");
        for(int64_t a=-int64_t(layers[0]);a<=layers[0];++a)for(int64_t b=-int64_t(layers[1]);b<=layers[1];++b){
            if(b>0||(b==0&&a>=0))continue;if(check)check();
            const ShakerPoint position{part.position[0]+basis[0][0]*double(a)+basis[1][0]*double(b),part.position[1]+basis[0][1]*double(a)+basis[1][1]*double(b)};
            const auto image=shakerCreatePart(scene,imageShapes[n],position,part.angle,part.mirror,0);
            result.push_back({n,image,int32_t(a),int32_t(b)});
        }
    }
    return result;
}
void toroidalDuplicateConstraint(const ToroidalDuplicate &duplicate,bool rotation,const ShakerProblem &problem,
    std::span<const ShakerPart> parts,LinearModel &model){
    require(!rotation,PN2S_UNSUPPORTED,"Original ToroidalShake does not support rotation");
    const auto &source=parts[duplicate.original],&image=parts[duplicate.image];
    model.addRow({{source.columns[0],1.},{image.columns[0],-1.},{problem.auxiliaryDC[0],double(duplicate.first)},{problem.auxiliaryDC[2],double(duplicate.second)}},0.,0.);
    model.addRow({{source.columns[1],1.},{image.columns[1],-1.},{problem.auxiliaryDC[1],double(duplicate.first)},{problem.auxiliaryDC[3],double(duplicate.second)}},0.,0.);
}
EscherEnumerationDescriptor escherToroidalDescriptor(std::span<const EscherPatternRecord> records,
    EscherLatticeBasis basis,int32_t restriction,const std::vector<bool> &choices,int64_t &minimum){
    require(!records.empty()&&records.size()<=2,PN2S_INVALID,"Toroidal descriptor requires its original mono/bimodule");
    auto source=escherSourceDescriptor(records);auto candidate=escherNormalizePeriodBasis(basis);
    candidate.boxes=escherCandidateBoxes(source.supports,basis[0]);candidate.offsets.resize(records.size());escherNormalizeCandidatePhase(candidate);
    const int64_t cross=std::bit_cast<int64_t>(uint64_t(basis[0][0])*uint64_t(basis[1][1])-uint64_t(basis[1][0])*uint64_t(basis[0][1]));
    const int64_t positive=cross<0?sub64(0,cross):cross;
    minimum=std::min(minimum,positive/int64_t(records.size()));
    auto result=std::move(source.descriptor);result.restrictionDegrees=restriction;result.orientationChoices.assign(choices.begin(),choices.end());
    (escherPeriodOrthogonal(basis[0],basis[1])?result.specialCandidates:result.normalCandidates).push_back(std::move(candidate));return result;
}
}
