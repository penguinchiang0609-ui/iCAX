#include "recovered_convolution_symmetry.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <stdexcept>
namespace study::recovered {
namespace {
int32_t wrap(uint32_t v){return std::bit_cast<int32_t>(v);}
int32_t add(int32_t a,int32_t b){return wrap(uint32_t(a)+uint32_t(b));}
int32_t neg(int32_t a){return wrap(0U-uint32_t(a));}
int32_t mul(int32_t a,int32_t b){return wrap(uint32_t(a)*uint32_t(b));}
int32_t norm(int32_t a){a%=3600000;return a<0?a+3600000:a;}
int32_t magnitude(int32_t a){const auto n=neg(a);return n<0?a:n;}
int32_t cvtt(double v){return !std::isfinite(v)||v>=2147483648.||v<-2147483648.?INT32_MIN:int32_t(v);}
int32_t rounded(double v){v=v<0?v-.5:v+.5;const auto truncated=!std::isfinite(v)||v>=9223372036854775808.||v<-9223372036854775808.?INT64_MIN:int64_t(v);return norm(wrap(uint32_t(truncated)));}
ConvolutionTransform inverse(ConvolutionTransform a){if(!a.mirror)a.angle=norm(neg(a.angle));return a;}
ConvolutionTransform compose(ConvolutionTransform a,ConvolutionTransform b){return {a.mirror!=b.mirror,norm(add(b.angle,norm(b.mirror?neg(a.angle):a.angle)))};}
ConvolutionSymmetryGroup groupOf(const ConvolutionSymmetry &value) {
    if (value.type < 1 || value.type > 360)
        throw std::invalid_argument("Convolution symmetry order must be in 1..360");
    return {value.type, value.reflectionAngle, value.flag};
}
std::vector<ConvolutionTransform> groupCandidates(const ConvolutionSymmetryGroup &group) {
    std::vector<ConvolutionTransform> result{{false, 0}};
    if (group.reflection)
        result.push_back({true, group.reflectionAngle});
    return result;
}
std::array<int32_t, 2> rational(double value, double tolerance) {
    if (value < 0) {
        auto result = rational(-value, tolerance);
        result[0] = neg(result[0]);
        return result;
    }
    const auto integer = cvtt(value);
    const double fraction = value - double(integer);
    if (fraction <= tolerance)
        return {integer, 1};
    const double reciprocal = 1.0 / fraction;
    const auto rest = rational(reciprocal, 1.0 / (fraction - tolerance) - reciprocal);
    return {add(mul(integer, rest[0]), rest[1]), rest[0]};
}
ConvolutionTransform rotation(int32_t turns, int32_t order) {
    return {false, rounded(((double(turns) * 360.0) / double(order)) * 10000.0)};
}
ConvolutionSymmetryPair reduceWithinGroup(ConvolutionTransform pose, const ConvolutionSymmetryGroup &group) {
    std::vector<ConvolutionSymmetryPair> candidates;
    for (const auto base : groupCandidates(group)) {
        const auto reduced = convolutionReduceAngle(compose(pose, base), group.order);
        candidates.push_back({reduced.pose, compose(base, rotation(reduced.turns, group.order))});
    }
    // Original insertion sort keeps the first pair when reduced poses tie.
    return *std::min_element(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return convolutionTransformLess(a[0], b[0]);
    });
}
std::array<ConvolutionSymmetryGroup, 2> commonGroups(
    const ConvolutionSymmetryGroup &left, ConvolutionTransform pose,
    const ConvolutionSymmetryGroup &right) {
    std::vector<ConvolutionTransform> first, second;
    for (const auto a : groupCandidates(left)) {
        for (const auto b : groupCandidates(right)) {
            const auto delta = compose(compose(a, pose), inverse(compose(pose, b)));
            const auto match = convolutionMatchRotations(delta, left.order, right.order);
            if (match) {
                first.push_back(compose(a, inverse((*match)[0])));
                second.push_back(compose(b, (*match)[1]));
            }
        }
    }
    if (first.empty())
        throw std::invalid_argument("Convolution symmetry common group is empty");
    return {convolutionGroupFromTransforms(first), convolutionGroupFromTransforms(second)};
}
struct ReducedGroups {
    ConvolutionTransform reduced, correction;
    ConvolutionSymmetryGroup group;
};
ReducedGroups reduceGroups(const ConvolutionSymmetryGroup &left, ConvolutionTransform pose,
                           const ConvolutionSymmetryGroup &right) {
    struct Candidate { ConvolutionTransform reduced, correction; };
    std::vector<Candidate> candidates;
    const int32_t order = std::lcm(left.order, right.order);
    for (const auto a : groupCandidates(left)) {
        for (const auto b : groupCandidates(right)) {
            const auto intermediate = compose(pose, b);
            const auto reduced = convolutionReduceAngle(compose(a, intermediate), order);
            const auto coefficients = convolutionDiophantine(order / left.order, order / right.order,
                                                             reduced.turns);
            if (!coefficients.valid)
                throw std::invalid_argument("Convolution symmetry rotation equation has no solution");
            const auto shifted = compose(intermediate, rotation(coefficients.first, left.order));
            const auto correction = compose(a, compose(shifted, inverse(intermediate)));
            candidates.push_back({reduced.pose, correction});
        }
    }
    const auto best = std::min_element(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return convolutionTransformLess(a.reduced, b.reduced);
    });
    const auto common = commonGroups(left, best->reduced, right);
    return {best->reduced, best->correction, common[0]};
}
std::optional<ConvolutionSymmetryPair> equivalentTransforms(
    ConvolutionTransform first, ConvolutionTransform second,
    const ConvolutionSymmetryGroup &left, const ConvolutionSymmetryGroup &right) {
    for (const auto a : groupCandidates(left)) {
        for (const auto b : groupCandidates(right)) {
            const auto delta = compose(compose(compose(inverse(b), inverse(second)), a), first);
            const auto match = convolutionMatchRotations(delta, right.order, left.order);
            if (match)
                return ConvolutionSymmetryPair{
                    compose(first, compose(inverse((*match)[1]), inverse(first))), (*match)[0]};
        }
    }
    return {};
}
}
bool convolutionTransformLess(ConvolutionTransform a,ConvolutionTransform b){if(a.mirror!=b.mirror)return a.mirror<b.mirror;const auto x=magnitude(a.angle),y=magnitude(b.angle);return x!=y?x<y:a.angle<b.angle;}
ConvolutionReducedAngle convolutionReduceAngle(ConvolutionTransform pose,int32_t order){
    if(order<=0)throw std::invalid_argument("Convolution symmetry order must be positive");
    const double step=360./order;const auto quotient=cvtt((pose.angle/10000.)/step);
    const int32_t lower=rounded((double(quotient)*step)*10000.),upper=rounded((double(add(quotient,1))*step)*10000.);
    const ConvolutionTransform a{pose.mirror,norm(add(norm(neg(lower)),norm(pose.angle)))},b{pose.mirror,norm(add(norm(neg(upper)),norm(pose.angle)))};
    auto turns=add(mul(order,2),neg(quotient));if(convolutionTransformLess(a,b))return {a,turns%order};
    return {b,add(turns,-1)%order};
}
ConvolutionDiophantine convolutionDiophantine(int32_t a,int32_t b,int32_t c){
    if(a<=0||b<=0||c<0)throw std::invalid_argument("Convolution Diophantine domain");
    if(a<b){auto r=convolutionDiophantine(b,a,c);std::swap(r.first,r.second);return r;}
    if(c==0)return {0,0,true};
    if(c<b){if(a==b)return {};auto r=convolutionDiophantine(b,a%b,c);if(r.valid){std::swap(r.first,r.second);r.second=add(r.second,neg(mul(a/b,r.first)));}return r;}
    auto r=convolutionDiophantine(a,b,c%b);if(r.valid)r.second=add(r.second,c/b);return r;
}
ConvolutionSymmetryPair convolutionCanonicalPair(const ConvolutionSymmetry &a,const ConvolutionSymmetry &b){
    const auto firstGroup = groupOf(a), secondGroup = groupOf(b);
    const ConvolutionTransform first{a.mirror,a.angle}, second{b.mirror,b.angle};
    const auto reduced = reduceGroups(secondGroup, compose(inverse(first), second), firstGroup);
    const auto canonical = reduceWithinGroup(compose(first, inverse(reduced.correction)), reduced.group);
    return {canonical[0], reduced.reduced};
}
std::vector<ConvolutionSymmetryPair> convolutionSymmetryCandidates(const ConvolutionSymmetry &a,const ConvolutionSymmetry &b){
    auto pair=convolutionCanonicalPair(a,b);std::vector<ConvolutionSymmetryPair> result{pair};
    auto reversed=pair[1];reversed.angle=norm(neg(reversed.angle));
    const auto equivalent = equivalentTransforms(inverse(reversed), inverse(pair[1]), groupOf(a), groupOf(b));
    if (equivalent)
        result.push_back({compose(pair[0], (*equivalent)[1]), reversed});
    return result;
}
ConvolutionSymmetryGroup convolutionGroupFromTransforms(std::span<const ConvolutionTransform> transforms) {
    std::vector<int32_t> rotations, reflections;
    for (const auto pose : transforms)
        (pose.mirror ? reflections : rotations).push_back(pose.angle);
    for (const auto first : reflections)
        for (const auto second : reflections)
            rotations.push_back(norm(add(first, neg(second))));
    std::vector<int32_t> orders;
    for (const auto angle : rotations) {
        const double half = double(angle) / 1800000.0;
        const auto fraction = rational(half + half, 0.022222222222222223);
        orders.push_back(fraction[1]);
    }
    int32_t order = 1;
    for (auto current = orders.rbegin(); current != orders.rend(); ++current)
        order = mul(order, *current) / std::gcd(order, *current);
    if (order < 1 || order > 360)
        throw std::invalid_argument("Generated convolution symmetry order is outside 1..360");
    return {order, reflections.empty() ? 0 : reflections.front(), !reflections.empty()};
}
std::optional<ConvolutionSymmetryPair> convolutionMatchRotations(
    ConvolutionTransform pose, int32_t firstOrder, int32_t secondOrder, double tolerance) {
    if (firstOrder < 1 || secondOrder < 1)
        throw std::invalid_argument("Convolution symmetry matching requires positive orders");
    if (pose.mirror)
        return {};
    const int32_t order = mul(firstOrder, secondOrder) / std::gcd(firstOrder, secondOrder);
    const double ratio = (double(pose.angle) / 10000.0) / (360.0 / double(order));
    const int32_t nearest = cvtt(std::round(ratio));
    if (std::abs(ratio - double(nearest)) > tolerance)
        return {};
    const auto coefficients = convolutionDiophantine(order / firstOrder, order / secondOrder, nearest);
    if (!coefficients.valid)
        return {};
    return ConvolutionSymmetryPair{rotation(coefficients.first, firstOrder),
                                  rotation(coefficients.second, secondOrder)};
}
std::optional<ConvolutionSymmetryCache::Hit> ConvolutionSymmetryCache::ordered(const ConvolutionSymmetry &a,const ConvolutionSymmetry &b,const Key &key)const{
    // 867380 passes its group arguments to 866220 in reverse order while
    // preserving pose order. This matters for distinct intrinsic groups.
    auto first = a, second = b;
    std::swap(first.type, second.type);
    std::swap(first.flag, second.flag);
    std::swap(first.reflectionAngle, second.reflectionAngle);
    const auto candidates=convolutionSymmetryCandidates(first,second);const auto found=entries_.find(key);if(found==entries_.end())return {};
    for(const auto &candidate:candidates){const auto pose=candidate[1];
        const double lo=std::clamp(pose.angle/10000.-.0002,0.,360.),hi=std::clamp(pose.angle/10000.+.0002,0.,360.);
        const int32_t low=rounded(lo*10000.);int32_t high=rounded(hi*10000.);
        if(high<low){if(high!=0||hi<=359.99)throw std::logic_error("Convolution symmetry angle window");high=3599999;}
        const auto end=found->second.upper_bound({pose.mirror,high});
        for(auto i=found->second.lower_bound({pose.mirror,low});i!=end;++i){const auto delta=compose(pose,inverse(i->first));
            if(i->first.mirror==pose.mirror&&magnitude(delta.angle)<3)return Hit{candidate[0],i->second};}
    }return {};
}
std::optional<ConvolutionSymmetryCache::Hit> ConvolutionSymmetryCache::get(const ConvolutionSymmetry &a,const ConvolutionSymmetry &b,Geometry ga,Geometry gb)const{
    // 721e40 stores the signed numeric geometry ID in the shared key; the
    // allocation address of either the geometry or its control block is not
    // part of the hash key used by 867380.
    std::shared_lock lock(mutex_);if(auto hit=ordered(a,b,{ga->key,gb->key}))return hit;return ordered(b,a,{gb->key,ga->key});
}
void ConvolutionSymmetryCache::put(ConvolutionTransform pose,Polygon polygon,Geometry a,Geometry b){std::unique_lock lock(mutex_);entries_[{a->key,b->key}].try_emplace(pose,std::move(polygon));}
size_t ConvolutionSymmetryCache::size()const{std::shared_lock lock(mutex_);size_t n=0;for(const auto &entry:entries_)n+=entry.second.size();return n;}
ConvolutionPolygon convolutionTransformCachedPolygon(const ConvolutionPolygon &source,ConvolutionTransform pose){
    auto result=source;for(auto &ring:result)convolutionScalePolygon(ring,1.);for(auto &ring:result)convolutionRotatePolygon(ring,pose.angle);
    if(pose.mirror)for(auto &ring:result){for(auto &point:ring)point[1]*=-1.;std::reverse(ring.begin(),ring.end());}
    for(auto &ring:result)for(auto &point:ring){point[0]+=0.;point[1]+=0.;}return result;
}
}
