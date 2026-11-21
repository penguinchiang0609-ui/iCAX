#include "recovered_chris.hpp"

namespace study::recovered {
ChrisFactoryResult chrisFactory(Random &random, const ChrisFactoryInput &input,
    const std::vector<ValuePart> &parts, double totalArea) {
    ChrisFactoryResult result;
    const double draw = random.unit();
    result.batch = truncateInt32(double(input.contextBatch238) * ((draw + draw) + 1.));
    result.complexity = drawComplexity(random, input.config);
    result.evaluationValues = values(parts, totalArea, result.complexity);
    result.reference = ReferenceKind::Normal;
    result.currentReference = input.currentReference;
    result.flagD0 = truncateInt32(input.config700) != 0;
    result.flagD8 = truncateInt32(input.config710) != 0;
    result.managerId = input.managerId;
    result.count10 = 0; result.flag14 = 0; result.limit18 = 0; result.count1c = 15;
    return result;
}
}
