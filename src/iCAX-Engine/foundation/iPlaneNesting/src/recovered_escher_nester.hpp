#pragma once
#include "recovered_escher.hpp"
namespace study::recovered {
struct EscherNesterPart {
    uintptr_t key=0;
    int32_t required=0;
    bool optional=false;
    double area58=0,overrideValue220=-1,gap210=0;
};
struct EscherNesterPriority {
    double weight=0;
    uintptr_t key=0;
    bool operator==(const EscherNesterPriority &)const=default;
};
std::vector<EscherNesterPriority> escherNesterPriorities(std::span<const EscherNesterPart>,
    const std::function<int32_t(uintptr_t)> &); //3f4e50 initial negative live demand values
std::vector<uintptr_t> escherNesterOrder(std::span<const EscherNesterPriority>,int32_t trial,Random &); //3f5780, then3f4e50 adjacent perturbation
struct EscherNesterInput {
    std::span<const EscherNesterPart> parts;
    int32_t trial13bc=0;
    int64_t maximumRight=0;
    double leftTrim70=0;
    bool fallback90=true;
};
class EscherNesterHost {
public:
    virtual ~EscherNesterHost()=default;
    virtual int32_t available(uintptr_t)=0;
    virtual void prepareGreedy(std::span<const uintptr_t> order,int64_t fixedRight)=0;
    virtual double remainingTime()=0;
    virtual bool stopped()=0;
    virtual int32_t greedy()=0;
    virtual int32_t pattern(uintptr_t,double gap)=0;
    virtual bool hasRemaining()=0;
    virtual int32_t fallback()=0;
};
int32_t escherOrdinaryNester(const EscherNesterInput &,Random &,EscherNesterHost &); //3f4e50
}
