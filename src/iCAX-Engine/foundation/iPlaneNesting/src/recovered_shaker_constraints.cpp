#include "recovered_shaker_constraints.hpp"
#include <bit>
#include <immintrin.h>
#include <stdexcept>
namespace study::recovered {
namespace {
double add(double a,double b){return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double sub(double a,double b){return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double neg(double value){return std::bit_cast<double>(std::bit_cast<uint64_t>(value)^0x8000000000000000ULL);}
}
int32_t shakerPairConstraintAxis(int32_t type){
    const auto positive=uint32_t(type);if(positive>23)return 0;
    const auto selected=positive%12;return selected<4?0:selected<8?1:2;
}
size_t shakerPairConstraintRows(const ShakerPairConstraint &constraint,bool rotation,
                               std::span<ShakerPart> parts,LinearModel &model){
    if(constraint.first>=parts.size()||constraint.second>=parts.size())throw std::out_of_range("Shaker pair-constraint part");
    auto &first=parts[constraint.first];auto &second=parts[constraint.second];const size_t axis=size_t(shakerPairConstraintAxis(constraint.type));
    const bool sum=uint32_t(constraint.type)>=12&&uint32_t(constraint.type)<=23;
    const double firstReference=axis<2?first.reference50[axis]:constraint.referenceAngles[0];
    const double secondReference=axis<2?second.reference50[axis]:constraint.referenceAngles[constraint.first==constraint.second?0:1];
    double rhs=sum?add(secondReference,firstReference):sub(firstReference,secondReference);
    std::vector<LinearTerm> terms;
    const bool firstLocked=first.locked[axis]||(axis==2&&!rotation),secondLocked=second.locked[axis]||(axis==2&&!rotation);
    if(!firstLocked)terms.push_back({first.columns[axis],1.});else rhs=sub(rhs,axis<2?first.position[axis]:first.angle);
    if(!secondLocked)terms.push_back({second.columns[axis],sum?1.:-1.});else rhs=sum?sub(rhs,axis<2?second.position[axis]:second.angle):add(rhs,axis<2?second.position[axis]:second.angle);
    if(terms.empty())return 0;
    const uint32_t mode=uint32_t(constraint.type)>23?0:uint32_t(constraint.type)%4;
    const bool lower=mode!=1,upper=mode!=2;
    const double value=mode==3?add(constraint.value,rhs):add(rhs,constraint.value),low=mode==3?sub(rhs,constraint.value):value;
    size_t count=0;
    if(lower){const auto row=model.addLowerRow(terms,low);first.rows.push_back(row);second.rows.push_back(row);++count;}
    if(upper){const auto row=model.addRow(std::move(terms),neg(std::numeric_limits<double>::infinity()),value);first.rows.push_back(row);second.rows.push_back(row);++count;}
    return count;
}
}
