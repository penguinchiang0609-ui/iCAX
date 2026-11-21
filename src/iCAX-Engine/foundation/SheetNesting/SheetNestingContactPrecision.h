#pragma once
#include "SheetNestingPipeline.h"
namespace iCAX::SheetNesting {
enum class ContactPrecisionStatus { Applied, AlreadySeparated, InvalidInput, Unsupported, LimitReached, Infeasible };
struct ContactPrecisionOptions {
    double Clearance=1e-6;
    double MaxAxisShift=2e-4;
    double TimeLimitSeconds=5;
    std::size_t MaxSweeps=100000;
    std::function<bool()> Cancel;
};
struct ContactPrecisionResult {
    ContactPrecisionStatus State=ContactPrecisionStatus::Unsupported;
    Result Output;
    std::size_t Constraints=0, Sweeps=0;
    double MaximumShift=0, MaximumResidual=0;
    std::string Diagnostic;
};
// Optional, conservative whole-layout micro-translation refinement. Original
// result is returned unchanged whenever refinement cannot be validated.
// Multiple physical sheets and positive nominal gaps are supported. A bounded
// extra clearance repairs floating-point contact poses without changing parts.
ContactPrecisionResult RefineContactPrecision(const Problem&,const Result&,const ContactPrecisionOptions&);
// Optional precision stage; never registered in the default strategy pool.
PostProcessStage MakeContactPrecisionStage(ContactPrecisionOptions options={});
}
