// Reproducible original-polygon benchmark and isolated postprocessing trials.
#include "SheetNesting.h"
#include "../../../../tools/sheet-nesting/NativeConstructionTraceJson.h"
#include "SheetNestingBoundedContact.h"
#include "SheetNestingCompaction.h"
#include "SheetNestingRotationCompaction.h"
#include "SheetNestingDomainRepair.h"
#include "SheetNestingBeam.h"
#include "SheetNestingConvexConstruction.h"
#include "SheetNestingFeatureConstruction.h"
#include "SheetNestingFeatureGreedy.h"
#include "SheetNestingReassembly.h"
#include "SheetNestingLinearRepair.h"
#include "SheetNestingSeparationRepair.h"
#include "SheetNestingNfp.h"
#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingPlaneTaskConditions.h"
#ifdef ICAX_SHEET_NESTING_WITH_CP_SAT
#include "SheetNestingConstraint.h"
#endif
#include "SheetNestingDomainSearch.h"
#include "SheetNestingExactFill.h"
#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    using Clock=std::chrono::steady_clock;
    struct RotationObservation
    {
        Result Construction;
        Result BeforeRotation;
        double ConstructorSeconds = 0;
        bool Constructed = false;
        bool RotationStarted = false;
    };
    class ObservedConstructor final : public IStrategy
    {
        std::unique_ptr<IStrategy> Constructor;
        std::shared_ptr<RotationObservation> Observation;
        double TimeLimit;
        std::size_t AttemptLimit;
    public:
        ObservedConstructor(std::unique_ptr<IStrategy> constructor,
            std::shared_ptr<RotationObservation> observation, double timeLimit, std::size_t attemptLimit)
            : Constructor(std::move(constructor)), Observation(std::move(observation)), TimeLimit(timeLimit), AttemptLimit(attemptLimit) {}
        Result Run(const Problem& input, const Options& options) override
        {
            auto settings = options;
            settings.TimeLimitSeconds = std::min(settings.TimeLimitSeconds, TimeLimit);
            settings.MaxPlacementAttempts = std::min(settings.MaxPlacementAttempts, AttemptLimit);
            const auto start = Clock::now();
            auto result = Constructor->Run(input, settings);
            Observation->Construction = result;
            Observation->Constructed = true;
            Observation->ConstructorSeconds = std::chrono::duration<double>(Clock::now() - start).count();
            return result;
        }
    };
    class ObservedRotation final : public IPostProcessor
    {
        std::unique_ptr<IPostProcessor> Rotation;
        std::shared_ptr<RotationObservation> Observation;
    public:
        explicit ObservedRotation(std::shared_ptr<RotationObservation> observation)
            : Rotation(MakeRotationCompactionStage().Factory()), Observation(std::move(observation)) {}
        Result Run(const Problem& input, const Result& source, const Options& options) override
        {
            Observation->BeforeRotation = source;
            Observation->RotationStarted = true;
            return Rotation->Run(input, source, options);
        }
    };
    void Text(std::ostream& out,const std::string& s)
    {
        out<<'"';for(unsigned char c:s)switch(c){case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
        case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
        default:if(c<32)out<<"\\u00"<<"0123456789abcdef"[c/16]<<"0123456789abcdef"[c%16];else out<<c;}out<<'"';
    }
    void Points(std::ostream& out,const std::vector<Point>& points)
    {out<<'[';for(std::size_t i=0;i<points.size();++i){if(i)out<<',';out<<'['<<points[i].X<<','<<points[i].Y<<']';}out<<']';}
    Problem Read(const std::filesystem::path& path,double width)
    {
        std::ifstream in(path);std::string token;double height;std::size_t count;in>>token>>height>>token>>count;
        Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"sheet",width,height,1,1,"m"});
        for(std::size_t i=0;i<count;++i)
        {
            Part part;std::ostringstream id;id<<'P'<<std::setw(3)<<std::setfill('0')<<i;part.ID=id.str();part.Material="m";part.FreeRotation=false;
            std::size_t angles,vertices;in>>token>>part.Quantity>>token>>angles;part.AllowedAnglesDegrees.resize(angles);
            for(auto& angle:part.AllowedAnglesDegrees)in>>angle;in>>token>>vertices;part.Geometry.Outer.resize(vertices);
            for(auto& point:part.Geometry.Outer)in>>point.X>>point.Y;p.Parts.push_back(std::move(part));
        }
        if(!in)throw std::runtime_error("Invalid original ISP input.");return p;
    }
    JobResult ReadSeed(const Problem& p,const std::filesystem::path& file)
    {
        std::ifstream in(file);if(!in)throw std::runtime_error("Cannot read original placement seed.");
        JobResult job;auto& out=job.Output;const auto& stock=p.Stocks[0];
        out.Layouts.push_back({stock.ID,0,stock.Width,stock.Height,{},0});
        std::string line;std::vector<std::size_t> used(p.Parts.size());
        while(std::getline(in,line))
        {
            if(line.empty())continue;Placement x;std::istringstream row(line);
            if(!(row>>x.PartID>>x.Instance>>x.Transform.X>>x.Transform.Y>>x.Transform.AngleDegrees))
                throw std::runtime_error("Invalid placement seed row.");
            std::string extra;if(row>>extra)
            {
                if(extra!="0"&&extra!="1")throw std::runtime_error("Invalid placement seed mirror permission.");
                x.Transform.Mirrored=extra=="1";
                if(row>>extra)throw std::runtime_error("Unexpected placement seed column.");
            }
            const auto it=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& part){return part.ID==x.PartID;});
            if(it==p.Parts.end())throw std::runtime_error("Unknown original seed part.");
            ++used[static_cast<std::size_t>(it-p.Parts.begin())];out.Layouts[0].MaterialArea+=Detail::Area(it->Geometry);
            out.Layouts[0].Placements.push_back(std::move(x));
        }
        for(std::size_t i=0;i<p.Parts.size();++i)
        {if(used[i]>p.Parts[i].Quantity)throw std::runtime_error("Excess seed quantity.");
         if(used[i]<p.Parts[i].Quantity)out.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-used[i]});}
        out.State=out.Remaining.empty()?Status::Complete:Status::Partial;
        Detail::AssessCandidate(p,job);if(!job.Checked.Valid)throw std::runtime_error("Original seed fails native validation.");return job;
    }
    void ProblemJson(std::ostream& out,const Problem& p)
    {
        out<<"{\"partGap\":"<<p.PartGap<<",\"edgeMargin\":"<<p.EdgeMargin<<",\"tolerance\":"<<p.Tolerance
            <<",\"allowCommonEdges\":"<<(p.AllowCommonEdges?"true":"false")<<",\"parts\":[";
        for(std::size_t i=0;i<p.Parts.size();++i)
        {
            if(i)out<<',';const auto& x=p.Parts[i];out<<"{\"id\":";Text(out,x.ID);out<<",\"quantity\":"<<x.Quantity<<",\"material\":";Text(out,x.Material);
            out<<",\"freeRotation\":"<<(x.FreeRotation?"true":"false")<<",\"allowMirror\":"<<(x.AllowMirror?"true":"false")<<",\"allowedAngles\":[";for(std::size_t j=0;j<x.AllowedAnglesDegrees.size();++j){if(j)out<<',';out<<x.AllowedAnglesDegrees[j];}
            out<<"],\"geometry\":{\"radius\":"<<x.Geometry.Radius<<",\"center\":["<<x.Geometry.Center.X<<','<<x.Geometry.Center.Y<<"],\"holes\":[";
            for(std::size_t j=0;j<x.Geometry.Holes.size();++j){if(j)out<<',';Points(out,x.Geometry.Holes[j]);}
            out<<"],\"outer\":";Points(out,x.Geometry.Outer);out<<"}}";
        }
        out<<"],\"stocks\":[";
        for(std::size_t i=0;i<p.Stocks.size();++i)
        {
            if(i)out<<',';const auto& s=p.Stocks[i];out<<"{\"id\":";Text(out,s.ID);
            out<<",\"width\":"<<s.Width<<",\"height\":"<<s.Height<<",\"quantity\":"<<s.Quantity<<",\"cost\":"<<s.Cost<<",\"material\":";Text(out,s.Material);out<<'}';
        }
        out<<"]}";
    }
    using Tools::TraceJson;
    class PlaneTraceWriter
    {
        std::ofstream Stream;
        std::filesystem::path Index,Events,ResultFile;
        const Problem Input;
        std::mutex Mutex;
        std::map<std::string,std::size_t> Routes;
        std::shared_ptr<ConstructionTraceHealth> Health;
        std::size_t Count=0;
        double TimeLimit=0;
        std::size_t AttemptCap=0;
        RankingPolicy Ranking=RankingPolicy::RegularRemnant;
        std::string TaskProfile;
        bool Failed=false,Finished=false,Rotated=false;
        void SaveIndex(bool completed)
        {
            std::ofstream out(Index,std::ios::binary|std::ios::trunc);out<<std::setprecision(17);
            out<<"{\"schemaVersion\":2,\"format\":\"native-construction-jsonl\",\"resultFile\":";Text(out,ResultFile.filename().string());
            out<<",\"eventsFile\":";Text(out,Events.filename().string());out<<",\"rotatedFrame\":"<<(Rotated?"true":"false")
                <<",\"instrumentedTiming\":true,\"eventCap\":null,\"byteCap\":null,\"eventsWritten\":"<<Count
                <<",\"runCompleted\":"<<(completed?"true":"false")<<",\"observerFailed\":"<<(Failed?"true":"false")
                <<",\"eventsOmitted\":"<<(Health?Health->Omitted.load():0)
                <<",\"truncated\":"<<((Failed||!completed)?"true":"false")<<",\"routes\":[";
            bool first=true;for(const auto& [route,count]:Routes){if(!first)out<<',';first=false;out<<"{\"id\":";Text(out,route);out<<",\"events\":"<<count<<'}';}
            out<<"],\"conditions\":{\"timeLimitSeconds\":"<<TimeLimit<<",\"attemptCap\":"<<AttemptCap
                <<",\"rankingPolicy\":"<<static_cast<int>(Ranking)<<",\"diagnosticRun\":true,\"externalLayoutSeed\":false,\"taskProfile\":";
            Text(out,TaskProfile);
            out<<",\"partGap\":"<<Input.PartGap<<",\"edgeMargin\":"<<Input.EdgeMargin
                <<",\"allowCommonEdges\":"<<(Input.AllowCommonEdges?"true":"false")
                <<",\"freeRotation\":"<<(std::all_of(Input.Parts.begin(),Input.Parts.end(),[](const Part& part){return part.FreeRotation;})?"true":"false")<<"},\"problem\":";
            ProblemJson(out,Input);out<<'}';out.flush();if(!out)Failed=true;
        }
    public:
        PlaneTraceWriter(const std::filesystem::path& result,const Problem& input,bool rotated,const Options& options,std::string taskProfile="original")
            :ResultFile(result),Input(input),Health(options.ConstructionObservation),TimeLimit(options.TimeLimitSeconds),
                AttemptCap(options.MaxPlacementAttempts),Ranking(options.Ranking),TaskProfile(std::move(taskProfile)),Rotated(rotated)
        {
            Index=result;Index.replace_extension(".trace-index.json");Events=result;Events.replace_extension(".construction.jsonl");
            if(std::filesystem::exists(Index)||std::filesystem::exists(Events))throw std::runtime_error("Refusing to overwrite native trace evidence.");
            Stream.open(Events,std::ios::binary);if(!Stream)throw std::runtime_error("Could not create native construction trace.");
            SaveIndex(false);
        }
        ~PlaneTraceWriter(){Finish(false);}
        void Record(const ConstructionTraceEvent& event)noexcept
        {
            std::lock_guard<std::mutex> guard(Mutex);
            if(Failed||Finished)return;
            try
            {
                Stream<<std::setprecision(17)<<"{\"ordinal\":"<<Count+1<<",\"event\":";TraceJson(Stream,event);Stream<<"}\n";
                ++Count;++Routes[event.Route];if(Count%256==0)Stream.flush();if(!Stream)Failed=true;
            }
            catch(...){Failed=true;}
        }
        bool Finish(bool completed)noexcept
        {
            std::lock_guard<std::mutex> guard(Mutex);
            if(Finished)return !Failed;Finished=true;
            try{Stream.flush();if(!Stream||(Health&&Health->Failed.load()))Failed=true;SaveIndex(completed&&!Failed);}catch(...){Failed=true;}
            return !Failed;
        }
    };
    class FeatureTraceWriter
    {
        static constexpr std::size_t MaxEvents=10000;
        static constexpr std::size_t MaxBytes=64*1024*1024;
        static constexpr std::size_t FooterReserve=4096;
        std::ofstream Stream;
        std::size_t Seen=0,Written=0,Dropped=0,Bytes=0;
        bool ByteLimited=false,EventLimited=false,CoreTruncated=false,ObserverFailed=false,Finished=false;
        std::optional<std::uint64_t> SelectedStateID;
    public:
        FeatureTraceWriter(const std::filesystem::path& path,const std::filesystem::path& result,
                           const Problem& problem,bool rotated)
        {
            if(std::filesystem::exists(path))throw std::runtime_error("Refusing to overwrite construction trace evidence.");
            std::ostringstream header;
            header<<std::setprecision(17)<<"{\"schemaVersion\":1,\"coordinateFrame\":\"benchmark-work\",\"rotatedFrame\":"
                <<(rotated?"true":"false")<<",\"resultFile\":";Text(header,result.filename().string());
            header<<",\"problem\":";ProblemJson(header,problem);header<<",\"events\":[";
            const auto data=header.str();
            if(data.size()>MaxBytes-FooterReserve)throw std::runtime_error("Original problem exceeds the construction trace byte limit.");
            Stream.open(path,std::ios::binary);
            Stream<<data;
            if(!Stream)throw std::runtime_error("Could not create construction trace evidence.");
            Bytes=data.size();
        }
        ~FeatureTraceWriter(){Finish(false);}
        void Record(const ConstructionTraceEvent& event) noexcept
        {
            ++Seen;
            CoreTruncated=CoreTruncated||event.Truncated;
            if(event.Stage==ConstructionTraceStage::Selected)SelectedStateID=event.StateID;
            if(Seen>MaxEvents)
            {
                EventLimited=true;
                ++Dropped;
                return;
            }
            if(ObserverFailed||Finished)
            {
                ++Dropped;
                return;
            }
            try
            {
                std::ostringstream json;
                json<<std::setprecision(17);
                TraceJson(json,event);
                const auto data=json.str();
                const std::size_t separator=Written?1:0;
                if(data.size()+separator>MaxBytes-FooterReserve-Bytes)
                {
                    ByteLimited=true;
                    ++Dropped;
                    return;
                }
                if(separator)Stream<<',';
                Stream<<data;
                Stream.flush();
                if(!Stream)
                {
                    ObserverFailed=true;
                    ++Dropped;
                    return;
                }
                Bytes+=data.size()+separator;
                ++Written;
            }
            catch(...)
            {
                ObserverFailed=true;
                ++Dropped;
            }
        }
        bool Finish(bool completed) noexcept
        {
            if(Finished)return static_cast<bool>(Stream);
            Finished=true;
            try
            {
                Stream<<"],\"selectedStateID\":";
                if(SelectedStateID)Stream<<*SelectedStateID;
                else Stream<<"null";
                Stream<<",\"caps\":{\"maxEvents\":"<<MaxEvents<<",\"maxBytes\":"<<MaxBytes
                    <<"},\"meta\":{\"eventsSeen\":"<<Seen<<",\"eventsWritten\":"<<Written<<",\"eventsDropped\":"<<Dropped
                    <<",\"bytesBeforeFooter\":"<<Bytes<<",\"eventLimited\":"<<(EventLimited?"true":"false")
                    <<",\"byteLimited\":"<<(ByteLimited?"true":"false")
                    <<",\"coreTruncated\":"<<(CoreTruncated?"true":"false")
                    <<",\"observerFailed\":"<<(ObserverFailed?"true":"false")
                    <<",\"runCompleted\":"<<(completed?"true":"false")
                    <<",\"truncated\":"<<((Dropped||CoreTruncated||ObserverFailed||!completed)?"true":"false")<<"}}";
                Stream.flush();
            }
            catch(...){ObserverFailed=true;}
            return static_cast<bool>(Stream)&&!ObserverFailed;
        }
    };
    Result Recover(Result r,const Problem& p,bool rotated)
    {
        if(!rotated)return r;
        for(auto& layout:r.Layouts)
        {
            layout.Width=p.Stocks[0].Width;layout.Height=p.Stocks[0].Height;
            for(auto& x:layout.Placements){const auto a=x.Transform.X;x.Transform.X=x.Transform.Y;x.Transform.Y=p.Stocks[0].Height-a;}
        }
        return r;
    }
    void JobJson(std::ostream& out,const Problem& p,const JobResult& job)
    {
        const auto& r=job.Output;out<<"{\"state\":"<<static_cast<int>(r.State)<<",\"valid\":"<<(job.Checked.Valid?"true":"false")
            <<",\"complete\":"<<(job.Checked.Complete?"true":"false")<<",\"placed\":"<<job.Checked.PlacedCount
            <<",\"regularRemnant\":"<<job.Remnants.LastSheetRegularArea<<",\"remnantMeasured\":"<<(job.Remnants.Measured?"true":"false")
            <<",\"attempts\":"<<r.Stats.PlacementAttempts<<",\"postSeconds\":"<<r.Stats.PostProcessingSeconds<<",\"layouts\":[";
        double right=0,top=0;
        for(std::size_t i=0;i<r.Layouts.size();++i)
        {
            if(i)out<<',';const auto& l=r.Layouts[i];out<<"{\"stock\":";Text(out,l.StockID);out<<",\"instance\":"<<l.StockInstance
                <<",\"width\":"<<l.Width<<",\"height\":"<<l.Height<<",\"placements\":[";
            for(std::size_t j=0;j<l.Placements.size();++j)
            {
                if(j)out<<',';const auto& x=l.Placements[j];out<<"{\"part\":";Text(out,x.PartID);out<<",\"instance\":"<<x.Instance
                    <<",\"x\":"<<x.Transform.X<<",\"y\":"<<x.Transform.Y<<",\"angle\":"<<x.Transform.AngleDegrees
                    <<",\"mirrored\":"<<(x.Transform.Mirrored?"true":"false")
                    <<",\"group\":"<<x.Group<<",\"gapFiller\":"<<(x.GapFiller?"true":"false")<<'}';
                const auto it=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& a){return a.ID==x.PartID;});
                const auto b=Detail::Bounds(it->Geometry,x.Transform);right=std::max(right,b.X+b.Width);top=std::max(top,b.Y+b.Height);
            }
            out<<"]}";
        }
        out<<"],\"right\":"<<right<<",\"top\":"<<top<<",\"remaining\":[";
        for(std::size_t i=0;i<r.Remaining.size();++i){if(i)out<<',';out<<"{\"part\":";Text(out,r.Remaining[i].PartID);out<<",\"quantity\":"<<r.Remaining[i].Quantity<<'}';}
        out<<"],\"remnantRects\":[";
        for(std::size_t i=0;i<job.Remnants.PerLayout.size();++i){if(i)out<<',';const auto& a=job.Remnants.PerLayout[i];
            out<<"{\"x\":"<<a.Bounds.X<<",\"y\":"<<a.Bounds.Y<<",\"width\":"<<a.Bounds.Width<<",\"height\":"<<a.Bounds.Height
                <<",\"area\":"<<a.Area<<",\"verified\":"<<(a.Verified?"true":"false")<<'}';}
        out<<"],\"diagnostics\":[";for(std::size_t i=0;i<r.Diagnostics.size();++i){if(i)out<<',';Text(out,r.Diagnostics[i]);}out<<"]}";
    }
}
int main(int argc,char** argv)try
{
    if(argc<7||argc>18)throw std::runtime_error("PublicBench source.dat width output.json seconds attempts strategy-csv-or-seed@file [post-seconds] [rot90|original|free|source-free|standard-sheet] [construct|rotate|rotate-repair|refill|reassemble|linear|separate|separate-nfp|constraint|constraint-neighborhood] [constraint-scale] [constraint-workers] [constraint-conflicts] [constraint-complete-hints:0|1] [neighborhood-movable] [neighborhood-plans] [neighborhood-seconds-per-plan] [neighborhood-contact-order:0|1]");
    auto original=Read(argv[1],std::stod(argv[2]));
    const std::string taskProfile=argc>8?argv[8]:"standard-sheet";
    const bool standardSheet=taskProfile=="standard-sheet";
    if(standardSheet)ApplyPlaneTaskConditions(original);
    const bool freeVariant=argc>8&&std::string(argv[8])=="free";
    const bool sourceFree=argc>8&&std::string(argv[8])=="source-free";
    if(freeVariant||sourceFree)for(auto& part:original.Parts){part.FreeRotation=true;part.AllowedAnglesDegrees.clear();}
    auto work=original;const bool rotated=argc>8&&std::string(argv[8])=="rot90";
    const bool refill=argc>9&&std::string(argv[9])=="refill";
    const bool reassemble=argc>9&&std::string(argv[9])=="reassemble";
    const bool rotationRepair=argc>9&&std::string(argv[9])=="rotate-repair";
    const bool rotation=rotationRepair||(argc>9&&std::string(argv[9])=="rotate");
    const bool constructionOnly=argc>9&&std::string(argv[9])=="construct";
    const bool linear=argc>9&&std::string(argv[9])=="linear";
    const bool geometric=argc>9&&std::string(argv[9])=="separate-nfp";
    const bool separate=geometric||(argc>9&&std::string(argv[9])=="separate");
    const bool neighborhood=argc>9&&std::string(argv[9])=="constraint-neighborhood";
    const bool constraint=neighborhood||(argc>9&&std::string(argv[9])=="constraint");
    if(argc>10&&!constraint)throw std::runtime_error("Constraint settings require constraint mode.");
    if(argc>14&&!neighborhood)throw std::runtime_error("Neighborhood settings require neighborhood mode.");
#ifndef ICAX_SHEET_NESTING_WITH_CP_SAT
    if(constraint)throw std::runtime_error("This build does not enable the optional constraint completion backend.");
#else
    ConstraintCompletionSettings constraintSettings;
    if(argc>10)constraintSettings.CoordinateScale=std::stoll(argv[10]);
    if(argc>11)constraintSettings.Workers=std::stoi(argv[11]);
    if(argc>12)constraintSettings.MaxConflicts=std::stoll(argv[12]);
    if(argc>13)
    {
        const auto hints=std::string(argv[13]);
        if(hints!="0"&&hints!="1")throw std::runtime_error("Constraint complete hints must be 0 or 1.");
        constraintSettings.CompleteSeparationHints=hints=="1";
    }
    ConstraintNeighborhoodSettings neighborhoodSettings;neighborhoodSettings.Solver=constraintSettings;
    if(argc>14)neighborhoodSettings.MaxMovableInstances=std::stoull(argv[14]);
    if(argc>15)neighborhoodSettings.MaxPlans=std::stoull(argv[15]);
    if(argc>16)neighborhoodSettings.SecondsPerPlan=std::stod(argv[16]);
    if(argc>17)
    {
        const auto order=std::string(argv[17]);
        if(order!="0"&&order!="1")throw std::runtime_error("Neighborhood contact order must be 0 or 1.");
        neighborhoodSettings.ContactOrder=order=="1"?ConstraintContactOrder::SmallMaterialContact:ConstraintContactOrder::LayoutOrder;
    }
    if(neighborhood)MakeConstraintNeighborhoodStage(neighborhoodSettings);
    else if(constraint)MakeConstraintCompletionStage(constraintSettings);
#endif
    if(rotated){for(auto& p:work.Parts)for(auto& v:p.Geometry.Outer){const auto x=v.X;v.X=-v.Y;v.Y=x;}std::swap(work.Stocks[0].Width,work.Stocks[0].Height);}
    const std::filesystem::path output=argv[3];if(std::filesystem::exists(output))throw std::runtime_error("Refusing to overwrite benchmark evidence.");
    std::vector<std::string> ids;std::istringstream csv(argv[6]);std::string id;while(std::getline(csv,id,','))if(!id.empty())ids.push_back(id);
    const auto traceCount=std::count_if(ids.begin(),ids.end(),[](const auto& strategy)
    {return strategy=="feature.contact.trace"||strategy=="feature.greedy.trace"||
        strategy=="feature.greedy.waste.trace"||strategy=="feature.greedy.low.trace"||
        strategy=="feature.greedy.remaining.trace";});
    if(traceCount>1)throw std::runtime_error("Only one construction trace strategy may run per benchmark invocation.");
    if(traceCount&&!constructionOnly)throw std::runtime_error("Construction trace strategies require construct mode so the recorded selection is the original constructor result.");
    const bool planeTrace=ids.size()==1&&ids.front().starts_with("plane.")&&ids.front().ends_with(".trace");
    if(planeTrace&&!constructionOnly)throw std::runtime_error("Plane construction trace requires construct mode.");
    if(planeTrace)ids.front().resize(ids.front().size()-6);
    Options settings;settings.TimeLimitSeconds=std::stod(argv[4]);settings.MaxPlacementAttempts=std::stoull(argv[5]);settings.MaxPatterns=128;
    std::shared_ptr<PlaneTraceWriter> planeWriter;
    if(planeTrace)
    {
        settings.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        planeWriter=std::make_shared<PlaneTraceWriter>(output,work,rotated,settings,taskProfile);
        settings.ConstructionTraceRoute=ids.front();
        settings.ConstructionTrace=[planeWriter](const ConstructionTraceEvent& event){planeWriter->Record(event);};
    }
    if(constructionOnly)settings.EnableBasicCompaction=false;
    const auto postSeconds=argc>7?std::stod(argv[7]):2.;
    auto registry=DefaultStrategies();RegisterPlaneStrategies(registry);
    registry.Register("domain.free",MakeDomainSearchFactory(DomainOrderPolicy::FreeArea));
    auto beamCache=std::make_shared<NfpCache>(NfpCacheSettings{512,4096,32*1024*1024});
    registry.Register("nfp.beam",MakeNfpBeamFactory({},beamCache));
    NfpBeamSettings greedy;greedy.QuickPolicy=NfpBeamQuickPolicy::HullGreedy;
    registry.Register("nfp.beam-greedy",MakeNfpBeamFactory(greedy,beamCache));
    NfpBeamSettings wide;wide.BeamWidth=256;wide.PerInventory=8;wide.PlacementsPerPart=16;
    registry.Register("nfp.beam-wide",MakeNfpBeamFactory(wide,beamCache));
    NfpBeamSettings waste;waste.Score=NfpBeamScore::MaterialWaste;
    registry.Register("nfp.beam-waste",MakeNfpBeamFactory(waste,beamCache));
    NfpBeamSettings lookahead;lookahead.EndgameDepth=6;lookahead.EndgameGeometry=4;lookahead.RemainingLookahead=3;
    registry.Register("nfp.beam-lookahead",MakeNfpBeamFactory(lookahead,beamCache));
    auto geometryCache=std::make_shared<NfpCache>();
    for(const auto& [name,order]:std::vector<std::pair<std::string,NfpCompositeOrder>>{
        {"material",NfpCompositeOrder::MaterialArea},{"anchor",NfpCompositeOrder::AnchorArea},
        {"balanced",NfpCompositeOrder::BalancedSpecifications},{"remnant",NfpCompositeOrder::PreserveRemnant}})
    {NfpCompositeSettings c;c.Order=order;registry.Register("nfp.composite."+name,MakeNfpCompositeContactFactory(c,geometryCache));}
    for(const auto& [name,placement]:std::vector<std::pair<std::string,NfpCompositePlacement>>{
        {"anchor-low",NfpCompositePlacement::LowThenLeft},{"anchor-contact",NfpCompositePlacement::SharedBoundary}})
    {NfpCompositeSettings c;c.Order=NfpCompositeOrder::AnchorArea;c.Placement=placement;
        registry.Register("nfp.composite."+name,MakeNfpCompositeContactFactory(c,geometryCache));}
    registry.Register("domain.area",MakeDomainSearchFactory(DomainOrderPolicy::AreaFirst));
    registry.Register("contact.bounded",MakeBoundedContactFactory(geometryCache));registry.Register("contact.exact-fill",MakeExactFillContactFactory());
    registry.Register("convex.joint",MakeConvexConstructionFactory());
    registry.Register("feature.contact",MakeFeatureConstructionFactory({},geometryCache));
    registry.Register("feature.greedy",MakeFeatureGreedyFactory({},geometryCache));
    FeatureGreedySettings greedyWaste;greedyWaste.Order=FeatureGreedyOrder::IncrementalWaste;
    registry.Register("feature.greedy.waste",MakeFeatureGreedyFactory(greedyWaste,geometryCache));
    FeatureGreedySettings greedyLow;greedyLow.Order=FeatureGreedyOrder::LowThenLeft;
    registry.Register("feature.greedy.low",MakeFeatureGreedyFactory(greedyLow,geometryCache));
    FeatureGreedySettings greedyRemaining;greedyRemaining.Order=FeatureGreedyOrder::RemainingArea;
    registry.Register("feature.greedy.remaining",MakeFeatureGreedyFactory(greedyRemaining,geometryCache));
    std::shared_ptr<FeatureTraceWriter> trace;
    auto tracePath=output;tracePath.replace_extension(".trace.json");
    if(traceCount)
    {
        trace=std::make_shared<FeatureTraceWriter>(tracePath,output,work,rotated);
        FeatureConstructionSettings traced;
        traced.Trace=[trace](const ConstructionTraceEvent& event){trace->Record(event);};
        registry.Register("feature.contact.trace",MakeFeatureConstructionFactory(std::move(traced),geometryCache));
        for(const auto& [name,order]:std::vector<std::pair<std::string,FeatureGreedyOrder>>{
            {"feature.greedy.trace",FeatureGreedyOrder::IncrementalArea},
            {"feature.greedy.waste.trace",FeatureGreedyOrder::IncrementalWaste},
            {"feature.greedy.low.trace",FeatureGreedyOrder::LowThenLeft},
            {"feature.greedy.remaining.trace",FeatureGreedyOrder::RemainingArea}})
        {
            FeatureGreedySettings greedyTraced;
            greedyTraced.Order=order;
            greedyTraced.Trace=[trace](const ConstructionTraceEvent& event){trace->Record(event);};
            registry.Register(name,MakeFeatureGreedyFactory(std::move(greedyTraced),geometryCache));
        }
    }
    registry.Register("construction.portfolio",MakeConstructionPortfolioFactory(registry,
        {"rules.composite","rules.contact-large","nfp.beam","nfp.beam-greedy",
            "contact.bounded","nfp.beam-wide","nfp.beam-waste","contact.exact-fill","nfp.beam-lookahead",
            "convex.joint","nfp.composite.anchor-contact","nfp.composite.material","nfp.composite.anchor",
            "nfp.composite.balanced","nfp.composite.remnant","nfp.composite.anchor-low","feature.contact",
            "feature.greedy","feature.greedy.waste","feature.greedy.low","feature.greedy.remaining"}));
    std::ofstream out(output);out<<std::setprecision(17)<<"{\"case\":";Text(out,std::filesystem::path(argv[1]).stem().string());
    out<<",\"taskProfile\":";Text(out,taskProfile);
    if(trace){out<<",\"traceFile\":";Text(out,tracePath.filename().string());}
    if(planeWriter){auto index=output;index.replace_extension(".trace-index.json");out<<",\"nativeConstructionTrace\":";Text(out,index.filename().string());out<<",\"instrumentedTiming\":true";}
    out<<",\"rotatedFrame\":"<<(rotated?"true":"false")<<",\"secondsPerConstructor\":"<<settings.TimeLimitSeconds
        <<",\"secondsPerPostprocessor\":"<<postSeconds<<",\"stageIsolation\":"<<(rotation?"false":"true")<<",\"refill\":"<<(refill?"true":"false")
        <<",\"postprocessor\":\""<<(constructionOnly?"none":rotationRepair?"compact.rotation+repair.domain":rotation?"compact.rotation":neighborhood?"repair.constraint-neighborhood":constraint?"complete.cp-sat":linear?"repair.convex-linear":geometric?"repair.triangle-separation.nfp-seeds":separate?"repair.triangle-separation":reassemble?"repair.nfp.reassembly":"compact.nfp")<<'"';
    out<<",\"rotationDomainVariant\":\""<<(standardSheet?"standard-sheet-free-domain":sourceFree?"original-free-domain":freeVariant?"free-rotation-variant":"original")<<'"';
    if(rotation)
    {
        if(settings.MaxPlacementAttempts>std::numeric_limits<std::size_t>::max()/2)
            throw std::runtime_error("Rotation pipeline attempt budget is too large.");
        out<<",\"sameExecutionConstruction\":true,\"compactionPrimaryAxis\":\"X\",\"wholeJobBudgetSeconds\":"<<settings.TimeLimitSeconds+postSeconds
            <<",\"constructorAttemptCap\":"<<settings.MaxPlacementAttempts<<",\"wholeJobAttemptBudget\":"<<settings.MaxPlacementAttempts*2;
    }
    if(constructionOnly)
        out<<",\"sameExecutionConstruction\":true,\"wholeJobBudgetSeconds\":"<<settings.TimeLimitSeconds
            <<",\"constructorAttemptCap\":"<<settings.MaxPlacementAttempts<<",\"wholeJobAttemptBudget\":"<<settings.MaxPlacementAttempts;
#ifdef ICAX_SHEET_NESTING_WITH_CP_SAT
    if(constraint)out<<",\"constraintSettings\":{\"coordinateScale\":"<<constraintSettings.CoordinateScale
        <<",\"workers\":"<<constraintSettings.Workers<<",\"maxConflicts\":"<<constraintSettings.MaxConflicts
        <<",\"completeSeparationHints\":"<<(constraintSettings.CompleteSeparationHints?"true":"false")<<'}';
    if(neighborhood)out<<",\"neighborhoodSettings\":{\"maxMovable\":"<<neighborhoodSettings.MaxMovableInstances
        <<",\"maxPlans\":"<<neighborhoodSettings.MaxPlans<<",\"secondsPerPlan\":"<<neighborhoodSettings.SecondsPerPlan
        <<",\"contactOrder\":\""<<(neighborhoodSettings.ContactOrder==ConstraintContactOrder::SmallMaterialContact?"small-material":"layout")<<"\"}";
#endif
    out<<",\"problem\":";ProblemJson(out,original);out<<",\"runs\":[";
    bool first=true;
    for(const auto& constructor:ids)
    {
        if(rotation)
        {
            if(constructor.starts_with("seed@"))throw std::runtime_error("Rotation pipeline requires an original-order constructor.");
            auto observation=std::make_shared<RotationObservation>();
            StrategyPipeline pipeline;
            pipeline.RequireCompleteConstruction=true;
            pipeline.Construct=[&registry,constructor,observation,limit=settings.TimeLimitSeconds,attempts=settings.MaxPlacementAttempts]
            {return std::make_unique<ObservedConstructor>(registry.Create(constructor),observation,limit,attempts);};
            pipeline.PostProcessors.push_back({"compact.rotation",[observation]
                {return std::make_unique<ObservedRotation>(observation);}});
            if(rotationRepair)
            {
                pipeline.PostProcessors.back().BudgetFraction=.5;
                pipeline.PostProcessors.push_back(MakeDomainRepairStage());
            }
            StrategyRegistry execution;
            execution.Register("rotation.pipeline",MakePipelineFactory(std::move(pipeline)));
            auto whole=settings;whole.TimeLimitSeconds+=postSeconds;whole.BasicCompactionXFirst=true;
            whole.MaxPlacementAttempts*=2;
            const auto started=Clock::now();
            auto after=RunJob({"rotation", "rotation.pipeline",work,whole},execution);
            const double wall=std::chrono::duration<double>(Clock::now()-started).count();
            if(!observation->Constructed)
            {
                std::ostringstream error;
                error<<"Rotation pipeline did not return an original construction result. Native state "
                    <<static_cast<int>(after.Output.State);
                for(const auto& diagnostic:after.Output.Diagnostics)error<<'\n'<<diagnostic;
                throw std::runtime_error(error.str());
            }
            JobResult constructed;constructed.Output=Recover(observation->Construction,original,rotated);
            Detail::AssessCandidate(original,constructed);
            JobResult before;before.Output=Recover(observation->RotationStarted?observation->BeforeRotation:after.Output,original,rotated);
            Detail::AssessCandidate(original,before);
            after.Output=Recover(std::move(after.Output),original,rotated);Detail::AssessCandidate(original,after);
            const bool accepted=Detail::Eligible(after)&&Detail::Quality(after,original,settings.Ranking)<Detail::Quality(before,original,settings.Ranking);
            const bool retain=Detail::Eligible(after)&&Detail::Quality(after,original,settings.Ranking)<=Detail::Quality(before,original,settings.Ranking);
            const auto& selected=retain?after:before;
            if(!first)out<<',';first=false;
            out<<"{\"constructor\":";Text(out,constructor);
            out<<",\"workAxis\":\"joint\",\"constructorWallSeconds\":"<<observation->ConstructorSeconds
                <<",\"postWallSeconds\":"<<std::max(0.,wall-observation->ConstructorSeconds)
                <<",\"wholeJobWallSeconds\":"<<wall<<",\"rotationStarted\":"<<(observation->RotationStarted?"true":"false")
                <<",\"accepted\":"<<(accepted?"true":"false")<<",\"construction\":";
            JobJson(out,original,constructed);out<<",\"before\":";JobJson(out,original,before);
            out<<",\"candidate\":";JobJson(out,original,after);out<<",\"selected\":";JobJson(out,original,selected);out<<'}';out.flush();
            std::cout<<constructor<<" rotation started="<<observation->RotationStarted<<" accepted="<<accepted
                <<" placed="<<selected.Checked.PlacedCount<<" remnant "<<before.Remnants.LastSheetRegularArea
                <<" -> "<<selected.Remnants.LastSheetRegularArea<<std::endl;
            continue;
        }
        const auto start=Clock::now();JobResult native;
        if(constructor.starts_with("seed@"))
        {if(rotated)throw std::runtime_error("Placement seeds use the original frame only.");native=ReadSeed(work,constructor.substr(5));}
        else native=RunJob({"base",constructor,work,settings},registry);
        const double constructorWall=std::chrono::duration<double>(Clock::now()-start).count();
        JobResult before=native;before.Output=Recover(before.Output,original,rotated);Detail::AssessCandidate(original,before);
        if(constructionOnly)
        {
            if(!first)out<<',';first=false;
            out<<"{\"constructor\":";Text(out,constructor);
            out<<",\"workAxis\":\"construct\",\"constructorWallSeconds\":"<<constructorWall
                <<",\"postWallSeconds\":0,\"wholeJobWallSeconds\":"<<constructorWall
                <<",\"rotationStarted\":false,\"accepted\":false,\"construction\":";
            JobJson(out,original,before);out<<",\"before\":";JobJson(out,original,before);
            out<<",\"candidate\":";JobJson(out,original,before);out<<",\"selected\":";JobJson(out,original,before);out<<'}';out.flush();
            std::cout<<constructor<<" construction placed="<<before.Checked.PlacedCount<<" complete="<<before.Checked.Complete<<std::endl;
            continue;
        }
        std::cout<<constructor<<" before "<<before.Checked.PlacedCount<<" complete="<<before.Checked.Complete<<std::endl;
        for(auto axis:{CompactionAxis::X,CompactionAxis::Y})
        {
            if((separate||constraint)&&axis==CompactionAxis::Y)continue;
            NfpCompactionSettings config;config.Axis=axis;config.MaxPasses=3;config.RefillMissing=refill;
            auto post=settings;post.TimeLimitSeconds=postSeconds;
            const auto started=Clock::now();NfpReassemblySettings assembly;assembly.MaxReleased=axis==CompactionAxis::X?6:8;
            ConvexLinearRepairSettings convex;convex.MaxAxisChoices=axis==CompactionAxis::X?4:12;
            convex.MaxSearchNodes=200000;convex.MaxOpenNodes=4096;convex.MaxSeeds=16;
            TriangleSeparationRepairSettings triangle;triangle.UseNfpSeeds=geometric;
            Result candidate;
#ifdef ICAX_SHEET_NESTING_WITH_CP_SAT
            if(neighborhood)candidate=MakeConstraintNeighborhoodStage(neighborhoodSettings).Factory()->Run(work,native.Output,post);
            else if(constraint)candidate=MakeConstraintCompletionStage(constraintSettings).Factory()->Run(work,native.Output,post);
            else
#endif
            candidate=linear?MakeConvexLinearRepairStage(convex).Factory()->Run(work,native.Output,post):
                separate?MakeTriangleSeparationRepairStage(triangle).Factory()->Run(work,native.Output,post):
                reassemble?MakeNfpReassemblyStage(assembly).Factory()->Run(work,native.Output,post):
                MakeNfpCompactionStage(config).Factory()->Run(work,native.Output,post);
            JobResult after;after.Output=Recover(std::move(candidate),original,rotated);Detail::AssessCandidate(original,after);
            const double postWall=std::chrono::duration<double>(Clock::now()-started).count();
            const bool accepted=Detail::Eligible(after)&&Detail::Quality(after,original,settings.Ranking)<Detail::Quality(before,original,settings.Ranking);
            if(!first)out<<',';first=false;out<<"{\"constructor\":";Text(out,constructor);out<<",\"workAxis\":\"";
            if(constraint||separate)out<<"joint";
            else if(linear)out<<"axes-"<<convex.MaxAxisChoices;
            else if(reassemble)out<<"released-"<<assembly.MaxReleased;else out<<(axis==CompactionAxis::X?'X':'Y');
            out<<"\",\"constructorWallSeconds\":"<<constructorWall<<",\"postWallSeconds\":"<<postWall<<",\"accepted\":"<<(accepted?"true":"false")<<",\"before\":";
            JobJson(out,original,before);out<<",\"candidate\":";JobJson(out,original,after);out<<",\"selected\":";JobJson(out,original,accepted?after:before);out<<'}';out.flush();
            std::cout<<" post "<<(axis==CompactionAxis::X?'X':'Y')<<" accepted="<<accepted<<" remnant "<<before.Remnants.LastSheetRegularArea<<" -> "<<after.Remnants.LastSheetRegularArea<<std::endl;
        }
    }
    out<<"]}";out.flush();if(!out)throw std::runtime_error("Could not save benchmark evidence.");
    if(trace&&!trace->Finish(true))throw std::runtime_error("Could not save complete construction trace evidence.");
    if(planeWriter&&!planeWriter->Finish(true))throw std::runtime_error("Could not save complete native construction trace evidence.");
    return 0;
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
