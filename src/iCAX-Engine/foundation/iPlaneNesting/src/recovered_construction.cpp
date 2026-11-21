#include "recovered_construction.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace study::recovered {
namespace {
struct Product {uint64_t low,high;};
Product multiply(uint64_t a,uint64_t b) {
    const uint64_t al=uint32_t(a),ah=a>>32,bl=uint32_t(b),bh=b>>32;
    const uint64_t first=al*bl,second=ah*bl+(first>>32);
    const uint64_t third=al*bh+uint32_t(second);
    return {(third<<32)|uint32_t(first),ah*bh+(second>>32)+(third>>32)};
}
int32_t cvtt32(double v) {
    if(!(v>=-2147483648. && v<2147483648.))return INT32_MIN;
    return static_cast<int32_t>(v);
}
int64_t cvtt64(double v) {
    if(!(v>=-9223372036854775808. && v<9223372036854775808.))return INT64_MIN;
    return static_cast<int64_t>(v);
}
struct Guard {
    ConstructionHost &host;bool supervisor;
    Guard(ConstructionHost &h,bool s):host(h),supervisor(s){if(s)h.lockSupervisor();else h.lockManager();}
    ~Guard(){if(supervisor)host.unlockSupervisor();else host.unlockManager();}
};
}
uint64_t boundedDraw(Random &rng,uint64_t limit) {
    const bool wide=(limit-1)>UINT32_MAX;
    const uint64_t mask=wide?UINT64_MAX:UINT32_MAX;
    auto draw=[&] {uint64_t v=rng.next();if(wide)v=(v<<32)|rng.next();return v;};
    auto product=multiply(limit,draw());
    uint64_t low=product.low&mask;
    if(low<limit) {
        const uint64_t threshold=(mask-limit+1)%limit;
        while(low<threshold){product=multiply(limit,draw());low=product.low&mask;}
    }
    return wide?product.high:(product.low>>32)|(product.high<<32);
}
void resetTrialRandom(Random &rng,double configured,uint32_t salt) {
    rng.words[1250]=salt;
    uint32_t value=uint32_t(cvtt32(configured))+salt;rng.words[1]=value;
    for(uint32_t i=1;i<624;++i){value=((value>>30)^value)*0x6c078965u+i;rng.words[i+1]=value;}
    rng.words[0]=624;
}
int32_t beginConstruction(TrialManagerState &s) {
    const auto salt=s.count60;
    s.count60=std::bit_cast<int32_t>(uint32_t(s.count60)+1);
    s.totalC8=s.totalC8+.0101;
    return salt;
}
void accountConstruction(TrialManagerState &s,double elapsed) {
    s.totalC8=(elapsed-.01)+s.totalC8;
    const int32_t previous=std::bit_cast<int32_t>(uint32_t(s.count60)-1);
    s.meanD0=((static_cast<double>(previous)*s.meanD0+elapsed)-.01)/static_cast<double>(s.count60);
}
double warmStartFraction(const TrialManagerState &s) {
    double fraction=s.totalC8/s.budget58;
    if(s.budget58<=s.meanD0*.5+s.totalC8)fraction=fraction+1.;
    fraction=!(fraction>1.)?fraction:1.; // COMISD/CMOVBE also selects NaN
    fraction=fraction>0.?fraction:0.; // MAXSD selects its second operand for NaN/ties
    fraction=fraction*.95;
    return .05<=fraction?fraction:.05;
}
int64_t constructionLengthBound(int64_t corrected,double worstShake,double currentShake,double factor) {
    const double worstFactor=(worstShake*4.)*factor+factor;
    const int64_t reference=referenceCandidateLength(corrected,worstShake,worstFactor);
    const double denominator=1.-(1.-currentShake)*((currentShake*4.)*factor+factor);
    const double scaled=((static_cast<double>(reference)/1e6)/denominator)*1e6;
    return cvtt64(scaled<0.?scaled-.5:scaled+.5);
}
double constructionSeconds(int64_t ticks){return static_cast<double>(ticks/1000000)/1000.;}
void recordTiming(TrialRecord &r,TimingKey key,double seconds) {
    r.timings.clear();
    if(key.stage==0)r.timings[{1,0}]=seconds;
    r.timings[key]=seconds;
}
bool warmStartEligible(const TrialRecord &r,bool high) {
    return high?(uint32_t(r.sourceId)-11u)<10u:r.sourceId<11;
}
bool shakeHistoryEligible(TrialRecord &r){return r.timings[{1,0}]>0.;}
std::optional<size_t> selectWarmStart(std::span<const TrialRecordPtr> records,double fraction,bool high,uint32_t salt) {
    if(fraction<=0.)return {}; // COMISD 0,fraction / JAE does not reject NaN
    std::vector<size_t> eligible;
    for(size_t i=0;i<records.size();++i)if(warmStartEligible(*records[i],high))eligible.push_back(i);
    if(eligible.empty())return {};
    Random rng(salt);return eligible.at(static_cast<size_t>(boundedDraw(rng,eligible.size())));
}
bool shouldShake(std::span<const TrialRecordPtr> records,const std::shared_ptr<TrialNesting> &nesting,
    const std::function<TrialRecordPtr(const std::shared_ptr<TrialNesting> &)> &evaluate,
    const std::function<bool(const TrialRecordPtr &,const TrialRecordPtr &)> &better) {
    if(!nesting || !evaluate || !better)return false;
    TrialRecordPtr last;
    for(const auto &record:records)if(shakeHistoryEligible(*record))last=record;
    if(!last)return false;
    return better(evaluate(nesting),last);
}
PreparedRecord prepareRecord(ConstructionHost &h,double elapsed) {
    if(cvtt32(h.config4e0())==1)h.preprocess();
    h.finalizeGeometry();
    const int32_t sourceId=h.managerId();
    auto record=std::make_shared<TrialRecord>();record->nesting=h.current();
    record->rawLength=h.rawLength(*record->nesting);record->sourceId=sourceId;
    h.evaluate(record);
    {Guard lock(h,false);accountConstruction(h.manager(),elapsed);}
    const auto inserted=h.notifyLocal(record);
    return {std::move(record),inserted.inserted};
}
ConstructionRun runConstruction(ConstructionHost &h) {
    ConstructionRun out;
    const double started=constructionSeconds(h.clockTicks());
    h.resetCurrent();
    if(const auto best=h.bestGoal()) {
        auto &n=*h.current();n.goal14d0=best->metric;n.goal14d8=(static_cast<double>(best->rawLength)/1e6)*best->height;n.goal14e0=true;
    }
    int32_t salt,count;
    {Guard lock(h,false);salt=beginConstruction(h.manager());count=h.manager().count60;}
    h.resetRandom(salt);h.current()->trial13bc=count;h.current()->bound14a8=0;
    if(h.phase()==1) {
        double fraction;
        {Guard lock(h,false);fraction=warmStartFraction(h.manager());}
        h.prepareWarmStart(fraction,h.managerId()>10);
        const auto worst=h.worstCandidate();
        if(!worst.hasGroups)h.current()->bound14a8=constructionLengthBound(worst.corrected,worst.shake,h.current()->shake14e8,h.evaluatorFactor());
    }
    h.runPreNester();h.prepareModules();
    const uint32_t history=h.useHistory()?h.prepareHistory():0;
    h.createStrategy();h.attachStop();h.runStrategy();h.finalizeStrategy();h.completeHistory(history);
    double shakeSeconds=0.;
    if(h.current()->shake14e8!=1. && h.mayShake()) {
        out.shakeAttempted=true;
        const double shakeStarted=constructionSeconds(h.clockTicks());
        out.shakeSucceeded=h.shake();
        if(out.shakeSucceeded)shakeSeconds=h.preciseSeconds()-shakeStarted;
    }
    h.current()->bound14a8=0;
    auto prepared=prepareRecord(h,constructionSeconds(h.clockTicks())-started);
    out.record=prepared.record;out.localInserted=prepared.localInserted;
    recordTiming(*out.record,{1,0},shakeSeconds);
    CandidatePool<TrialRecordPtr>::Inserted inserted;
    {Guard lock(h,true);inserted=h.notifyGlobal(out.record);}
    out.globalInserted=inserted.inserted;out.newBest=inserted.newBest;
    if(inserted.inserted && inserted.newBest && h.hasBestCallback())h.publishBest(out.record);
    h.logTrial(out.record,constructionSeconds(h.clockTicks())-started);
    if(out.localInserted || out.globalInserted) {
        h.releaseCurrentResources();h.current()=h.acquireNesting();out.replacedNesting=true;
    }
    return out;
}
}
