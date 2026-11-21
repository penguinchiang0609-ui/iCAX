#include "recovered_shaker.hpp"
#include "recovered_shaker_solver.hpp"
#include "Highs.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
double primalViolation(const LinearModel &model,const std::vector<double> &values){
    if(values.size()!=model.columns.size())return std::numeric_limits<double>::infinity();
    double maximum=0.;
    for(size_t index=0;index<values.size();++index){
        if(!std::isfinite(values[index]))return std::numeric_limits<double>::infinity();
        maximum=std::max({maximum,model.columns[index].lower-values[index],values[index]-model.columns[index].upper});
    }
    for(const auto &row:model.rows){double activity=0.;for(const auto &term:row.terms)activity+=term.coefficient*values[size_t(term.column)];
        if(!std::isfinite(activity))return std::numeric_limits<double>::infinity();
        maximum=std::max({maximum,row.lower-activity,activity-row.upper});}
    return maximum;
}
}
ShakerSolution solveShakerHighs(const LinearModel &model,double seconds,double feasibilityTolerance,int32_t iterationLimit){
    const auto started=std::chrono::steady_clock::now();
    ShakerSolution result;
    if(std::isnan(seconds)||seconds<0)throw std::invalid_argument("Shaker time budget");
    Highs highs;highs.setOptionValue("output_flag",false);highs.setOptionValue("threads",1);
    highs.setOptionValue("parallel","off");highs.setOptionValue("simplex_max_concurrency",1);
    shakerSetIterationLimit(iterationLimit,[&](std::string_view key,int32_t value){
        if(highs.setOptionValue(std::string(key),value)!=HighsStatus::kOk)throw std::invalid_argument("Shaker HiGHS iteration limit");});
    shakerSetFeasibilityTolerance(feasibilityTolerance,[&](std::string_view key,double value){
        if(highs.setOptionValue(std::string(key),value)!=HighsStatus::kOk)throw std::invalid_argument("Shaker HiGHS feasibility tolerance");});
    if(std::isfinite(seconds))highs.setOptionValue("time_limit",seconds);
    HighsLp lp;lp.num_col_=static_cast<HighsInt>(model.columns.size());lp.num_row_=static_cast<HighsInt>(model.rows.size());
    lp.sense_=model.minimize?ObjSense::kMinimize:ObjSense::kMaximize;
    for(const auto &column:model.columns){lp.col_lower_.push_back(column.lower);lp.col_upper_.push_back(column.upper);lp.col_cost_.push_back(column.cost);}
    lp.a_matrix_.num_col_=lp.num_col_;lp.a_matrix_.num_row_=lp.num_row_;
    lp.a_matrix_.format_=MatrixFormat::kRowwise;lp.a_matrix_.start_.assign(1,0);
    for(const auto &row:model.rows){
        lp.row_lower_.push_back(row.lower);lp.row_upper_.push_back(row.upper);
        for(const auto &term:row.terms){
            if(term.column<0||size_t(term.column)>=model.columns.size())throw std::out_of_range("Shaker matrix column");
            lp.a_matrix_.index_.push_back(term.column);lp.a_matrix_.value_.push_back(term.coefficient);
        }
        lp.a_matrix_.start_.push_back(static_cast<HighsInt>(lp.a_matrix_.value_.size()));
    }
    if(highs.passModel(lp)==HighsStatus::kError)return result;
    if(highs.run()==HighsStatus::kError)return result;
    const auto status=highs.getModelStatus();
    if(status==HighsModelStatus::kTimeLimit||status==HighsModelStatus::kIterationLimit){result.status=3;return result;}
    if(status!=HighsModelStatus::kOptimal&&status!=HighsModelStatus::kModelEmpty)return result;
    // HiGHS can classify sub-tolerance hard-row violations as zero. Keep the
    // recovered row model and first solve settings, then polish that same LP
    // when its actual primal residual could corrupt SDK coordinate rounding.
    // This is an adapter refinement, not a change to recovered Shaker policy.
    if(feasibilityTolerance>1e-9&&primalViolation(model,highs.getSolution().col_value)>1e-9){
        const double remaining=seconds-std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        if(!(remaining>0.)){result.status=3;return result;}
        return solveShakerHighs(model,remaining,1e-9,iterationLimit);
    }
    const auto &solution=highs.getSolution();result.status=0;result.objective=highs.getObjectiveValue();
    result.primal=solution.col_value;result.dual=solution.row_dual;return result;
}
}
