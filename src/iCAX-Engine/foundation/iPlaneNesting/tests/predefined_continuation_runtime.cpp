#include "ordinary_predefined_validation.hpp"
#include <iostream>

namespace {
size_t checks=0,cases=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
study::Placement at(size_t group,size_t ordinal,double x,double y,size_t pose=0){study::Placement result{group,ordinal,pose,study::rounded(x*1e6),study::rounded(y*1000.)};return result;}
}
int main(){using namespace study;try{
    Input input;input.width=20;input.height=12;input.pitch=.5;PartInput part;part.polygon={{0,0},{4,0},{4,3},{0,3}};part.quantity=4;part.poses={{0,false},{90,false}};input.parts={part};
    const Output manual{{at(0,0,-2,2),at(0,1,-2,2)},500000};
    {
        auto candidate=manual;candidate.placements.push_back(at(0,2,8,0));candidate.placements.push_back(at(0,3,13,7));const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!valid_layout(input,manual)&&!valid_layout(input,candidate),"source case actually contains original out-of-sheet and overlap defects");
        check(result.valid&&result.complete&&result.preservedOriginal==2&&result.added==2&&result.preservedPairs==1,"unchanged original defects coexist with complete valid additional parts");++cases;
    }
    {
        auto candidate=manual;candidate.placements.push_back(at(0,2,0,3));const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::NewMaterialCollision,"new part cannot overlap the inside portion of a partially outside original part");++cases;
    }
    {
        auto candidate=manual;candidate.placements.push_back(at(0,2,19,8));const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::ChangedPartOutsideSheet,"expanded collision domain never excuses a new part outside the original sheet");++cases;
    }
    {
        const auto result=validatePredefinedContinuation(input,manual,manual);check(result.valid&&!result.complete&&result.preservedOriginal==2&&!result.added,"valid partial continuation is distinguished from complete demand");++cases;
    }
    {
        auto candidate=manual;candidate.placements[0]=at(0,0,0,3);const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::NewMaterialCollision,"a moved original part must clear collision even with an originally conflicting partner");++cases;
    }
    {
        auto candidate=manual;candidate.placements[0]=at(0,0,0,7);const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(result.valid&&result.movedOriginal==1&&result.preservedOriginal==1&&!result.complete,"a movable original may leave the old conflict while another unchanged original remains outside");++cases;
    }
    {
        auto candidate=manual;candidate.placements.pop_back();const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::MissingOriginalInstance,"continuation cannot omit an original concrete instance");++cases;
    }
    {
        auto candidate=manual;candidate.placements.push_back(candidate.placements.front());const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::DuplicateCandidateInstance,"continuation cannot duplicate an original concrete instance");++cases;
    }
    {
        auto candidate=manual;candidate.placements.push_back(at(0,4,8,0));const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::InvalidCandidateInstance,"continuation cannot manufacture an ordinal outside demand");++cases;
    }
    {
        auto candidate=manual;candidate.placements.push_back(at(3,0,8,0));const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::InvalidCandidateInstance,"continuation cannot manufacture a part group");++cases;
    }
    {
        auto candidate=manual;candidate.placements[0]=at(0,0,0,0,1);const auto result=validatePredefinedContinuation(input,manual,candidate);
        check(!result.valid,"changing an original pose is a move and cannot retain original defect exemptions");++cases;
    }
    {
        const Output source{{at(0,0,0,0)},500000};auto candidate=source;candidate.placements.push_back(at(0,1,4,0));candidate.placements.push_back(at(0,2,8,0));const auto result=validatePredefinedContinuation(input,source,candidate);
        check(result.valid&&!result.complete&&result.added==2,"material boundary contact remains valid and partial demand remains explicit");++cases;
    }
    {
        auto far=manual;far.placements[0]=at(0,0,-100000,-100000);far.placements[1]=at(0,1,-100000,-100000);auto candidate=far;candidate.placements.push_back(at(0,2,0,0));const auto result=validatePredefinedContinuation(input,far,candidate);
        check(result.valid&&!result.complete,"expanded collision domain preserves a finite remote original defect without growing the real sheet");++cases;
    }
    {
        auto duplicate=manual;duplicate.placements[1]=duplicate.placements[0];const auto result=validatePredefinedContinuation(input,duplicate,duplicate);
        check(!result.valid&&result.failure==PredefinedContinuationFailure::DuplicateOriginalInstance,"invalid original instance identity is never grandfathered as geometry");++cases;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
