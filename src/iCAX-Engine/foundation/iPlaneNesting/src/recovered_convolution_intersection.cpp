#include "recovered_convolution_integer.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <set>
#include <functional>
namespace study::recovered {
namespace {
// The original fallback uses arbitrary-precision rational arithmetic. For this
// entry, differences need 65 bits and the four triple products need at most
// 196 bits. Eight 32-bit limbs cover every int64 input without external headers.
struct Wide {
    std::array<uint32_t,8> words{};bool negative=false;
    Wide()=default;explicit Wide(int64_t value){negative=value<0;const auto bits=negative?uint64_t(0)-uint64_t(value):uint64_t(value);words[0]=uint32_t(bits);words[1]=uint32_t(bits>>32);}
    bool zero()const{return std::all_of(words.begin(),words.end(),[](auto x){return x==0;});}
    void normalize(){if(zero())negative=false;}
};
int magnitudeCompare(const Wide &a,const Wide &b){for(size_t n=8;n--;)if(a.words[n]!=b.words[n])return a.words[n]<b.words[n]?-1:1;return 0;}
Wide magnitudeSubtract(Wide a,const Wide &b){uint64_t borrow=0;for(size_t n=0;n<8;++n){const uint64_t rhs=uint64_t(b.words[n])+borrow;const uint64_t lhs=a.words[n];a.words[n]=uint32_t(lhs-rhs);borrow=lhs<rhs;}a.normalize();return a;}
Wide operator-(Wide a){if(!a.zero())a.negative=!a.negative;return a;}
Wide operator+(const Wide &a,const Wide &b){
    if(a.negative!=b.negative)return magnitudeCompare(a,b)>=0?magnitudeSubtract(a,b):magnitudeSubtract(b,a);
    Wide result;result.negative=a.negative;uint64_t carry=0;for(size_t n=0;n<8;++n){const auto word=uint64_t(a.words[n])+b.words[n]+carry;result.words[n]=uint32_t(word);carry=word>>32;}if(carry)throw std::overflow_error("Intersection wide addition");return result;
}
Wide operator-(const Wide &a,const Wide &b){return a+(-b);}
Wide operator*(const Wide &a,const Wide &b){
    Wide result;result.negative=a.negative!=b.negative;
    for(size_t i=0;i<8;++i){uint64_t carry=0;for(size_t j=0;j<8-i;++j){const uint64_t value=uint64_t(a.words[i])*b.words[j]+result.words[i+j]+carry;result.words[i+j]=uint32_t(value);carry=value>>32;}if(carry)throw std::overflow_error("Intersection wide multiplication");for(size_t j=8-i;j<8;++j)if(a.words[i]&&b.words[j])throw std::overflow_error("Intersection wide multiplication");}result.normalize();return result;
}
int compare(const Wide &a,const Wide &b){return a.negative!=b.negative?(a.negative?-1:1):(a.negative?-1:1)*magnitudeCompare(a,b);}
int64_t rationalFloor(Wide numerator,Wide denominator){
    if(denominator.zero())throw std::invalid_argument("Intersection zero denominator");if(denominator.negative){denominator=-denominator;numerator=-numerator;}
    Wide quotient,remainder;for(size_t bit=256;bit--;){uint32_t carry=(numerator.words[bit/32]>>(bit%32))&1;for(auto &word:remainder.words){const auto next=word>>31;word=(word<<1)|carry;carry=next;}if(magnitudeCompare(remainder,denominator)>=0){remainder=magnitudeSubtract(remainder,denominator);quotient.words[bit/32]|=uint32_t(1)<<(bit%32);}}
    const auto limit=Wide(numerator.negative?INT64_MIN:INT64_MAX);int64_t truncated;
    if(magnitudeCompare(quotient,limit)>0)truncated=numerator.negative?INT64_MIN:INT64_MAX;
    else {uint64_t value=uint64_t(quotient.words[0])|(uint64_t(quotient.words[1])<<32);if(numerator.negative)value=uint64_t(0)-value;truncated=std::bit_cast<int64_t>(value);}
    // Preserve the original saturating conversion followed by a wrapped -1.
    if(compare(numerator,Wide(truncated)*denominator)<0)truncated=std::bit_cast<int64_t>(uint64_t(truncated)-1);return truncated;
}
bool inside(ConvolutionIntegerPoint p,const ConvolutionIntegerSegment &s){for(size_t n=0;n<2;++n)if(p[n]<std::min(s[0][n],s[1][n])||p[n]>std::max(s[0][n],s[1][n]))return false;return true;}
}
bool convolutionIntegerExactIntersection(ConvolutionIntegerPoint &out,const ConvolutionIntegerSegment &a,const ConvolutionIntegerSegment &b){
    if(!convolutionIntegerSegmentsIntersect(a,b))return false;
    for(size_t n=0;n<2;++n)if(std::min(a[0][n],a[1][n])>std::max(b[0][n],b[1][n])||std::min(b[0][n],b[1][n])>std::max(a[0][n],a[1][n]))return false;
    const auto vertical=[&](const auto &v,const auto &s){if(s[0][0]==s[1][0])return false;const auto dx=Wide(s[1][0])-Wide(s[0][0]);const auto y=rationalFloor(Wide(s[0][1])*dx+(Wide(s[1][1])-Wide(s[0][1]))*(Wide(v[0][0])-Wide(s[0][0])),dx);if(y<std::min(v[0][1],v[1][1])||y>std::max(v[0][1],v[1][1]))return false;out={v[0][0],y};return true;};
    if(a[0][0]==a[1][0])return vertical(a,b);if(b[0][0]==b[1][0])return vertical(b,a);
    const auto ax=Wide(a[1][0])-Wide(a[0][0]),ay=Wide(a[1][1])-Wide(a[0][1]),bx=Wide(b[1][0])-Wide(b[0][0]),by=Wide(b[1][1])-Wide(b[0][1]);
    const auto denominator=bx*ay-ax*by;if(denominator.zero())return false;
    const auto x=rationalFloor(Wide(a[0][0])*ay*bx-Wide(b[0][0])*by*ax+Wide(b[0][1])*ax*bx-Wide(a[0][1])*ax*bx,denominator);
    auto y=rationalFloor(Wide(a[0][1])*ax*by-Wide(b[0][1])*bx*ay+Wide(b[0][0])*ay*by-Wide(a[0][0])*ay*by,-denominator);
    if(a[0][1]==a[1][1])y=a[0][1];if(b[0][1]==b[1][1])y=b[0][1];const ConvolutionIntegerPoint result{x,y};if(!inside(result,a)||!inside(result,b))return false;out=result;return true;
}
std::vector<ConvolutionIntegerSplitSegment> convolutionSplitIntegerSegments(std::span<const ConvolutionIntegerSegment> input){
    if(input.empty())return {};if(input.size()>INT32_MAX)throw std::length_error("Integer split source IDs exceed int32");
    struct YEvent {int64_t y,active,ended;};std::vector<std::array<int64_t,2>> events;
    for(auto s:input){const int64_t delta=s[0][1]<s[1][1]?1:-1;events.push_back({s[0][1],delta});events.push_back({s[1][1],-delta});}
    std::sort(events.begin(),events.end());std::vector<YEvent> counts;int64_t active=0,ended=0;
    for(auto event:events){if(counts.empty()||counts.back().y!=event[0])counts.push_back({event[0],active,ended});active+=event[1];if(event[1]<0)ended-=event[1];counts.back()={event[0],active,ended};}
    std::vector<int64_t> cuts;
    std::function<void(size_t,size_t,uint64_t)> partition=[&](size_t first,size_t last,uint64_t count){
        while(first!=last&&count>29){const auto width=last-first,third=width/3;size_t chosen=first;uint64_t minimum=count;
            for(size_t n=first;n<last;++n){const auto offset=n-first;if(offset<third)continue;if(width-offset<third)break;if(uint64_t(counts[n].active)<minimum){chosen=n;minimum=uint64_t(counts[n].active);}}
            if(chosen==first||count/9<uint64_t(counts[chosen].active))return;
            partition(first,chosen,uint64_t(counts[chosen].active)+uint64_t(counts[chosen].ended));cuts.push_back(counts[chosen].y);count-=uint64_t(counts[chosen].ended);first=chosen;
        }
    };partition(0,counts.size(),input.size());
    std::vector<int64_t> keys{counts.front().y};keys.insert(keys.end(),cuts.begin(),cuts.end());std::vector<std::vector<ConvolutionIntegerSplitSegment>> buckets(keys.size());
    for(size_t n=0;n<input.size();++n){const auto &s=input[n];size_t first=std::lower_bound(keys.begin(),keys.end(),std::min(s[0][1],s[1][1]))-keys.begin();if(first) --first;const auto last=size_t(std::upper_bound(keys.begin(),keys.end(),std::max(s[0][1],s[1][1]))-keys.begin());for(size_t j=first;j<last;++j)buckets[j].push_back({int64_t(n),s});}
    std::vector<std::set<ConvolutionIntegerPoint>> points(input.size());
    for(size_t bucket=0;bucket<buckets.size();++bucket){auto &segments=buckets[bucket];for(auto &s:segments)if(s.segment[1]<s.segment[0])std::swap(s.segment[0],s.segment[1]);
        std::sort(segments.begin(),segments.end(),[](const auto&a,const auto&b){return a.segment!=b.segment?a.segment<b.segment:a.source<b.source;});
        const auto floor=bucket?keys[bucket]:INT64_MIN;ConvolutionIntegerRing candidates;
        for(size_t i=0;i<segments.size();++i){const auto &a=segments[i];candidates.insert(candidates.end(),a.segment.begin(),a.segment.end());for(auto p:points[size_t(a.source)])if(p[1]>=floor)candidates.push_back(p);
            const bool allAbove=a.segment[0][1]>=floor&&a.segment[1][1]>=floor;
            for(size_t j=i;j<segments.size();++j){const auto &b=segments[j];if(!allAbove&&(b.segment[0][1]<floor||b.segment[1][1]<floor))continue;if(a.segment==b.segment)continue;
                if(a.segment[1][0]<=b.segment[0][0])break;
                if(a.segment[0]==b.segment[0]||a.segment[1]==b.segment[1]||!convolutionIntegerSegmentsIntersect(a.segment,b.segment))continue;
                ConvolutionIntegerPoint p;const auto approximate=convolutionIntegerApproximateIntersection(p,a.segment,b.segment);
                if((!approximate||!convolutionIntegerPointInSegmentCell(p,a.segment)||!convolutionIntegerPointInSegmentCell(p,b.segment))&&!convolutionIntegerExactIntersection(p,a.segment,b.segment))continue;
                candidates.push_back(p);points[size_t(a.source)].insert(p);points[size_t(b.source)].insert(p);
            }
        }
        std::sort(candidates.begin(),candidates.end());candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());size_t cursor=0;
        for(const auto &s:segments){while(cursor<candidates.size()&&candidates[cursor][0]<s.segment[0][0])++cursor;for(size_t n=cursor;n<candidates.size()&&candidates[n][0]<=s.segment[1][0];++n)if(convolutionIntegerPointInSegmentCell(candidates[n],s.segment))points[size_t(s.source)].insert(candidates[n]);}
    }
    std::vector<ConvolutionIntegerSplitSegment> output;
    for(size_t n=0;n<input.size();++n){ConvolutionIntegerRing ordered(points[n].begin(),points[n].end());ConvolutionIntegerPoint delta;for(size_t axis=0;axis<2;++axis)delta[axis]=std::bit_cast<int64_t>(uint64_t(input[n][1][axis])-uint64_t(input[n][0][axis]));
        if(delta[0]!=0&&convolutionIntegerDirectionLess(delta,{1,0}))std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a[0]!=b[0]?a[0]<b[0]:a[1]>b[1];});
        for(size_t k=1;k<ordered.size();++k)output.push_back({int64_t(n),{ordered[k-1],ordered[k]}});
    }return output;
}
std::vector<ConvolutionIntegerSegment> convolutionSplitDirectedIntegerSegments(std::span<const ConvolutionIntegerSegment> input){
    std::vector<ConvolutionIntegerSegment> out;for(auto split:convolutionSplitIntegerSegments(input)){uint64_t dot=0;const auto &source=input[size_t(split.source)];for(size_t axis=0;axis<2;++axis)dot+=(uint64_t(source[1][axis])-uint64_t(source[0][axis]))*(uint64_t(split.segment[1][axis])-uint64_t(split.segment[0][axis]));if(std::bit_cast<int64_t>(dot)<0)std::swap(split.segment[0],split.segment[1]);out.push_back(split.segment);}return out;
}
}
