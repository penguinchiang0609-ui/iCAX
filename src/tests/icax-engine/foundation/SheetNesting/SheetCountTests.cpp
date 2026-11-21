#include "SheetNestingSheetCount.h"
#include "NestingSheetCountSchedule.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Part Box(std::string id,double width,double height,std::size_t quantity=1)
    {
        Part p;p.ID=std::move(id);p.Quantity=quantity;p.Material="material";p.FreeRotation=false;p.AllowedAnglesDegrees={0};
        p.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return p;
    }
    Problem Task(double width,double height,std::size_t boards)
    {Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"stock",width,height,boards,1,"material"});return p;}
    bool Has(const Result& r,const char* text)
    {return std::any_of(r.Diagnostics.begin(),r.Diagnostics.end(),[&](const std::string& note){return note.find(text)!=std::string::npos;});}
    using Group = std::set<SheetCountPartition::Identity>;
    std::set<Group> PartitionGroups(const SheetCountPartition& partition)
    {
        std::set<Group> groups;
        for (const auto& group : partition.Groups) groups.emplace(group.begin(), group.end());
        return groups;
    }
    std::set<Group> ResultGroups(const Result& result)
    {
        std::set<Group> groups;
        for (const auto& board : result.Layouts)
        {
            Group members;
            for (const auto& part : board.Placements) members.emplace(part.PartID, part.Instance);
            groups.insert(std::move(members));
        }
        return groups;
    }
}
int main()try
{
    // Scheduling regressions are deterministic; solver wall-clock luck does
    // not decide whether a checked incumbent survives a retry or cancellation.
    using Detail::SheetCountOutcome;using Detail::SheetCountSchedule;
    SheetCountSchedule sequence(3,8);
    auto next=sequence.Next();Require(next&&next->Boards==3&&!next->Revisit,"initial fixed-count order did not start at the generic lower bound");
    sequence.Observe(*next,SheetCountOutcome::Unknown);
    next=sequence.Next();Require(next&&next->Boards==4&&!next->Revisit,"initial unknown count blocked finding a complete upper bound");
    sequence.Observe(*next,SheetCountOutcome::Complete);
    next=sequence.Next();Require(next&&next->Boards==3&&next->Revisit&&next->PendingInRound==1,"complete upper bound did not trigger revisiting the smaller unknown count");
    sequence.Observe(*next,SheetCountOutcome::Unknown);
    Require(sequence.Incumbent()==4&&!sequence.Next(false)&&sequence.Incumbent()==4,"budget exhaustion discarded the complete incumbent");
    Require(!sequence.Next(true,true)&&sequence.Incumbent()==4,"cancellation discarded the complete incumbent");
    next=sequence.Next();Require(next&&next->Boards==3&&next->Round==2,"another unresolved-count attempt did not advance its deterministic retry round");
    sequence.Observe(*next,SheetCountOutcome::Complete);
    Require(sequence.AtAreaLowerBound()&&sequence.Incumbent()==3&&!sequence.Next(),"reaching the material lower bound did not terminate immediately");

    SheetCountSchedule fair(2,9);
    next=fair.Next();fair.Observe(*next,SheetCountOutcome::Unknown);
    next=fair.Next();fair.Observe(*next,SheetCountOutcome::Unknown);
    next=fair.Next();fair.Observe(*next,SheetCountOutcome::Complete);
    next=fair.Next();Require(next&&next->Boards==2&&next->PendingInRound==2,"revisit budget did not include each unresolved smaller count");
    fair.Observe(*next,SheetCountOutcome::InfeasibleOnLattice);
    next=fair.Next();Require(next&&next->Boards==3&&next->PendingInRound==1,"proven lattice failure was retried or the next unresolved count lost its budget share");
    fair.Observe(*next,SheetCountOutcome::Complete);
    Require(fair.Incumbent()==3&&!fair.Next()&&!fair.AtAreaLowerBound(),"lattice-only rejection was confused with a material lower-bound proof");

    SheetCountSettings settings;settings.Workers=1;settings.SecondsPerBoardCount=1;settings.CoordinateScale=10;
    Options options;options.TimeLimitSeconds=10;options.MaxPlacementAttempts=1000;
    const auto solve=[&](const Problem& p){return MakeSheetCountStrategy(settings)()->Run(p,options);};

    auto demand=Task(10,10,4);demand.Parts={Box("a",6,10,2),Box("b",4,10,2)};
    const auto packed=solve(demand);const auto check=Validate(demand,packed);
    Require(check.Valid&&check.Complete&&check.PlacedCount==4&&packed.Layouts.size()==2,"cold constructor did not expand original quantities at the area lower bound");
    Require(packed.GeometryValidated&&Has(packed,"prior-layout-input=0")&&Has(packed,"common-edge-objective=0")&&Has(packed,"consolidation-objective=0"),"missing cold construction contract");
    Require(Has(packed,"native current-input NFP initializer"),"native board-count search must build its own original-only initializer");
    std::set<std::pair<std::string,std::size_t>> identities;
    for(const auto& board:packed.Layouts)for(const auto& q:board.Placements)identities.emplace(q.PartID,q.Instance);
    Require(identities==std::set<std::pair<std::string,std::size_t>>{{"a",0},{"a",1},{"b",0},{"b",1}},"original instance identities changed");
    auto symmetric=demand;for(auto& p:symmetric.Parts)p.AllowedAnglesDegrees={180,90,270,0};
    Require(Validate(symmetric,solve(symmetric)).Complete,"equivalent-quarter reduction or global half-turn symmetry removed a valid copy layout");

    auto renamed=demand;renamed.Stocks[0].ID="different-stock";renamed.Parts[0].ID="arbitrary-key";renamed.Parts[1].ID="another-key";std::reverse(renamed.Parts.begin(),renamed.Parts.end());
    const auto renamedResult=solve(renamed);
    Require(Validate(renamed,renamedResult).Complete&&renamedResult.Layouts.size()==2,"task IDs or original order changed cold packing capability");

    auto rotated=Task(2.4,4.4,1);rotated.EdgeMargin=.2;rotated.Parts={Box("rotated",4,2)};rotated.Parts[0].AllowedAnglesDegrees={90};
    const auto turned=solve(rotated);
    Require(Validate(rotated,turned).Complete&&turned.Layouts.size()==1&&turned.Layouts[0].Placements[0].Transform.AngleDegrees==90,"rectangular stock lost a permitted 90 degree rotation or edge margins");

    auto gap=Task(10.4,5.4,2);gap.EdgeMargin=.2;gap.PartGap=.2;gap.Parts={Box("copies",5,5,2)};
    auto spaced=solve(gap);
    Require(Validate(gap,spaced).Complete&&spaced.Layouts.size()==2,"uniform clearance was weakened to zero spacing");
    gap.AllowCommonEdges=true;const auto touching=solve(gap);
    Require(Validate(gap,touching).Complete&&touching.Layouts.size()==1,"real shared edge did not permit the smaller sheet count");

    auto corner=Task(10.4,10.4,1);corner.PartGap=.2;corner.EdgeMargin=.2;corner.AllowCommonEdges=true;corner.Parts={Box("corner",5,5,2)};
    Result pointOnly;pointOnly.State=Status::Complete;pointOnly.Layouts.push_back({"stock",0,10.4,10.4,
        {{"corner",0,{.2,.2,0},0,false},{"corner",1,{5.2,5.2,0},0,false}},50});
    Require(!Validate(corner,pointOnly).Valid,"point contact was treated as a common edge");

    auto triangles=Task(4.4,3.4,1);triangles.EdgeMargin=.2;triangles.PartGap=.2;triangles.AllowCommonEdges=true;
    auto triangle=Box("triangle",4,3,2);triangle.Geometry.Outer={{0,0},{4,0},{0,3}};triangle.AllowedAnglesDegrees={0,180};triangles.Parts={triangle};
    const auto halves=solve(triangles);
    Require(Validate(triangles,halves).Complete&&halves.Layouts.size()==1,"oblique common-edge material was replaced by its bounding box");
    Require(Has(halves,"interior-rectangle-types=1"),"oblique fixture did not exercise exact inner packing propagation");

    // The sloped input has an offset original origin and clockwise variants.
    // Preparing each case also verifies all four rotated inner rectangles'
    // corners against the exact integer original material half-planes.
    for(int quarter=0;quarter<4;++quarter)
    {
        auto shifted=Task(quarter%2?3.4:4.4,quarter%2?4.4:3.4,1);shifted.EdgeMargin=.2;
        auto sloped=Box("offset-sloped",4,3);sloped.Geometry.Outer={{-7.3,14.2},{-3.3,13.2},{-4.3,16.2}};
        sloped.AllowedAnglesDegrees={double(quarter*90)};
        if(quarter%2)std::reverse(sloped.Geometry.Outer.begin(),sloped.Geometry.Outer.end());shifted.Parts={sloped};
        const auto placed=solve(shifted);
        Require(Validate(shifted,placed).Complete&&placed.Layouts[0].Placements[0].Transform.AngleDegrees==quarter*90&&Has(placed,"interior-rectangle-types=1"),
            "signed half-plane division, winding, original offset or rotated inner rectangle lost a valid original");
    }

    auto tiny=Task(.1,.1,1);auto tinyTriangle=Box("tiny",.1,.1,2);
    tinyTriangle.Geometry.Outer={{0,0},{.1,0},{0,.1}};tinyTriangle.AllowedAnglesDegrees={0,180};tiny.Parts={tinyTriangle};
    const auto tinyResult=solve(tiny);
    Require(Validate(tiny,tinyResult).Complete&&tinyResult.Layouts.size()==1&&Has(tinyResult,"interior-skipped-types=1"),
        "absence of a positive grid inner rectangle rejected valid sharp material");
    auto noInterior=settings;noInterior.EnableInteriorRectangles=false;
    for(const auto& sameTask:std::vector<Problem>{demand,rotated,gap,triangles,tiny})
    {
        const auto enabled=solve(sameTask),disabled=MakeSheetCountStrategy(noInterior)()->Run(sameTask,options);
        Require(Validate(sameTask,enabled).Complete&&Validate(sameTask,disabled).Complete&&enabled.Layouts.size()==disabled.Layouts.size()&&Has(disabled,"interior-rectangles=0"),
            "redundant interior propagation changed small-task feasibility or minimum sheet count");
    }

    auto tooLarge=rotated;tooLarge.Stocks[0].Width=2.3;const auto unfit=solve(tooLarge);
    Require(!Validate(tooLarge,unfit).Complete&&unfit.Layouts.empty()&&Has(unfit,"no allowed original orientation"),"oversized original was silently shrunk or stock enlarged");
    auto unavailable=demand;unavailable.Stocks[0].Quantity=1;const auto shortStock=solve(unavailable);
    Require(shortStock.Layouts.empty()&&Has(shortStock,"exceeds available stock area"),"stock quantity was ignored");

    auto unsupported=demand;unsupported.Parts[0].FreeRotation=true;auto free=solve(unsupported);
    Require(free.State==Status::Failed&&Has(free,"unsupported free rotation"),"free rotation was silently rounded to quarter turns");
    unsupported=demand;unsupported.Parts[0].Geometry.Outer={{0,0},{6,0},{6,1},{1,1},{1,10},{0,10}};
    Require(Has(solve(unsupported),"unsupported nonconvex"),"concave material was silently replaced by its convex hull");
    unsupported=demand;unsupported.Stocks.push_back({"other",10,10,1,1,"material"});
    Require(Has(solve(unsupported),"unsupported multiple stocked types"),"multiple original stock types were silently discarded");
    auto fractional=demand;fractional.EdgeMargin=.25;
    Require(Has(solve(fractional),"not lossless"),"nonrepresentable process parameter was rounded on the search lattice");
    auto excessive=demand;excessive.Stocks[0].Width=100001;
    Require(Has(solve(excessive),"arithmetic scope"),"large exact-grid coordinates bypassed the declared integer arithmetic bound");

    auto changing = Task(10.4, 10.4, 3);
    changing.EdgeMargin = .2;
    changing.PartGap = .2;
    changing.AllowCommonEdges = true;
    changing.Stocks[0].ID = "current stock / arbitrary";
    changing.Parts = {Box("wide tall", 6, 10), Box("slim tall", 4, 10),
        Box("wide short", 6, 8), Box("slim short", 4, 8)};
    SheetCountPartition originalPartition;
    originalPartition.Groups = {{{"wide tall", 0}, {"slim tall", 0}},
        {{"wide short", 0}, {"slim short", 0}}};
    const auto perturb = [&](const Problem& problem, const SheetCountPartition& partition)
    {
        return MakeSheetCountPerturbationStrategy(partition, settings)()->Run(problem, options);
    };
    const auto changed = perturb(changing, originalPartition);
    const auto changedCheck = Validate(changing, changed);
    Require(changedCheck.Valid && changedCheck.Complete && changedCheck.PlacedCount == 4 && changed.Layouts.size() == 2,
        "fixed-count reconstruction lost an original, changed its geometry or used a different sheet count");
    Require(ResultGroups(changed) != PartitionGroups(originalPartition),
        "board labels changed but the original physical member partition remained intact");
    Require(Has(changed, "partition-change-constraint=1") && Has(changed, "partition-change=1") &&
        Has(changed, "minimum-count-proof=0") && !Has(changed, "complete at material-area lower bound"),
        "legal partition perturbation was confused with cold minimum-count construction");
    for (const auto& board : changed.Layouts)
        Require(!board.Placements.empty() && board.StockID == changing.Stocks[0].ID,
            "fixed-count perturbation returned an empty board or changed stock identity");
    auto relabelled = changed;
    std::reverse(relabelled.Layouts.begin(), relabelled.Layouts.end());
    for (std::size_t i = 0; i < relabelled.Layouts.size(); ++i) relabelled.Layouts[i].StockInstance = i;
    Require(ResultGroups(changed) == ResultGroups(relabelled),
        "test comparison treated a pure board-label permutation as a member change");

    // The balanced 80/40 partition is feasible. With exactly two sheets, the
    // distinct 60/40/20 originals can instead form 100/20. Twenty is also the
    // smallest demanded material area, proving this local objective's bound.
    auto concentration = Task(10.4, 10.4, 2);
    concentration.EdgeMargin = .2;
    concentration.PartGap = .2;
    concentration.AllowCommonEdges = true;
    concentration.Parts = {Box("large original", 6, 10), Box("medium original", 4, 10), Box("small original", 2, 10)};
    Result balanced;
    balanced.State = Status::Complete;
    balanced.Layouts = {
        {"stock", 0, 10.4, 10.4, {{"large original", 0, {.2, .2, 0}, 0, false},
            {"small original", 0, {6.2, .2, 0}, 0, false}}, 80},
        {"stock", 1, 10.4, 10.4, {{"medium original", 0, {.2, .2, 0}, 0, false}}, 40}
    };
    Require(Validate(concentration, balanced).Complete, "material-concentration reference partition is invalid");
    SheetCountPartition balancedPartition;
    balancedPartition.Groups = {{{"large original", 0}, {"small original", 0}}, {{"medium original", 0}}};
    const auto concentrated = perturb(concentration, balancedPartition);
    const auto concentratedCheck = Validate(concentration, concentrated);
    Require(concentratedCheck.Valid && concentratedCheck.Complete && concentratedCheck.PlacedCount == 3 &&
        concentrated.Layouts.size() == 2 && ResultGroups(concentrated) != PartitionGroups(balancedPartition),
        "material-concentration objective lost fixed-count partition exclusion or original geometry");
    const auto tail = std::min_element(concentrated.Layouts.begin(), concentrated.Layouts.end(),
        [](const Layout& a, const Layout& b) { return a.MaterialArea < b.MaterialArea; });
    Require(tail != concentrated.Layouts.end() && std::abs(tail->MaterialArea - 20) < 1e-9 && tail->MaterialArea < 40 &&
        Has(concentrated, "consolidation-objective=1") && Has(concentrated, "local-tail-objective-mm2=20") &&
        Has(concentrated, "local-tail-bound-mm2=20") && Has(concentrated, "minimum-count-proof=0"),
        "fixed-count perturbation retained balanced material instead of reaching the proven smaller tail objective");

    // Fixed neighborhood count and physical area lower bound are independent.
    // This task has material lower bound two, while this neighborhood retains
    // exactly three nonempty boards and must change their original membership.
    auto threeGroups = originalPartition;
    threeGroups.Groups = {{{"wide tall", 0}, {"slim tall", 0}}, {{"wide short", 0}}, {{"slim short", 0}}};
    const auto threeChanged = perturb(changing, threeGroups);
    Require(Validate(changing, threeChanged).Complete && threeChanged.Layouts.size() == 3 &&
        ResultGroups(threeChanged) != PartitionGroups(threeGroups) && Has(threeChanged, "area-lower-bound=2") &&
        Has(threeChanged, "fixed-count=3") && Has(threeChanged, "minimum-count-proof=0"),
        "fixed neighborhood count replaced the true material-area lower bound or failed to change membership");

    SheetCountPartition repeatedPartition;
    repeatedPartition.Groups = {{{"a", 0}, {"b", 0}}, {{"a", 1}, {"b", 1}}};
    const auto changedCopies = perturb(demand, repeatedPartition);
    const auto copyCheck = Validate(demand, changedCopies);
    Require(copyCheck.Valid && copyCheck.Complete && copyCheck.PlacedCount == 4 && changedCopies.Layouts.size() == 2 &&
        ResultGroups(changedCopies) != PartitionGroups(repeatedPartition),
        "copy-bin symmetry locked the original partition or repeated original quantities were not conserved");

    auto warmInput = Task(10.4, 10.4, 2);
    warmInput.EdgeMargin = .2;
    warmInput.PartGap = .2;
    warmInput.AllowCommonEdges = true;
    warmInput.Parts = {Box("warm wide / id", 6, 10), Box("warm slim quoted id", 4, 10)};
    Result warmLayout;
    warmLayout.State = Status::Complete;
    warmLayout.Layouts = {
        {"stock", 1, 10.4, 10.4, {{"warm wide / id", 0, {2.2, .2, 0}, 9, false}}, 60},
        {"stock", 0, 10.4, 10.4, {{"warm slim quoted id", 0, {.2, .2, 0}, 4, true}}, 40}
    };
    Require(Validate(warmInput, warmLayout).Complete, "invalid current-task warm source");
    SheetCountCurrentTaskHint warmHint{warmInput, warmLayout};
    auto reducedWarm = warmInput;
    reducedWarm.Stocks[0].Quantity = 1;
    const auto repaired = MakeSheetCountRepairStrategy(warmHint, settings)()->Run(reducedWarm, options);
    Require(Validate(reducedWarm, repaired).Complete && repaired.Layouts.size() == 1 &&
        Has(repaired, "current-task-hint=1") && Has(repaired, "hint-source-validated=1") &&
        Has(repaired, "hint-hard-locks=0") && Has(repaired, "hinted-position-instances=1") &&
        Has(repaired, "donor-hint-instances=1") && Has(repaired, "prior-layout-input=0"),
        "current-task hints did not guide a complete reduced-count original solve");
    const auto repairedWide = std::find_if(repaired.Layouts.front().Placements.begin(), repaired.Layouts.front().Placements.end(),
        [](const Placement& pose) { return pose.PartID == "warm wide / id"; });
    const auto repairedSlim = std::find_if(repaired.Layouts.front().Placements.begin(), repaired.Layouts.front().Placements.end(),
        [](const Placement& pose) { return pose.PartID == "warm slim quoted id"; });
    Require(repairedWide != repaired.Layouts.front().Placements.end() &&
        std::abs(repairedWide->Transform.X - 2.2) > warmInput.Tolerance &&
        repairedSlim != repaired.Layouts.front().Placements.end() && repairedSlim->GapFiller,
        "soft current-task guidance locked a retained pose or lost original-instance provenance");
    Require(warmHint.Layout.Layouts[0].Placements[0].Transform.X == 2.2 &&
        warmHint.Input.Stocks[0].Quantity == 2, "repair changed its source layout or task snapshot");

    auto roundedHint = warmHint;
    roundedHint.Layout.Layouts[0].Placements[0].Transform.X = 2.233;
    const auto roundedRepair = MakeSheetCountRepairStrategy(roundedHint, settings)()->Run(reducedWarm, options);
    Require(Validate(reducedWarm, roundedRepair).Complete && Has(roundedRepair, "rounded-hint-instances=1") &&
        roundedHint.Layout.Layouts[0].Placements[0].Transform.X == 2.233,
        "continuous current poses were discarded, modified or published as unverified rounded coordinates");
    auto aliasHint = warmHint;
    aliasHint.Input.Parts[0].AllowedAnglesDegrees = {0, 180};
    aliasHint.Layout.Layouts[0].Placements[0].Transform = {8.2, 10.2, 180};
    auto aliasTarget = reducedWarm;
    aliasTarget.Parts[0].AllowedAnglesDegrees = {0, 180};
    const auto aliasRepair = MakeSheetCountRepairStrategy(aliasHint, settings)()->Run(aliasTarget, options);
    Require(Validate(aliasTarget, aliasRepair).Complete && Has(aliasRepair, "canonical-rotation-hints=1") &&
        Has(aliasRepair, "hinted-position-instances=1"),
        "an equivalent allowed rotation used original pose coordinates as normalized model bounding minima");

    auto knownInput = Task(8.4, 5.4, 9);
    knownInput.EdgeMargin = .2;
    knownInput.PartGap = .2;
    knownInput.Parts = {Box("known repeated originals", 4, 5, 2)};
    Result knownLayout;
    knownLayout.State = Status::Complete;
    knownLayout.Layouts = {
        {"stock", 1, 8.4, 5.4, {{"known repeated originals", 0, {.2, .2, 0}, 0, false}}, 20},
        {"stock", 8, 8.4, 5.4, {{"known repeated originals", 1, {.2, .2, 0}, 0, false}}, 20}
    };
    const auto retainedKnown = MakeSheetCountRepairStrategy({knownInput, knownLayout}, settings)()->Run(knownInput, options);
    Require(Validate(knownInput, retainedKnown).Complete && retainedKnown.Layouts.size() == 2 &&
        Has(retainedKnown, "stock-count-upper-bound=2") && Has(retainedKnown, "validated-current-task-incumbent=1") &&
        Has(retainedKnown, "solver-rebuild=0"),
        "repair ignored actual current board count, rebuilt a known complete count or discarded the validated incumbent");

    auto firstComplete = settings;
    Require(!firstComplete.StopAfterFirstComplete, "complete-result handoff changed the default cold-search policy");
    firstComplete.StopAfterFirstComplete = true;
    const auto handedOff = MakeSheetCountStrategy(firstComplete)()->Run(knownInput, options);
    Require(Validate(knownInput, handedOff).Complete && handedOff.Layouts.size() == 2 &&
        Has(handedOff, "stop-after-first-complete=1") && Has(handedOff, "first-complete-handoff=1") &&
        Has(handedOff, "handed off for current-task guided repair"),
        "configured constructor did not hand off its first newly validated complete candidate");
    const auto improvedHint = MakeSheetCountRepairStrategy(warmHint, firstComplete)()->Run(warmInput, options);
    Require(Validate(warmInput, improvedHint).Complete && improvedHint.Layouts.size() == 1 &&
        Has(improvedHint, "first-complete-handoff=1"),
        "an existing complete hint stopped the repair before it searched a smaller feasible count");

    Result changingSource;
    changingSource.State = Status::Complete;
    changingSource.Layouts = {
        {changing.Stocks[0].ID, 0, 10.4, 10.4, {{"wide tall", 0, {.2, .2, 0}, 0, false},
            {"slim tall", 0, {6.2, .2, 0}, 0, false}}, 100},
        {changing.Stocks[0].ID, 1, 10.4, 10.4, {{"wide short", 0, {.2, .2, 0}, 0, false},
            {"slim short", 0, {6.2, .2, 0}, 0, false}}, 80}
    };
    SheetCountCurrentTaskHint changingHint{changing, changingSource};
    const auto guidedChange = MakeSheetCountPerturbationStrategy(originalPartition, changingHint, settings)()->Run(changing, options);
    Require(Validate(changing, guidedChange).Complete && ResultGroups(guidedChange) != PartitionGroups(originalPartition) &&
        Has(guidedChange, "hint-source-validated=1") && Has(guidedChange, "partition-change=1"),
        "current-task hint locked the old member partition during fixed-count perturbation");

    const auto rejectHint = [&](const SheetCountCurrentTaskHint& hint, const Problem& target)
    {
        const auto rejectedHint = MakeSheetCountRepairStrategy(hint, settings)()->Run(target, options);
        Require(rejectedHint.State == Status::InvalidInput && rejectedHint.Layouts.empty() &&
            Has(rejectedHint, "invalid current-task hint"), "invalid or cross-task layout guidance was accepted");
    };
    auto wrongHint = warmHint;
    wrongHint.Layout.Layouts[0].Placements[0].PartID = "unknown original";
    rejectHint(wrongHint, reducedWarm);
    wrongHint = warmHint;
    wrongHint.Layout.Layouts[0].Placements[0].Transform.AngleDegrees = 90;
    rejectHint(wrongHint, reducedWarm);
    wrongHint = warmHint;
    wrongHint.Layout.Layouts[0].Placements[0].Instance = 1;
    rejectHint(wrongHint, reducedWarm);
    wrongHint = warmHint;
    wrongHint.Input.Parts[0].Geometry.Outer[1].X += .1;
    rejectHint(wrongHint, reducedWarm);
    wrongHint = warmHint;
    ++wrongHint.Input.Parts[0].Quantity;
    rejectHint(wrongHint, reducedWarm);
    wrongHint = warmHint;
    wrongHint.Input.PartGap = .3;
    rejectHint(wrongHint, reducedWarm);
    wrongHint = warmHint;
    wrongHint.Input.Stocks[0].Width += 1;
    rejectHint(wrongHint, reducedWarm);
    auto increasedInventory = reducedWarm;
    increasedInventory.Stocks[0].Quantity = 3;
    rejectHint(warmHint, increasedInventory);
    auto mismatchedGroups = originalPartition;
    std::swap(mismatchedGroups.Groups[0][1], mismatchedGroups.Groups[1][1]);
    const auto groupMismatch = MakeSheetCountPerturbationStrategy(mismatchedGroups, changingHint, settings)()->Run(changing, options);
    Require(groupMismatch.State == Status::InvalidInput && groupMismatch.Layouts.empty(),
        "guided perturbation accepted a declared source partition different from the validated current layout");

    StrategyRegistry perturbations;
    perturbations.Register("current-window", MakeSheetCountPerturbationStrategy(originalPartition, settings));
    auto rawOptions = options;
    rawOptions.EnableBasicCompaction = false;
    const auto changedJob = RunJob({"current-window", "current-window", changing, rawOptions}, perturbations);
    Require(changedJob.Checked.Valid && changedJob.Checked.Complete &&
        ResultGroups(changedJob.Output) != PartitionGroups(originalPartition),
        "public job execution did not retain a valid changed original partition");

    const auto invalidPartition = [&](const SheetCountPartition& partition, const Problem& problem)
    {
        const auto result = perturb(problem, partition);
        Require(result.State == Status::InvalidInput && result.Layouts.empty() &&
            !Validate(problem, result).Complete && Has(result, "invalid original partition"),
            "invalid member partition was silently repaired or accepted as a complete reconstruction");
    };
    invalidPartition({}, changing);
    auto malformed = originalPartition;
    malformed.Groups.push_back({});
    invalidPartition(malformed, changing);
    malformed = originalPartition;
    malformed.Groups[1][1] = malformed.Groups[0][0];
    invalidPartition(malformed, changing);
    malformed = originalPartition;
    malformed.Groups[0][0].first = "missing identity";
    invalidPartition(malformed, changing);
    malformed = originalPartition;
    malformed.Groups[0][0].second = 1;
    invalidPartition(malformed, changing);
    malformed = originalPartition;
    malformed.Groups[1].pop_back();
    invalidPartition(malformed, changing);
    auto insufficientPartitionStock = changing;
    insufficientPartitionStock.Stocks[0].Quantity = 2;
    invalidPartition(threeGroups, insufficientPartitionStock);

    auto singletonStock = changing;
    singletonStock.Stocks[0].Quantity = 4;
    SheetCountPartition singletonGroups;
    for (const auto& part : singletonStock.Parts) singletonGroups.Groups.push_back({{part.ID, 0}});
    const auto immovable = perturb(singletonStock, singletonGroups);
    Require(immovable.State == Status::Partial && immovable.Layouts.empty() && Has(immovable, "noMutablePair"),
        "singleton-only partition was falsely reported as a successful exchange");
    auto oneSheet = Task(4, 4, 1);
    oneSheet.Parts = {Box("only-pair", 1, 1, 2)};
    SheetCountPartition oneGroup;
    oneGroup.Groups = {{{"only-pair", 0}, {"only-pair", 1}}};
    const auto noAlternative = perturb(oneSheet, oneGroup);
    Require(!Validate(oneSheet, noAlternative).Complete && noAlternative.Layouts.empty() &&
        Has(noAlternative, "partition-change-constraint=1") && Has(noAlternative, "status=INFEASIBLE"),
        "unchanged one-board membership escaped the fixed-count exclusion by changing only poses");

    auto cancelledOptions=options;cancelledOptions.Cancel=[]{return true;};
    const auto cancelled=MakeSheetCountStrategy(settings)()->Run(demand,cancelledOptions);
    Require(cancelled.State==Status::Cancelled&&cancelled.Layouts.empty(),"cancellation did not stop before construction");
    std::size_t preparationPolls=0;auto preparationCancel=options;preparationCancel.Cancel=[&]{return ++preparationPolls>=5;};
    const auto cancelledInterior=MakeSheetCountStrategy(settings)()->Run(triangles,preparationCancel);
    Require(cancelledInterior.State==Status::Cancelled&&cancelledInterior.Layouts.empty()&&Has(cancelledInterior,"interior-rectangle preparation"),"exact inner rectangle preparation ignored cancellation");
    auto exhaustedOptions=options;exhaustedOptions.TimeLimitSeconds=0;
    Require(MakeSheetCountStrategy(settings)()->Run(demand,exhaustedOptions).Layouts.empty(),"zero time budget still constructed a layout");
    const auto cancelledPerturbation = MakeSheetCountPerturbationStrategy(originalPartition, settings)()->Run(changing, cancelledOptions);
    Require(cancelledPerturbation.State == Status::Cancelled && cancelledPerturbation.Layouts.empty() &&
        Validate(changing, cancelledPerturbation).Valid,
        "partition perturbation ignored cancellation or lost original demand accounting");
    const auto exhaustedPerturbation = MakeSheetCountPerturbationStrategy(originalPartition, settings)()->Run(changing, exhaustedOptions);
    Require(exhaustedPerturbation.Layouts.empty() && !Validate(changing, exhaustedPerturbation).Complete,
        "partition perturbation constructed a layout after its time budget was exhausted");
    auto insufficientAttempts = options;
    insufficientAttempts.MaxPlacementAttempts = 3;
    const auto noVerificationBudget = MakeSheetCountPerturbationStrategy(originalPartition, settings)()->Run(changing, insufficientAttempts);
    Require(noVerificationBudget.Layouts.empty() && Has(noVerificationBudget, "insufficient complete-original validation budget"),
        "partition perturbation bypassed complete original-instance verification budgeting");
    bool rejected=false;auto invalid=settings;invalid.CoordinateScale=0;
    try{MakeSheetCountStrategy(invalid);}catch(const std::invalid_argument&){rejected=true;}
    Require(rejected,"invalid numerical settings were not rejected");
    std::cout<<"Cold sheet-count tests passed\n";return 0;
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
