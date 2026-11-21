#include "recovered_engine.hpp"
#include <bit>

namespace study::recovered {
SingleEngineBudgets singleEngineBudgets(const SingleEngineBudgetInput &input) {
    double first = .25, second = .25;
    if (input.rectangleModeC8) { first = .5; second = 0.; }
    double fraction = .98;
    if (truncateInt32(input.usePostop9b0) != 0 && !input.restrictedC9 &&
        !input.specialTechnology3c && !input.auxiliary240)
        fraction = input.strategyFraction9c0;
    if (truncateInt32(input.disableConstruction548) != 0) fraction = 0.;
    const double construction = input.remaining * fraction;
    SingleEngineBudgets result;
    result.selectionA0 = first * construction;
    result.refinementA8 = second * construction;
    result.continuationB0 = construction * .5;
    result.postB8 = input.remaining - construction;
    result.totalC0 = ((result.refinementA8 + result.selectionA0) + result.continuationB0) + result.postB8;
    return result;
}
namespace {
int64_t cvtt64(double value) {
    if (!(value >= -9223372036854775808. && value < 9223372036854775808.)) return INT64_MIN;
    return static_cast<int64_t>(value);
}
int64_t quantizePitch(double value) {
    return cvtt64(value < 0. ? value - .5 : value + .5);
}
}
int64_t contextPitch(std::span<const PitchSheet> sheets, double explicitPitch,
                     double maximumPitch, double stripsPerPart9d8, bool specialSheet) {
    if (explicitPitch != 0.) return quantizePitch(explicitPitch * 1000000.);
    double width = 0.;
    for (const auto &sheet : sheets) {
        const double current = (sheet.width - sheet.left) - sheet.right;
        width = current > width ? current : width; // MAXSD returns its second operand for ties/NaNs.
    }
    // Constants ff43a8 / ff43e0 are verified against the original image.
    double pitch = width / 3.;
    pitch = pitch < maximumPitch ? pitch : maximumPitch;
    if (specialSheet) stripsPerPart9d8 *= 5.;
    const int64_t result = quantizePitch((pitch / stripsPerPart9d8) * 1000000.);
    return result > 0 ? result : 1000000;
}
bool smallNestingShakeGate(const SupervisorShakerInput &input) {
    const double largeArea = (input.width * input.height) * .9;
    const double usableWidth = (input.width - input.left) - input.right;
    const double usableHeight = (input.height - input.bottom) - input.top;
    const double minimum = usableHeight <= usableWidth ? usableHeight : usableWidth;
    return !((input.placedCount > 7 || largeArea <= input.placedArea) &&
        ((minimum * minimum) * .5 <= input.placedArea || input.placedCount > 74) &&
        (!input.specialTechnology3c || largeArea <= input.placedArea));
}
int32_t supervisorShakerOperation(const SupervisorShakerInput &input) {
    if (input.specialTechnology3c) return 10;
    return input.restrictedC9 || smallNestingShakeGate(input) ? 2 : 1;
}
ShakerIterationOptions supervisorShakerOneOptions(double seconds, double pitch,
    double contextHeight220, double sceneScale, bool rotate) {
    ShakerIterationOptions options;
    options.maximumIterations = 25; options.stallLimit = 2;
    options.seconds = seconds == 0. ? 15. : seconds;
    options.improvementThreshold = pitch == 0. ? sceneScale * .0001 : pitch;
    const double window = contextHeight220 / 5.;
    options.translationWindow = window == 0. ? sceneScale * (1. / 30.) : window;
    options.rotationWindow = sceneScale * .01; options.angularWindow = .01;
    options.rotate = rotate; options.retryStatus2 = false;
    return options;
}
MultiPolishBudget multiPolishBudget(double remaining,uint64_t sheets,bool compactLast) {
    if(!sheets)return {};
    MultiPolishBudget result;
    if(compactLast){
        const double base=remaining/double(sheets+1);
        const double minimum=1.<=remaining?1.:remaining;
        result.reservedLast=base>minimum?base:minimum;
    }
    result.perLayout=(remaining-result.reservedLast)/double(sheets);return result;
}
std::optional<double> multiLastPolishSeconds(double remaining,int32_t count,uint64_t vertices) {
    if(count>3 && (count>=15 || vertices>119) && remaining<=1.)return {};
    return 1.<=remaining?remaining:1.;
}
bool multiPolishTypeLess(const std::map<int32_t,double> &a,const std::map<int32_t,double> &b) {
    auto delta=a;for(const auto [key,value]:b)delta[key]-=value;
    for(auto entry=delta.rbegin();entry!=delta.rend();++entry)if(std::abs(entry->second)>=1e-9)return entry->second>0.;
    return false;
}
bool multiPolishLess(const MultiPolishOrder &a,const MultiPolishOrder &b) {
    if(multiPolishTypeLess(a.typeQuantities,b.typeQuantities) || multiPolishTypeLess(b.typeQuantities,a.typeQuantities))
        return multiPolishTypeLess(a.typeQuantities,b.typeQuantities);
    if(a.sheetUserId!=b.sheetUserId)return a.sheetUserId<b.sheetUserId;
    const int64_t first=cvtt64(std::floor(a.quantity18*10.)/(a.height*a.width));
    const int64_t second=cvtt64(std::floor(b.quantity18*10.)/(b.height*b.width));
    if(first!=second)return first>second;
    if(a.sortedPartUserIds.size()!=b.sortedPartUserIds.size())return a.sortedPartUserIds.size()>b.sortedPartUserIds.size();
    return std::lexicographical_compare(a.sortedPartUserIds.begin(),a.sortedPartUserIds.end(),b.sortedPartUserIds.begin(),b.sortedPartUserIds.end());
}
MultiPostBudget multiPostBudget(double remaining,double policy,bool predefined) {
    MultiPostBudget result;
    if(truncateInt32(policy)!=0 && !predefined){const double reserve=remaining*.1;result.polishReserve=reserve<300.?reserve:300.;}
    result.optimization=remaining-result.polishReserve;return result;
}
std::optional<MultiPolishStockQuery> multiPolishStockQuery(const MultiPolishStock &sheet,
    std::optional<int64_t> occupiedLength,bool predefined) {
    if(predefined || sheet.hasContours)return {};
    const auto margin=quantizePitch(sheet.margin70*1000000.);
    const auto extent=std::bit_cast<int64_t>(uint64_t(occupiedLength.value_or(0))-uint64_t(margin));
    return MultiPolishStockQuery{double(extent)/1000000.,sheet.height,sheet.attributesD0,sheet.attributesD8};
}
std::optional<size_t> multiPolishLookupStock(std::span<const MultiPolishStock> sheets,
    const MultiPolishStockQuery &query) {
    std::optional<size_t> selected;
    for(size_t index=0;index<sheets.size();++index){
        const auto &sheet=sheets[index];
        if((!(std::abs(sheet.width-query.width)<1e-9) && query.width>sheet.width) ||
           (!(std::abs(sheet.height-query.height)<1e-9) && query.height>sheet.height) ||
           uint32_t(sheet.attributesD0)!=uint32_t(query.attributesD0))continue;
        if(selected){
            const double current=sheets[*selected].height*sheets[*selected].width;
            const double candidate=sheet.height*sheet.width;
            if(std::abs(candidate-current)<1e-9 || !(candidate<current))continue;
        }
        selected=index;
    }
    return selected;
}
bool multiPolishAcceptStock(const MultiPolishStock &current,const MultiPolishStock &candidate) {
    return candidate.identity && candidate.identity!=current.identity && !candidate.hasContours &&
        !(candidate.width>=current.width) &&
        (std::abs(current.height-candidate.height)<1e-9 || !(candidate.height>current.height));
}
}
