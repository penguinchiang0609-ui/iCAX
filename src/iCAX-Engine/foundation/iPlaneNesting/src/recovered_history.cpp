#include "recovered_history.hpp"
#include "recovered_history_initialization.hpp"
#include <bit>
namespace study::recovered {
HistoryReferenceRectangle historyReferenceRectangle(double width, double height, int32_t identifier) {
    const double low = -height * .5, high = height * .5;
    return {width, height, identifier, {{0., low}, {width, low}, {width, high}, {0., high}}};
}
HistoryReferenceRectangle fixedHistoryReference(Point mean, int32_t identifier) {
    return historyReferenceRectangle(1.5 * (mean.x * .25), .5 * (mean.y * .25), identifier);
}
std::vector<HistoryReferenceRectangle> dynamicHistoryReferences(double height, double sheetHeight,
    int32_t count, int32_t firstIdentifier) {
    std::vector<HistoryReferenceRectangle> result;
    const double low = height * .1;
    const double step = (height * 4. - low) / double(count);
    const double width = sheetHeight * .1;
    for (int32_t index = 0; index < count; ++index) {
        const double itemHeight = (double(index + 1) * step + low) * .5 + (double(index) * step + low);
        const auto identifier = std::bit_cast<int32_t>(uint32_t(firstIdentifier) + uint32_t(index));
        result.push_back(historyReferenceRectangle(width, itemHeight, identifier));
    }
    return result;
}
HistoryStatistics initialHistoryStatistics(size_t count) {
    HistoryStatistics result;
    result.samples.resize(count);
    result.estimates.resize(count, std::numeric_limits<double>::max() * .5);
    result.attempts.resize(count);
    result.weights.resize(count, 1.);
    result.best = std::numeric_limits<double>::max();
    result.elite = 1;
    return result;
}
namespace {
// ADD(SD/PD) retains the destination NaN when both operands are NaNs.
// C++ permits exchanging the operands of + even with /fp:precise. Preserve
// the original payload/sign explicitly before performing finite arithmetic.
double orderedAdd(double destination,double source) {
    const auto quiet=[](double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)|0x0008000000000000ull);};
    if(std::isnan(destination))return quiet(destination);
    if(std::isnan(source))return quiet(source);
    return destination+source;
}
void insertion(std::span<int32_t> order,std::span<const double> values) {
    for(size_t i=1;i<order.size();++i) {
        const auto key=order[i];size_t j=i;
        if(values[key]>values[order.front()]) {std::move_backward(order.begin(),order.begin()+i,order.begin()+i+1);order[0]=key;continue;}
        while(j && values[key]>values[order[j-1]]){order[j]=order[j-1];--j;}order[j]=key;
    }
}
void bufferedSort(std::span<int32_t> order,std::span<const double> values) {
    for(size_t i=0;i<order.size();i+=32)insertion(order.subspan(i,std::min<size_t>(32,order.size()-i)),values);
    std::vector<int32_t> temp(order.size());
    for(size_t width=32;width<order.size();width*=2) {
        for(size_t start=0;start<order.size();start+=width*2) {
            const auto mid=std::min(start+width,order.size()),end=std::min(start+2*width,order.size());
            size_t a=start,b=mid,out=start;
            while(a<mid && b<end)temp[out++]=values[order[b]]>values[order[a]]?order[b++]:order[a++];
            while(a<mid)temp[out++]=order[a++];while(b<end)temp[out++]=order[b++];
        }
        std::copy(temp.begin(),temp.end(),order.begin());
    }
}
// 3a2550/3a29f0 have sentinel moves before and after their merge loops.
// They are observable for unordered scores, so a library stable_sort/merge
// cannot substitute for these routines even when finite-score order matches.
void mergeRank(std::span<int32_t> order,size_t mid,std::span<const double> values) {
    size_t first=0,last=order.size();
    while(first<mid && !(values[order[mid]]>values[order[first]]))++first;
    if(first==mid)return;
    while(last-1>mid && !(values[order[last-1]]>values[order[mid-1]]))--last;
    if(last-1==mid) {
        const auto item=order[mid];std::move_backward(order.begin()+first,order.begin()+mid,order.begin()+mid+1);order[first]=item;return;
    }
    const auto leftCount=mid-first,rightCount=last-mid;
    if(leftCount==1) {
        const auto item=order[first];std::move(order.begin()+mid,order.begin()+last,order.begin()+first);order[last-1]=item;return;
    }
    if(leftCount<=rightCount) {
        std::vector<int32_t> buffer(order.begin()+first,order.begin()+mid);size_t a=0,b=mid,out=first;
        order[out++]=order[b++];
        for(;;) {
            if(values[order[b]]>values[buffer[a]]) {
                order[out++]=order[b++];
                if(b==last){std::copy(buffer.begin()+a,buffer.end(),order.begin()+out);return;}
            } else {
                order[out++]=buffer[a++];
                if(a+1==buffer.size()) {while(b<last)order[out++]=order[b++];order[out]=buffer[a];return;}
            }
        }
    } else {
        std::vector<int32_t> buffer(order.begin()+mid,order.begin()+last);size_t a=mid,b=buffer.size(),out=last;
        order[--out]=order[--a];
        for(;;) {
            if(values[buffer[b-1]]>values[order[a-1]]) {
                order[--out]=order[--a];
                if(a==first){order[--out]=buffer[--b];std::copy(buffer.begin(),buffer.begin()+b,order.begin()+out-b);return;}
            } else {
                order[--out]=buffer[--b];
                if(b==1) {order[--out]=order[--a];std::move_backward(order.begin()+first,order.begin()+a,order.begin()+out);order[first]=buffer[0];return;}
            }
        }
    }
}
}
std::vector<int32_t> historyOrder(std::span<const double> values) {
    if(values.size()>INT32_MAX)throw std::length_error("history index exceeds original signed range");
    std::vector<int32_t> order(values.size());std::iota(order.begin(),order.end(),0);
    if(order.size()<=32)insertion(order,values);
    else {
        const auto mid=(order.size()+1)/2;
        bufferedSort(std::span(order).first(mid),values);bufferedSort(std::span(order).subspan(mid),values);mergeRank(order,mid,values);
    }
    std::reverse(order.begin(),order.end());return order;
}
void refreshHistoryWeights(HistoryStatistics &s,bool exploitOnly) {
    const auto order=historyOrder(s.estimates);const auto count=order.size();
    if(s.attempts.size()!=count)throw std::invalid_argument("history attempts/estimate count differs");
    std::vector<double> exploration(count),exploitation(count);double exploreSum=0.,exploitSum=0.;
    for(size_t rank=0;rank<count;++rank) {
        const auto i=static_cast<size_t>(order[rank]);const auto trials=s.attempts[i];
        const int32_t square=std::bit_cast<int32_t>(uint32_t(trials)*uint32_t(trials)+1u);
        exploration[i]=1./static_cast<double>(square)+.01;
        if(int32_t(rank)<s.elite && trials>0)exploitation[i]=1./static_cast<double>(rank+1);
        exploreSum=exploreSum+exploration[i];exploitSum=exploitSum+exploitation[i];
    }
    const double ratio=static_cast<double>(s.updates)/static_cast<double>(count);
    const double scale=ratio>1.?ratio:1.; // MAXSD with second operand 1
    s.weights.resize(count);double total=0.;
    for(size_t i=0;i<count;++i) {
        exploration[i]=exploitOnly?0.:exploration[i]/exploreSum;
        if(exploitSum>0.)exploitation[i]=exploitation[i]/exploitSum;
        s.weights[i]=scale*exploitation[i]+exploration[i];total=total+s.weights[i];
    }
    for(auto &w:s.weights)w=w/total;
}
void updateHistory(HistoryStatistics &s,size_t i,double length) {
    if(i>=s.samples.size() || i>=s.estimates.size())throw std::out_of_range("history sample index");
    s.updates=std::bit_cast<int32_t>(uint32_t(s.updates)+1u);
    auto &all=s.samples[i];all.push_back(length);
    std::vector<double> recent(all.end()-std::min<size_t>(5,all.size()),all.end());
    // This is the original <=5-item insertion path, preserving NaN placement
    // and summation order rather than passing a non-ordering relation to sort.
    for(size_t j=1;j<recent.size();++j) {
        const auto value=recent[j];size_t k=j;
        while(k && value<recent[k-1]){recent[k]=recent[k-1];--k;}recent[k]=value;
    }
    recent.resize(std::min<size_t>(3,recent.size()));double sum=0.;for(auto v:recent)sum=sum+v;
    s.estimates[i]=sum/static_cast<double>(recent.size());
    if(length<s.best)s.best=length;
}
HistoryDistribution historyDistribution(std::span<const double> input) {
    HistoryDistribution out;out.normalized.assign(input.begin(),input.end());
    double sum=0.;for(auto v:input)sum=orderedAdd(sum,v);
    if(input.empty())out.normalized.push_back(1.);
    else if(sum!=1.)for(auto &v:out.normalized)v=v/sum;
    out.cumulative.push_back(out.normalized.front());
    for(size_t i=1;i<out.normalized.size();++i)out.cumulative.push_back(orderedAdd(out.cumulative.back(),out.normalized[i]));
    return out;
}
HistoryChoice chooseHistory(HistoryStatistics &s,Random &rng,bool exploitOnly) {
    if(s.estimates.empty())throw std::invalid_argument("original history selection requires an entry");
    refreshHistoryWeights(s,exploitOnly);const auto distribution=historyDistribution(s.weights);
    const double value=rng.unit();size_t lo=0,count=distribution.cumulative.size()-1;
    while(count) {
        const auto half=count/2,mid=lo+half;
        if(distribution.cumulative[mid]<value){lo=mid+1;count-=half+1;}else count=half;
    }
    auto &attempts=s.attempts.at(lo);const auto previous=attempts;attempts=std::bit_cast<int32_t>(uint32_t(attempts)+1u);
    return {int32_t(lo),previous};
}
PreparedHistory prepareHistory(HistoryControl &h,TrialNesting &nesting,double elapsed,std::optional<int32_t> attempts) {
    if(h.elements.size()!=h.statistics.estimates.size() || h.elements.size()>INT32_MAX)
        throw std::invalid_argument("history element/estimate count differs");
    PreparedHistory out;
    const int32_t threshold=std::bit_cast<int32_t>(uint32_t(h.elements.size())*5u);
    if(!h.dynamic29dc && h.phase1c8==0 && elapsed>h.budget1b8/3. && attempts && *attempts>threshold) {
        h.dynamic29dc=true;out.enabledDynamic=true;
    }
    if(h.dynamic29dc) {
        out.choice=chooseHistory(h.statistics,h.random,h.phase1c8!=0);
        nesting.element14c0=h.elements.at(size_t(out.choice->index));
        nesting.trial13bc=out.choice->previousAttempts;
    } else nesting.element14c0=h.fixedElement;
    return out;
}
CompletedHistory completeHistory(HistoryControl &h,const TrialNesting &nesting,const HistoryCompletionInput &in) {
    CompletedHistory out;
    // COMISD/JBE rejects unordered and equal values in both admission gates.
    const bool complete=in.placedArea>((in.target14b8-in.queue20)+in.placedArea)*.9999999;
    if(!complete || in.managerId!=h.targetManager29d8)return out;
    // rawCandidateLength preserves CVTTSD2SI and wrapped integer subtraction.
    const double length=static_cast<double>(rawCandidateLength(in.maximumRight,in.trim70))/1e6;
    if(h.dynamic29dc) {updateHistory(h.statistics,size_t(in.index),length);out.updated=true;}
    const int32_t logging=(!(in.config758>=-2147483648. && in.config758<2147483648.))?INT32_MIN:static_cast<int32_t>(in.config758);
    if(logging!=0) {
        const auto element=h.dynamic29dc?h.elements.at(size_t(in.index)):nesting.element14c0;
        if(!element)throw std::invalid_argument("history logging requires a structuring element");
        out.log=HistoryLogEntry{length,element->value10,in.managerId,in.elapsed};
    }
    return out;
}
}
