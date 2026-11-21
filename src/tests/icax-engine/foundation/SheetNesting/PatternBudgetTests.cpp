#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include "SheetNestingConstructionTrace.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }

    Part Polygon(std::string id, std::vector<Point> points, std::size_t quantity)
    {
        Part part;
        part.ID = std::move(id);
        part.Geometry.Outer = std::move(points);
        part.Quantity = quantity;
        part.FreeRotation = false;
        part.AllowedAnglesDegrees = {0, 180};
        return part;
    }

    Problem Library(bool manyDirections)
    {
        Problem problem;
        problem.Tolerance = 1e-7;
        problem.Stocks = {{"sheet", 100, 100, 1, 1, ""}};
        problem.Parts = {
            Polygon("channel", {{-7,-2.5},{-5,-2.5},{-5,.5},{5,.5},
                {5,-2.5},{7,-2.5},{7,2.5},{-7,2.5}}, 15),
            Polygon("diamond", {{-6,0},{0,-6},{6,0},{0,6}}, 7),
            Polygon("stepped", {{-5.5,-1},{.5,-1},{.5,-3},{1.5,-3},{5.5,1},
                {5.5,3},{2.5,3},{2.5,0},{-3.5,0},{-3.5,3},{-5.5,3}}, 9),
            Polygon("cross", {{-3,-1},{-1,-1},{-1,-3},{1,-3},{1,-1},{3,-1},
                {3,1},{1,1},{1,3},{-1,3},{-1,1},{-3,1}}, 12)
        };
        if (manyDirections)
        {
            auto& angles = problem.Parts[1].AllowedAnglesDegrees;
            angles.clear();
            for (int i = 0; i < 32; ++i) angles.push_back(i * 11.25);
        }
        return problem;
    }

    std::size_t SingletonCount(const Problem& problem,
        OrientationProposalPolicy policy = OrientationProposalPolicy::AllowedReflections)
    {
        std::size_t count = 0;
        for (const auto& part : problem.Parts)
            if (part.Quantity) count += Detail::CandidateOrientations(part, problem.Tolerance, policy).size();
        return count;
    }

    void Fingerprint(std::uint64_t& value,const std::vector<Detail::Pattern>& patterns)
    {
        const auto word=[&](std::uint64_t item)
        {for(unsigned byte=0;byte<8;++byte){value^=item&255;value*=1099511628211ULL;item>>=8;}};
        word(patterns.size());
        for(const auto& pattern:patterns)
        {
            word(static_cast<std::uint64_t>(pattern.Kind));word(pattern.Members.size());
            word(std::bit_cast<std::uint64_t>(pattern.Width));word(std::bit_cast<std::uint64_t>(pattern.Height));
            word(std::bit_cast<std::uint64_t>(pattern.MaterialArea));
            for(const auto& member:pattern.Members)
            {
                word(member.PartIndex);word(std::bit_cast<std::uint64_t>(member.Transform.X));
                word(std::bit_cast<std::uint64_t>(member.Transform.Y));word(std::bit_cast<std::uint64_t>(member.Transform.AngleDegrees));
            }
        }
    }

    void Verify(const Problem& problem, const Options& options, const std::vector<Detail::Pattern>& patterns)
    {
        Require(patterns.size() <= SingletonCount(problem, options.OrientationProposals) + options.MaxPatterns,
            "preparation exceeded the additional pattern cap");
        for (const auto& pattern : patterns)
        {
            Require(!pattern.Members.empty() && pattern.Members.size() <= options.MaxPatternMembers,
                "preparation exceeded the original member cap");
            Require(Detail::HasSinglePartType(pattern.Members),
                "ordinary preparation combined distinct original part indices");
            std::map<std::size_t, std::size_t> demand;
            double material = 0;
            for (const auto& member : pattern.Members)
            {
                Require(member.PartIndex < problem.Parts.size(), "preparation changed an original part index");
                const auto& part = problem.Parts[member.PartIndex];
                Require(++demand[member.PartIndex] <= part.Quantity, "preparation exceeded original demand");
                Require(Detail::PoseAllowed(part, member.Transform, problem.Tolerance),
                    "preparation widened the original angle or reflection permission");
                Require(options.OrientationProposals != OrientationProposalPolicy::OriginalOnly || !member.Transform.Mirrored,
                    "original-only preparation proposed a reflected member");
                material += Detail::Area(part.Geometry);
            }
            Require(std::abs(material - pattern.MaterialArea) <= 1e-9,
                "preparation changed the original material area");
        }
    }

    bool DenseChannelPair(const Problem& problem, const std::vector<Detail::Pattern>& patterns,
        const std::string& firstID = "channel", const std::string& secondID = "channel")
    {
        return std::any_of(patterns.begin(), patterns.end(), [&](const auto& pattern)
        {
            if (pattern.Members.size() != 2 || pattern.MaterialArea / (pattern.Width * pattern.Height) < .7)
                return false;
            const auto& a = pattern.Members[0];
            const auto& b = pattern.Members[1];
            const auto& first = problem.Parts[a.PartIndex];
            const auto& second = problem.Parts[b.PartIndex];
            if (!((first.ID == firstID && second.ID == secondID) ||
                  (first.ID == secondID && second.ID == firstID))) return false;
            const auto firstGeometry = AxisSafety::Make(first.Geometry, a.Transform);
            const auto secondGeometry = AxisSafety::Make(second.Geometry, b.Transform);
            return AxisSafety::Inside(firstGeometry, pattern.Width, pattern.Height, 0) &&
                AxisSafety::Inside(secondGeometry, pattern.Width, pattern.Height, 0) &&
                AxisSafety::Pair(firstGeometry, secondGeometry, 0, false, problem.Tolerance) ==
                    AxisSafety::Verdict::Safe;
        });
    }

    void SingletonNormalizationMatchesReference()
    {
        auto problem = Library(true);
        auto holed = Polygon("holed", {{-9.125,-3.75},{7.625,-3.75},{7.625,8.5},{-9.125,8.5}}, 2);
        holed.Geometry.Holes = {{{-1,-1},{1,-1},{1,1},{-1,1}}};
        holed.AllowedAnglesDegrees = {0, 90, 180, 270, -45, 405};
        problem.Parts.push_back(holed);
        Part circle;
        circle.ID = "circle";
        circle.Geometry.Radius = 3.25;
        circle.Geometry.Center = {-7.125, 2.625};
        circle.FreeRotation = false;
        circle.AllowedAnglesDegrees = {0, 13, 90, 271};
        problem.Parts.push_back(circle);
        auto inactive = problem.Parts.front();
        inactive.ID = "inactive";
        inactive.Quantity = 0;
        problem.Parts.push_back(inactive);
        problem.Parts[2].FreeRotation = true;
        const auto original = problem;

        std::vector<std::vector<double>> angles(problem.Parts.size());
        for (std::size_t i = 0; i < problem.Parts.size(); ++i)
            if (problem.Parts[i].Quantity)
                angles[i] = Detail::CandidateAngles(problem.Parts[i], problem.Tolerance);
        std::vector<Detail::Pattern> expected;
        for (std::size_t angleIndex = 0;; ++angleIndex)
        {
            bool any = false;
            for (std::size_t i = 0; i < angles.size(); ++i)
            {
                if (angleIndex >= angles[i].size()) continue;
                any = true;
                Detail::Pattern pattern;
                pattern.Members.push_back({i, {0, 0, angles[i][angleIndex]}});
                Detail::NormalizePattern(pattern, problem);
                expected.push_back(std::move(pattern));
            }
            if (!any) break;
        }
        Options options;
        options.EnableCombinations = false;
        const auto patterns = Detail::BuildPatterns(problem, options, {});
        Require(patterns.size() == expected.size(), "singleton preparation changed the original direction count");
        const auto sameNumber = [](double a, double b)
        { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); };
        for (std::size_t i = 0; i < patterns.size(); ++i)
        {
            const auto& actual = patterns[i];
            const auto& reference = expected[i];
            Require(actual.Members.size() == 1 && actual.Members.front().PartIndex == reference.Members.front().PartIndex,
                "singleton preparation changed the round-robin specification order");
            const auto pose = actual.Members.front().Transform;
            const auto referencePose = reference.Members.front().Transform;
            Require(sameNumber(pose.X, referencePose.X) && sameNumber(pose.Y, referencePose.Y) &&
                sameNumber(pose.AngleDegrees, referencePose.AngleDegrees) &&
                sameNumber(actual.Width, reference.Width) && sameNumber(actual.Height, reference.Height) &&
                sameNumber(actual.MaterialArea, reference.MaterialArea) && actual.Kind == reference.Kind,
                "cached singleton classification changed NormalizePattern output");
        }
        const auto sameRing = [&](const std::vector<Point>& a, const std::vector<Point>& b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [&](Point first, Point second)
            { return sameNumber(first.X, second.X) && sameNumber(first.Y, second.Y); });
        };
        Require(problem.Parts.size() == original.Parts.size(), "singleton preparation changed the source specifications");
        for (std::size_t i = 0; i < problem.Parts.size(); ++i)
        {
            const auto& part = problem.Parts[i];
            const auto& source = original.Parts[i];
            Require(part.ID == source.ID && part.Quantity == source.Quantity && part.Material == source.Material &&
                part.FreeRotation == source.FreeRotation && part.AllowedAnglesDegrees == source.AllowedAnglesDegrees &&
                sameNumber(part.Geometry.Radius, source.Geometry.Radius) &&
                sameNumber(part.Geometry.Center.X, source.Geometry.Center.X) &&
                sameNumber(part.Geometry.Center.Y, source.Geometry.Center.Y) &&
                sameRing(part.Geometry.Outer, source.Geometry.Outer) && part.Geometry.Holes.size() == source.Geometry.Holes.size(),
                "singleton preparation changed an original specification or contour");
            for (std::size_t hole = 0; hole < part.Geometry.Holes.size(); ++hole)
                Require(sameRing(part.Geometry.Holes[hole], source.Geometry.Holes[hole]),
                    "singleton preparation changed an original hole contour");
        }
    }

    void LateComplementaryDirectionReceivesContactBudget()
    {
        Problem problem;problem.Tolerance=1e-7;
        problem.Stocks={{"sheet",7.5,7.5,1,1,""}};
        auto part=Polygon("turning-half",{{0,0},{6,0},{0,6}},2);part.AllowedAnglesDegrees.clear();
        for(int angle=0;angle<180;angle+=10)part.AllowedAnglesDegrees.push_back(angle);
        part.AllowedAnglesDegrees.push_back(180);problem.Parts={part};
        Options options;options.MaxPatternMembers=2;options.MaxPatterns=16;options.MaxPlacementAttempts=48;
        std::size_t attempts=0;const auto patterns=Detail::BuildPatterns(problem,options,{},&attempts);
        Verify(problem,options,patterns);
        Require(attempts<=options.MaxPlacementAttempts,"direction coverage exceeded its exact-validation allowance");
        Require(std::any_of(patterns.begin(),patterns.end(),[&](const auto& pattern)
        {
            if(pattern.Members.size()!=2||pattern.MaterialArea/(pattern.Width*pattern.Height)<.99)return false;
            const auto& first=pattern.Members[0];const auto& second=pattern.Members[1];
            return first.PartIndex==second.PartIndex&&
                AxisSafety::Pair(AxisSafety::Make(problem.Parts[first.PartIndex].Geometry,first.Transform),
                    AxisSafety::Make(problem.Parts[second.PartIndex].Geometry,second.Transform),0,false,
                    problem.Tolerance)==AxisSafety::Verdict::Safe;
        }),"a later legal complementary direction was starved by earlier box overlays");
    }

    void PerSpecificationPreparationSummary()
    {
        const auto problem=Library(false);Options options;options.MaxPlacementAttempts=2048;
        std::vector<ConstructionTraceEvent> summaries;
        options.ConstructionTrace=[&](const auto& event){if(event.Source=="preparation.pair-summary")summaries.push_back(event);};
        const auto patterns=Detail::BuildPatterns(problem,options,{});Verify(problem,options,patterns);
        Require(summaries.size()==problem.Parts.size(),"ordinary preparation omitted a source specification summary");
        const auto channel=std::find_if(summaries.begin(),summaries.end(),[](const auto& event){return event.PartIndex==0;});
        Require(channel!=summaries.end()&&channel->Members.size()==2&&channel->Width>0&&channel->Height>0,
            "ordinary summary omitted the best certified real self pair");
        Require(std::any_of(channel->Measures.begin(),channel->Measures.end(),[](const auto& value)
            {return value.first=="preworkVisits"&&value.second>0;}),"ordinary summary lost work-list preparation coverage");
        auto bounded=problem;while(bounded.Parts.size()<300)bounded.Parts.push_back(problem.Parts[0]);
        options.EnableCombinations=false;summaries.clear();Detail::BuildPatterns(bounded,options,{});
        Require(summaries.size()==257&&summaries.back().Reason=="summary-specification-cap"&&summaries.back().Truncated,
            "preparation summaries exceeded their cap or hid omitted source specifications");
    }
    void OrdinaryRetentionPrefersUtilizationOverFilledKind()
    {
        Problem problem;problem.Tolerance=1e-7;problem.Stocks={{"sheet",100,100,1}};
        auto first=Polygon("skew-triangle",{{0,0},{6,0},{8,4}},2);
        problem.Parts={first};
        Options options;options.MaxPatternMembers=2;options.MaxPatterns=1;options.MaxPlacementAttempts=4096;
        const auto low=Detail::BuildPatterns(problem,options,{});Verify(problem,options,low);
        const auto filled=std::find_if(low.begin(),low.end(),[](const auto& pattern)
            {return pattern.Members.size()==2&&pattern.Kind==ShapeKind::Parallelogram;});
        Require(filled!=low.end()&&filled->MaterialArea/(filled->Width*filled->Height)<.85,
            "ordinary density fixture did not construct its genuine low-AABB-fill basic carrier");
        auto notched=Polygon("notched-original",{{0,0},{10,0},{10,10},{6,10},{6,9},{5,9},{5,10},{0,10}},2);
        problem.Parts.push_back(notched);
        const auto patterns=Detail::BuildPatterns(problem,options,{});Verify(problem,options,patterns);
        const auto best=std::find_if(patterns.begin(),patterns.end(),[](const auto& pattern){return pattern.Members.size()==2;});
        Require(best!=patterns.end()&&best->Kind!=ShapeKind::Rectangle&&best->Kind!=ShapeKind::Parallelogram&&
            best->MaterialArea/(best->Width*best->Height)>.95,
            "a low-utilization filled basic carrier displaced the tighter irregular ordinary combination");
        const auto& a=best->Members[0];const auto& b=best->Members[1];
        Require(!Detail::ShapesConflict(problem.Parts[a.PartIndex].Geometry,a.Transform,
            problem.Parts[b.PartIndex].Geometry,b.Transform,0,problem.Tolerance)&&
            AxisSafety::Pair(AxisSafety::Make(problem.Parts[a.PartIndex].Geometry,a.Transform),
                AxisSafety::Make(problem.Parts[b.PartIndex].Geometry,b.Transform),0,false,
                problem.Tolerance)==AxisSafety::Verdict::Safe,
            "ordinary utilization priority bypassed original-material safety");
    }

    void SingleInstancesNeverEnterPairPreparation()
    {
        auto problem=Library(false);problem.Parts.resize(1);problem.Parts.front().Quantity=1;
        auto other=problem.Parts.front();other.ID="same-contour-distinct-original";problem.Parts.push_back(other);
        Options options;options.MaxPlacementAttempts=4096;
        std::vector<ConstructionTraceEvent> summaries;
        options.ConstructionTrace=[&](const auto& event){if(event.Source=="preparation.pair-summary")summaries.push_back(event);};
        std::size_t attempts=99;const auto patterns=Detail::BuildPatterns(problem,options,{},&attempts);
        Verify(problem,options,patterns);
        Require(attempts==0&&patterns.size()==SingletonCount(problem),
            "distinct single instances entered geometric combination trials or lent each other inventory");
        Require(summaries.size()==problem.Parts.size()&&std::all_of(summaries.begin(),summaries.end(),[](const auto& event)
        {
            return event.Attempts==0&&event.Reason=="insufficient-self-demand"&&
                std::all_of(event.Measures.begin(),event.Measures.end(),[](const auto& value)
                    {return (value.first!="preworkVisits"&&value.first!="entered"&&value.first!="exact")||value.second==0;});
        }),"quantity-one originals entered self-combination prework");
    }

    void SelfCombinationsRetainDepthAndPosePermissions()
    {
        Problem problem;problem.Tolerance=1e-7;problem.Stocks={{"sheet",100,100,1}};
        auto rectangle=Polygon("repeat-four",{{0,0},{4,0},{4,3},{0,3}},4);rectangle.AllowedAnglesDegrees={0};
        problem.Parts={rectangle};Options options;options.MaxPatterns=64;options.MaxPlacementAttempts=8192;
        const auto patterns=Detail::BuildPatterns(problem,options,{});Verify(problem,options,patterns);
        for(std::size_t depth=2;depth<=4;++depth)
        {
            const auto found=std::find_if(patterns.begin(),patterns.end(),[&](const auto& pattern){return pattern.Members.size()==depth;});
            Require(found!=patterns.end(),"same-original preparation lost a two-, three- or four-member growth layer");
            for(std::size_t i=0;i<found->Members.size();++i)
            for(std::size_t j=0;j<i;++j)
                Require(AxisSafety::Pair(AxisSafety::Make(rectangle.Geometry,found->Members[i].Transform),
                    AxisSafety::Make(rectangle.Geometry,found->Members[j].Transform),0,false,problem.Tolerance)==AxisSafety::Verdict::Safe,
                    "same-original growth bypassed real material safety");
        }
        auto triangle=Polygon("mirror-complement",{{0,0},{6,0},{0,6}},2);
        triangle.AllowedAnglesDegrees={0,90};triangle.AllowMirror=true;problem.Parts={triangle};
        const auto reflected=Detail::BuildPatterns(problem,options,{});Verify(problem,options,reflected);
        Require(std::any_of(reflected.begin(),reflected.end(),[&](const auto& pattern)
        {
            return pattern.Members.size()==2&&pattern.MaterialArea/(pattern.Width*pattern.Height)>.99&&
                std::any_of(pattern.Members.begin(),pattern.Members.end(),[](const auto& member){return member.Transform.Mirrored;})&&
                AxisSafety::Pair(AxisSafety::Make(triangle.Geometry,pattern.Members[0].Transform),
                    AxisSafety::Make(triangle.Geometry,pattern.Members[1].Transform),0,false,problem.Tolerance)==AxisSafety::Verdict::Safe;
        }),"same-original preparation lost its legal reflected complementary pair");
        options.OrientationProposals=OrientationProposalPolicy::OriginalOnly;
        const auto originalOnly=Detail::BuildPatterns(problem,options,{});Verify(problem,options,originalOnly);
        Require(originalOnly.size()>=SingletonCount(problem,options.OrientationProposals),
            "original-only self-combination search removed original directions");
        options.OrientationProposals=OrientationProposalPolicy::AllowedReflections;problem.Parts.front().AllowMirror=false;
        Verify(problem,options,Detail::BuildPatterns(problem,options,{}));
    }
}

int main()
{
    try
    {
        SingletonNormalizationMatchesReference();
        LateComplementaryDirectionReceivesContactBudget();
        PerSpecificationPreparationSummary();
        OrdinaryRetentionPrefersUtilizationOverFilledKind();
        SingleInstancesNeverEnterPairPreparation();
        SelfCombinationsRetainDepthAndPosePermissions();
        std::cout << "PASS cached singleton classification preserves per-angle normalization and source geometry\n";
        Options options;
        options.MaxPlacementAttempts = 2048;
        std::size_t permutations = 0, missed = 0;
        std::uint64_t patternFingerprint=14695981039346656037ULL;
        for (const bool manyDirections : {false, true})
        {
            const auto original = Library(manyDirections);
            std::array<std::size_t, 4> order{0, 1, 2, 3};
            do
            {
                auto problem = original;
                for (std::size_t i = 0; i < order.size(); ++i) problem.Parts[i] = original.Parts[order[i]];
                const auto patterns = Detail::BuildPatterns(problem, options, {});
                Verify(problem, options, patterns);
                Fingerprint(patternFingerprint,patterns);
                if (!DenseChannelPair(problem, patterns))
                {
                    ++missed;
                    std::cerr << "missing channel pair, manyDirections=" << manyDirections << ", order=";
                    for (const auto index : order) std::cerr << index;
                    std::cerr << '\n';
                }
                ++permutations;
            }
            while (std::next_permutation(order.begin(), order.end()));
        }
        Require(!missed, "finite preparation starved a later self-complementary specification");
        std::cout << "PASS same-specification pair coverage for " << permutations
            << " input permutations and direction counts\n";
        std::cout<<"PATTERN-FINGERPRINT permutations="<<permutations<<" fnv64="<<std::hex<<patternFingerprint<<std::dec<<'\n';

        auto mixed = Library(true);
        mixed.Parts[0].Quantity = 1;
        auto other = mixed.Parts[0];
        other.ID = "other-channel";
        mixed.Parts.push_back(other);
        auto patterns = Detail::BuildPatterns(mixed, options, {});
        Verify(mixed, options, patterns);
        Require(!DenseChannelPair(mixed, patterns, "channel", "other-channel")&&
            std::none_of(patterns.begin(),patterns.end(),[&](const auto& pattern)
                {return pattern.Members.size()>1&&(pattern.Members.front().PartIndex==0||pattern.Members.front().PartIndex==mixed.Parts.size()-1);})&&
            std::any_of(patterns.begin(),patterns.end(),[](const auto& pattern){return pattern.Members.size()>1;}),
            "single-instance specifications borrowed cross-type inventory or blocked other self-combinations");
        std::cout << "PASS distinct single-instance specifications never combine while repeated types remain eligible\n";

        Problem filled;
        filled.Stocks = {{"sheet", 100, 100, 1, 1, ""}};
        filled.Parts = {
            Polygon("outside", {{0,0},{10,0},{10,2},{2,2},{2,10},{0,10}}, 1),
            Polygon("inside", {{0,0},{8,0},{8,8},{0,8}}, 1),
            Library(false).Parts[0]
        };
        auto filledOptions=options;
        std::vector<ConstructionTraceEvent> rareSummaries;
        filledOptions.ConstructionTrace=[&](const auto& event)
            {if(event.Source=="preparation.pair-summary"&&event.PartIndex<2)rareSummaries.push_back(event);};
        patterns = Detail::BuildPatterns(filled, filledOptions, {});
        Verify(filled, options, patterns);
        Require(std::none_of(patterns.begin(), patterns.end(), [&](const auto& pattern)
        {
            return pattern.Members.size()>1&&pattern.Members.front().PartIndex<2;
        })&&DenseChannelPair(filled,patterns),
            "complementary rare originals entered cross-type preparation or displaced a repeated type");
        Require(rareSummaries.size()==2&&std::all_of(rareSummaries.begin(),rareSummaries.end(),[](const auto& event)
        {
            return event.Attempts==0&&event.Reason=="insufficient-self-demand"&&
                std::all_of(event.Measures.begin(),event.Measures.end(),[](const auto& value)
                    {return (value.first!="preworkVisits"&&value.first!="entered"&&value.first!="exact")||value.second==0;});
        }),"rare complementary originals entered contact prework or geometric cross-type trials");
        for(std::size_t index=0;index<2;++index)
            Require(std::any_of(patterns.begin(),patterns.end(),[&](const auto& pattern)
                {return pattern.Members.size()==1&&pattern.Members.front().PartIndex==index;}),
                "forbidding cross-type combinations removed an original singleton");
        std::cout << "PASS cross-type geometric complements stay separate and retain singleton candidates\n";

        auto fixed = Library(false);
        fixed.Parts[0].AllowedAnglesDegrees = {0};
        patterns = Detail::BuildPatterns(fixed, options, {});
        Verify(fixed, options, patterns);
        Require(!DenseChannelPair(fixed, patterns), "fixed-angle input acquired a half-turn interlock");
        std::cout << "PASS finite angle domains remain unchanged\n";

        const auto problem = Library(true);
        auto limited = options;
        limited.MaxPatterns = 1;
        patterns = Detail::BuildPatterns(problem, limited, {});
        Verify(problem, limited, patterns);
        Require(patterns.size() >= SingletonCount(problem), "small pattern cap removed original directions");
        limited = options;
        limited.MaxPlacementAttempts = 0;
        patterns = Detail::BuildPatterns(problem, limited, {});
        Verify(problem, limited, patterns);
        Require(patterns.size() == SingletonCount(problem), "zero trial budget performed optional combinations");
        limited = options;
        limited.MaxPatternMembers = 1;
        patterns = Detail::BuildPatterns(problem, limited, {});
        Verify(problem, limited, patterns);
        Require(patterns.size() == SingletonCount(problem), "member cap one performed optional combinations");
        limited = options;
        limited.EnableCombinations = false;
        patterns = Detail::BuildPatterns(problem, limited, {});
        Require(patterns.size() == SingletonCount(problem), "disabled combination search still produced motifs");
        std::cout << "PASS original pattern, attempt, member and enable limits remain effective\n";

        Require(Detail::BuildPatterns(problem, options, [] { return true; }).empty(),
            "immediate preparation cancellation was ignored");
        std::size_t polls = 0;
        patterns = Detail::BuildPatterns(problem, options, [&] { return ++polls >= 150; });
        Verify(problem, options, patterns);
        Require(polls >= 150, "preparation did not observe mid-search cancellation");
        std::cout << "PASS immediate and in-progress cancellation remain effective\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
