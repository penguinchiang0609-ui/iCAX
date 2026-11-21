#include "recovered_shaker_solver.hpp"
namespace study::recovered {
void shakerSetIterationLimit(int32_t value,const ShakerIntegerOption &set){
    set("simplex_iteration_limit",value);set("ipm_iteration_limit",value);
}
void shakerSetFeasibilityTolerance(double value,const ShakerRealOption &set){
    set("primal_feasibility_tolerance",value);set("dual_feasibility_tolerance",value);
}
}
