#include "ordinary_supervisor.hpp"
#include <iostream>
namespace {
size_t checks = 0, cases = 0, placements = 0;
void check(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
study::Input inputFor(bool concave, bool shifted) {
    study::Input input; input.width = 15; input.height = 12; input.pitch = .5;
    study::PartInput first, second;
    first.polygon = {{0, 0}, {3, 0}, {3, 2}, {0, 2}}; first.quantity = 3;
    first.poses = {{0, false}, {90, false}, {0, true}, {90, true}};
    second.polygon = concave ? study::Polygon{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}}
                             : study::Polygon{{0, 0}, {2, 0}, {2, 4}, {0, 4}};
    second.quantity = 3; second.poses = {{0, false}, {90, false}, {180, false}, {270, false}};
    if (shifted) {
        for (auto &point : first.polygon) { point.x += 6; point.y -= 3; }
        first.pivot = {7, -2};
        for (auto &point : second.polygon) { point.x -= 4; point.y += 5; }
        second.pivot = {-3, 7};
    }
    input.parts = {first, second}; return input;
}
uint64_t encoded(int base) { uint64_t result = 1; while (--base) result *= 10; return result; }
}
int main() {
    using namespace study;
    try {
        for (int id : {1, 2, 3, 4, 5, 11, 12, 13, 15}) {
            for (bool shifted : {false, true}) {
                auto input = inputFor(id % 2 != 0, shifted);
                SupervisorProgram settings;
                settings.initialPreset = id < 11 ? encoded(id) : 0;
                settings.reversePreset = id > 10 ? encoded(id - 10) : 0;
                settings.fastPreset = settings.initialPreset;
                settings.selectionTrialLimit = 2; settings.optimizationTrialLimit = 0;
                settings.usePostOptimization = false; settings.strategyFraction = 1;
                SupervisorTrace trace; Budget budget(10.);
                const auto output = ordinaryDefault(input, budget, settings, &trace);
                check(output.placements.size() == 6, "Supervisor actual family fills the required order");
                check(valid_layout(input, output), "Supervisor actual family material validity");
                check(trace.managerAttempts.size() == 1 && trace.managerAttempts.at(id) == 2 && trace.trials == 2,
                      "Supervisor original schedule invokes the selected concrete manager twice");
                check(trace.globalAccepted && trace.localAccepted && trace.candidateCount,
                      "Supervisor constructs and publishes actual local/global pool records");
                check(trace.unsupportedManagers.empty(), "Supervisor supported family never aliases an unsupported strategy");
                check(trace.warmSelections == 0, "Explicit phase-zero supervisor never injects a warm start");
                check(trace.orientationPopulation == (id % 2 ? 6 : 4) &&
                      trace.partOrientationCacheSizes == std::vector<size_t>{2, size_t(id % 2 ? 4 : 2)},
                      "Supervisor factory scale uses actual SDK/canonical orientation caches rather than pose or quantity counts");
                placements += output.placements.size(); ++cases;
            }
        }
        for (int id : {1, 3, 11, 13}) {
            auto input = inputFor(true, true);
            input.width = 30; input.height = 20;
            for (auto &part : input.parts) part.quantity = 20;
            SupervisorProgram settings;
            settings.initialPreset = id < 11 ? encoded(id) : 0;
            settings.reversePreset = id > 10 ? encoded(id - 10) : 0;
            settings.fastPreset = settings.initialPreset;
            settings.selectionTrialLimit = 1; settings.optimizationTrialLimit = 1;
            settings.constructionPhases = {0, 1, 1};
            settings.usePostOptimization = false; settings.strategyFraction = 1;
            SupervisorTrace trace; Budget budget(15.);
            const auto output = ordinaryDefault(input, budget, settings, &trace);
            check(output.placements.size() == 40 && valid_layout(input, output), "Supervisor warm continuation validity");
            check(trace.stages.size() == 3 && trace.trials == 3 && trace.warmSelections == 2 && trace.warmRetained > 0,
                  "Supervisor explicit warm phases copy recovered selected prefixes into actual state");
            placements += output.placements.size(); ++cases;
        }
        auto input = inputFor(false, false);
        SupervisorProgram rectangle;
        rectangle.initialPreset = encoded(8); rectangle.reversePreset = 0;
        rectangle.selectionTrialLimit = 1; rectangle.optimizationTrialLimit = 0;
        rectangle.usePostOptimization = false; rectangle.strategyFraction = 1;
        SupervisorTrace rectangleTrace; Budget rectangleBudget(1.);
        const auto rectangular = ordinaryDefault(input, rectangleBudget, rectangle, &rectangleTrace);
        check(rectangular.placements.size() == 6 && valid_layout(input, rectangular), "Supervisor actual Rectangle family");
        check(rectangleTrace.managerAttempts.contains(8) && rectangleTrace.unsupportedManagers.empty(), "Rectangle concrete dispatch");
        placements += rectangular.placements.size(); ++cases;
        {
            auto geometry = inputFor(true, false);
            SupervisorProgram history;
            history.initialPreset = encoded(3); history.reversePreset = 0;
            history.selectionTrialLimit = 3; history.optimizationTrialLimit = 0;
            history.strategyFraction = 1; history.usePostOptimization = false; history.dynamicHistory = true;
            for (const auto size : {Point{1, 2}, Point{2, 4}, Point{4, 6}}) {
                auto reference = std::make_shared<SupervisorReference>();
                reference->polygon = {{0, -size.y / 2}, {size.x, -size.y / 2}, {size.x, size.y / 2}, {0, size.y / 2}};
                reference->value10 = size.y;
                history.historyElements.push_back(reference);
            }
            SupervisorTrace record; Budget time(10.);
            const auto result = ordinaryDefault(geometry, time, history, &record);
            check(result.placements.size() == 6 && valid_layout(geometry, result), "Supervisor actual history geometry and BT layout");
            check(record.historyChoices == 3 && record.historyUpdates == 3,
                  "Supervisor dynamic history chooses owned reference polygons and records actual layout lengths");
            placements += result.placements.size(); ++cases;
        }
        for (const bool concave : {false, true}) {
            auto geometry = inputFor(concave, true);
            SupervisorTrace record; Budget time(.8);
            const auto result = ordinaryDefault(geometry, time, &record);
            check(result.placements.size() == 6 && valid_layout(geometry, result), "Uncapped default supervisor layout");
            check(record.trials > 1 && record.globalAccepted && record.postOptimizationTasks > 0,
                  "Default real timed selection, candidate publication and Shaker scene execute together");
            check(record.postOptimizationOperations.contains(2),
                  "Small default orders select original compaction operation two");
            placements += result.placements.size(); ++cases;
        }
        {
            auto geometry = inputFor(false, false); geometry.width = 30; geometry.height = 20;
            for (auto &part : geometry.parts) part.quantity = 20;
            SupervisorProgram settings; settings.initialPreset = encoded(3); settings.reversePreset = 0;
            settings.selectionTrialLimit = 1; settings.optimizationTrialLimit = 0;
            SupervisorTrace record; Budget time(4.);
            const auto result = ordinaryDefault(geometry, time, settings, &record);
            check(result.placements.size() == 40 && valid_layout(geometry, result), "Large-order Shaker operation-one material validity");
            check(record.postOptimizationOperations.contains(1) && !record.postOptimizationOperations.contains(2),
                  "Original area/count gate selects operation one independently from X direction two");
            check(record.postOptimizationTasks == 1 && record.shakerGainSamples.size() == 1,
                  "Second post phase skips the result already timed under its actual operation key");
            placements += result.placements.size(); ++cases;
        }
        for(const bool concave:{false,true}) {
            auto geometry=inputFor(concave,false);
            SupervisorProgram settings;settings.initialPreset=encoded(3);settings.reversePreset=0;
            settings.selectionTrialLimit=1;settings.optimizationTrialLimit=0;
            settings.engineBudget.specialTechnology3c=true;
            settings.technologyMinimum40=.1;
            SupervisorTrace record;Budget time(6.);
            const auto result=ordinaryDefault(geometry,time,settings,&record);
            check(result.placements.size()==6 && valid_layout(geometry,result),"Special-technology Shaker operation ten material validity");
            check(record.postOptimizationOperations.contains(10) && !record.postOptimizationOperations.contains(2) &&
                record.postOptimizationGuides>0 && !record.missingProviders.contains("supervisor.shakerOperation10"),
                "Operation ten consumes actual occupancy terrain and weighted movable guide parts");
            check(record.postOptimizationTasks==1,"Second post phase skips an operation-ten result already timed under type ten");
            placements+=result.placements.size();++cases;
        }
        {
            Budget time(1.);
            const auto footprint=rasterFootprint({{{.25,.75},{2.25,.75},{2.25,3.75},{.25,3.75}}},1000000,time);
            check(footprint.rows==Rows(2,Row{{0,3000}}) && footprint.offsetStrip==-1 && footprint.offsetY==-750,
                "Occupied mode-zero raster preserves both slabs without aligned leading empty/intersection");
            ++cases;
        }
        for (const auto dimensions : {Point{1, 30}, Point{1, .001}}) {
            Input geometry; geometry.width = 40; geometry.height = dimensions.y > 1 ? 10 : 6; geometry.pitch = .5;
            PartInput part; part.quantity = 2;
            part.polygon = {{0, 0}, {dimensions.x, 0}, {dimensions.x, dimensions.y}, {0, dimensions.y}};
            part.poses = {{0, false}, {90, false}}; geometry.parts.push_back(part);
            SupervisorProgram settings; settings.initialPreset = encoded(3); settings.reversePreset = 0;
            settings.selectionTrialLimit = 1; settings.optimizationTrialLimit = 0;
            settings.usePostOptimization = false; settings.strategyFraction = 1;
            SupervisorTrace record; Budget time(3.);
            const auto result = ordinaryDefault(geometry, time, settings, &record);
            check(result.placements.size() == (dimensions.y > 1 ? 0U : 2U) && valid_layout(geometry, result),
                  "Default context zero/large batch follows actual factory semantics without a borrowed explicit-API cap");
            placements += result.placements.size(); ++cases;
        }
        Budget empty(1.); check(ordinaryDefault({}, empty).placements.empty(), "Empty supervisor input");
        std::cout << "{\"pass\":true,\"cases\":" << cases << ",\"placements\":" << placements
                  << ",\"checks\":" << checks << "}\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
