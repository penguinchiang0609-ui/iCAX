#include "../../../../tools/sheet-nesting/NativeConstructionTraceWriter.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* reason) { if(!value)throw std::runtime_error(reason); }
std::string Read(const std::filesystem::path& path)
{std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Files
{
    std::filesystem::path Index,Events,Result;
    explicit Files(const char* label)
    {
        const auto name=std::string("icax-trace-")+label+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Result=std::filesystem::temp_directory_path()/(name+".json");
        Index=std::filesystem::temp_directory_path()/(name+".trace-index.json");
        Events=std::filesystem::temp_directory_path()/(name+".construction.jsonl");
    }
    ~Files()
    {
        std::error_code error;
        std::filesystem::remove(Index,error);std::filesystem::remove(Events,error);
        auto staging=Index;staging+=".tmp";std::filesystem::remove(staging,error);
    }
};
void ProblemJson(std::ostream& out,const Problem&)
{out<<"{\"parts\":[],\"stocks\":[],\"partGap\":0.2,\"edgeMargin\":0.5,\"allowCommonEdges\":true}";}
ConstructionTraceEvent Event(const char* route,const char* source,ConstructionTraceStage stage)
{
    ConstructionTraceEvent event;event.Route=route;event.Scope="trial-7/state-13";event.Source=source;event.Stage=stage;
    event.Rank={{"boards",1},{"negativeRegularRemnant",-100}};
    return event;
}
std::size_t ByteCap(const Tools::NativeConstructionTraceLimits& caps)
{
    return caps.Routes*(caps.DetailBytesPerRoute+caps.ProgressBytesPerRoute+caps.StateBytesPerRoute+caps.PathBytesPerRoute+
        caps.SummaryBytesPerRoute+caps.PerformanceBytesPerRoute+caps.TerminalBytesPerRoute)+caps.FinalSelectionBytes;
}
std::string EventJson(const ConstructionTraceEvent& event)
{
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::setprecision(std::numeric_limits<double>::max_digits10);Tools::TraceJson(out,event);return out.str();
}
void IndependentReservations()
{
    Files files("reserved");Options options;options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    Tools::NativeConstructionTraceLimits caps;caps.Routes=3;caps.DetailEventsPerRoute=16;caps.DetailBytesPerRoute=4096;
    caps.ProgressEventsPerRoute=16;caps.ProgressBytesPerRoute=4096;caps.TerminalBytesPerRoute=16384;caps.FinalSelectionBytes=16384;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto detail=Event("route-a","nfp.proposal",ConstructionTraceStage::Generate);
        for(std::size_t i=0;i<10000;++i){detail.Step=i;writer.Record(detail);}
        auto commit=Event("route-a","nfp.commit",ConstructionTraceStage::Commit);writer.Record(commit);
        for(const char* route:{"route-a","route-b"})
        {
            auto terminal=Event(route,"scheduler.route-result",ConstructionTraceStage::Complete);
            terminal.Layouts={{"stock",0,40,40,{{"original",0,{.5,.5,37},0,false}},100}};
            writer.Record(terminal);
            writer.Record(Event(route,"scheduler.quality",ConstructionTraceStage::Score));
        }
        writer.Record(Event("plane.initial","scheduler.final-selection",ConstructionTraceStage::Selected));
        Require(writer.Finish(true),"bounded observation unexpectedly failed");
    }
    const auto index=Read(files.Index),events=Read(files.Events);
    Require(index.find("\"runCompleted\":true")!=std::string::npos&&index.find("\"observerFailed\":false")!=std::string::npos,
        "completed index lost success/failure distinction");
    Require(index.find("\"truncated\":true")!=std::string::npos&&index.find("\"rankObserved\":true")!=std::string::npos,
        "sampling omissions or final rank missing");
    Require(events.find("nfp.commit")!=std::string::npos&&events.find("route-b")!=std::string::npos&&
        events.find("scheduler.final-selection")!=std::string::npos&&events.find("\"angle\":37")!=std::string::npos,
        "early proposals starved progress, later terminal or original placement");
    const auto hardCap=ByteCap(caps);
    Require(events.size()<=hardCap,"writer exceeded reserved total byte bound");
    bool refused=false;
    try{Tools::NativeConstructionTraceWriter duplicate(files.Index,files.Result,{},options,ProblemJson,caps);}
    catch(const std::runtime_error&){refused=true;}
    Require(refused,"existing native evidence was overwritten");
}
void PairSummariesSurviveDetailSampling()
{
    Files files("pair-summary");Options options;
    Tools::NativeConstructionTraceLimits caps;caps.Routes=4;
    caps.DetailEventsPerRoute=32;caps.DetailBytesPerRoute=64*1024;
    caps.SummaryEventsPerRoute=2;caps.SummaryBytesPerRoute=8192;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto detail=Event("route-a","preparation.retained-template",ConstructionTraceStage::Analysis);
        for(std::size_t i=0;i<10000;++i)writer.Record(detail);
        auto summary=Event("route-a","preparation.pair-summary",ConstructionTraceStage::Analysis);
        summary.PartIndex=19;summary.Reason="late-specification-pair";
        writer.Record(summary);summary.PartIndex=20;writer.Record(summary);
        summary.PartIndex=21;summary.Reason="summary-cap-explicitly-omitted";writer.Record(summary);
        summary.Route="route-b";summary.Reason="independent-summary-reservation";writer.Record(summary);
        summary.Source="nfp.original-clearance";summary.Stage=ConstructionTraceStage::Complete;
        summary.Reason="whole-original-clearance-cost-summary";writer.Record(summary);
        detail.Route="route-c";for(std::size_t i=0;i<1000;++i)writer.Record(detail);
        summary.Route="route-c";summary.Source="preparation.combination-summary";
        summary.Reason="mixed-combination-search-cost-summary";writer.Record(summary);
        summary.Source="array.original-query-result";summary.Stage=ConstructionTraceStage::Analysis;
        summary.Reason="greedy-original-rank-screen-cost-summary";writer.Record(summary);
        detail.Route="route-d";for(std::size_t i=0;i<1000;++i)writer.Record(detail);
        summary.Route="route-d";summary.Source="nfp.query-result";summary.Stage=ConstructionTraceStage::Analysis;
        summary.Reason="original-pose-query-summary";writer.Record(summary);
        summary.Source="array.rigid-query-result";summary.Reason="rigid-group-query-summary";writer.Record(summary);
        detail.Source="nfp.query";detail.Stage=ConstructionTraceStage::Complete;
        for(std::size_t i=0;i<1000;++i)writer.Record(detail);
        writer.Record(Event("route-d","constructor.commit",ConstructionTraceStage::Commit));
        writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
        writer.Record(Event("route-a","scheduler.quality",ConstructionTraceStage::Score));
        Require(writer.Finish(true),"pair summaries unexpectedly failed observation");
    }
    const auto index=Read(files.Index),events=Read(files.Events);
    Require(events.find("preparation.retained-template")!=std::string::npos,
        "same-stage detail flood never reached the normal observation lane");
    Require(events.find("\"partIndex\":19")!=std::string::npos&&events.find("\"partIndex\":20")!=std::string::npos&&
        events.find("independent-summary-reservation")!=std::string::npos,
        "earlier sampled singleton details hid later pair summaries");
    Require(events.find("whole-original-clearance-cost-summary")!=std::string::npos,
        "query cost summary was routed back into sampled progress events");
    Require(events.find("mixed-combination-search-cost-summary")!=std::string::npos,
        "combination search cost disappeared behind sampled candidate details");
    Require(events.find("greedy-original-rank-screen-cost-summary")!=std::string::npos,
        "ordinary single-original rank screening costs disappeared behind sampled candidate details");
    Require(events.find("original-pose-query-summary")!=std::string::npos&&
        events.find("rigid-group-query-summary")!=std::string::npos&&events.find("constructor.commit")!=std::string::npos,
        "placement queries consumed the original-commit replay reservation or hid query costs");
    Require(events.find("summary-cap-explicitly-omitted")==std::string::npos&&index.find("\"summaryEventsOmitted\":1")!=std::string::npos,
        "summary reservation did not enforce or expose its own bound");
    Require(events.find("scheduler.quality")!=std::string::npos&&events.find("scheduler.route-result")!=std::string::npos,
        "pair summary reservation consumed terminal/rank evidence");
}
void RigidPhaseSummariesKeepTheirOwnBoundedReservation()
{
    Files files("rigid-phases");Options options;Tools::NativeConstructionTraceLimits caps;
    caps.Routes=1;caps.DetailEventsPerRoute=1;caps.DetailBytesPerRoute=4096;
    caps.ProgressEventsPerRoute=1;caps.ProgressBytesPerRoute=4096;
    caps.SummaryEventsPerRoute=8;caps.SummaryBytesPerRoute=32*1024;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto detail=Event("route-a","nfp.rigid-placement",ConstructionTraceStage::Generate);
        for(std::size_t i=0;i<1000;++i)writer.Record(detail);
        auto summary=Event("route-a","nfp.rigid-placement",ConstructionTraceStage::Complete);
        summary.Reason="rigid-phase-cost-summary";
        summary.Measures={{"phaseRigidLookupSeconds",.25},{"phaseRigidLoopConversionSeconds",.5},
            {"phaseRigidBooleanSeconds",.75},{"phaseRigidCertificationSeconds",1.25},{"regionLoopCacheBytes",8192}};
        writer.Record(summary);
        for(std::size_t i=0;i<1000;++i){summary.Reason="bounded-rigid-summary-flood";writer.Record(summary);}
        writer.Record(Event("route-a","constructor.commit",ConstructionTraceStage::Commit));
        writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
        Require(writer.Finish(true),"rigid phase summary reservation failed observation");
    }
    const auto events=Read(files.Events),index=Read(files.Index);
    Require(events.find("rigid-phase-cost-summary")!=std::string::npos&&
        events.find("phaseRigidLoopConversionSeconds")!=std::string::npos&&events.find("regionLoopCacheBytes")!=std::string::npos,
        "rigid phase/cache costs disappeared into sampled placement details");
    Require(events.find("constructor.commit")!=std::string::npos&&events.find("scheduler.route-result")!=std::string::npos,
        "rigid query completion consumed the original-commit or terminal reservation");
    Require(index.find("\"observerFailed\":false")!=std::string::npos&&events.size()<=ByteCap(caps),
        "rigid phase summaries exceeded their bounded observation reservation");
}

void PlacementChoicesSurviveQuerySummaryFloods()
{
    for(const bool exhaustBytes:{false,true})
    {
        Files files(exhaustBytes?"choice-bytes":"choice-events");Options options;
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        Tools::NativeConstructionTraceLimits caps;caps.Routes=1;
        caps.DetailEventsPerRoute=16;caps.DetailBytesPerRoute=4096;
        caps.ProgressEventsPerRoute=16;caps.ProgressBytesPerRoute=4096;
        caps.SummaryEventsPerRoute=exhaustBytes?128:8;caps.SummaryBytesPerRoute=16*1024;
        caps.TerminalBytesPerRoute=8192;caps.FinalSelectionBytes=8192;
        {
            Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
            auto query=Event("route-a","nfp.query-result",ConstructionTraceStage::Analysis);
            query.Reason=exhaustBytes?std::string(4096,'q'):"query-cost-flood";
            for(std::size_t i=0;i<10000;++i)
            {
                query.Step=i;
                query.Source=i%3==0?"nfp.query-result":i%3==1?"array.rigid-query-result":"placement.local-pair-result";
                writer.Record(query);
            }
            auto choice=Event("route-a","placement.choice",ConstructionTraceStage::Analysis);
            choice.Reason="choice-best-small-pair";choice.StateID=13;choice.CandidateID=71;choice.ProposalID=17;
            choice.Rank={{"negativeUsedSectionUtilization",-.75},{"negativeNetMaterial",-12},
                {"occupiedPlanningArea",100},{"intrinsicFirstMaterialCornerDistance",0},
                {"primaryAxis",1.25},{"secondaryAxis",2.5},{"part",2},{"template",23},{"columns",1},{"rows",1}};
            choice.Members={{2,7,{2.25,3.5,180,true},19},{4,11,{8.75,9.25,37.5,false},19}};
            writer.Record(choice);
            choice.Reason="choice-best-single";choice.CandidateID=72;choice.ProposalID=18;
            choice.Rank={{"negativeUsedSectionUtilization",-.625},{"negativeNetMaterial",-6},
                {"occupiedPlanningArea",120},{"intrinsicFirstMaterialCornerDistance",0},
                {"primaryAxis",4.5},{"secondaryAxis",6.25},{"part",3},{"template",4},{"columns",0},{"rows",0}};
            choice.Members={{3,5,{6.25,4.5,90,false},20}};
            writer.Record(choice);
            choice.Reason="later-choice-flood";
            for(std::size_t i=0;i<1000;++i){choice.Step=10002+i;writer.Record(choice);}
            writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
            writer.Record(Event("route-a","scheduler.quality",ConstructionTraceStage::Score));
            writer.Record(Event("plane.initial","scheduler.final-selection",ConstructionTraceStage::Selected));
            Require(writer.Finish(true),"placement choice reservation unexpectedly failed observation");
        }
        const auto index=Read(files.Index),events=Read(files.Events);
        const auto line=[&](const char* reason)
        {
            const auto marker=events.find(std::string("\"reason\":\"")+reason+"\"");
            Require(marker!=std::string::npos,"query summaries hid a later actual placement choice");
            const auto previous=events.rfind('\n',marker),next=events.find('\n',marker);
            const auto begin=previous==std::string::npos?0:previous+1;
            return events.substr(begin,next==std::string::npos?std::string::npos:next-begin);
        };
        const auto pair=line("choice-best-small-pair"),single=line("choice-best-single");
        const std::string pairMembers="\"members\":[{\"partIndex\":2,\"instance\":7,\"pose\":{\"x\":2.25,\"y\":3.5,\"angle\":180,\"mirrored\":true},\"group\":19},"
            "{\"partIndex\":4,\"instance\":11,\"pose\":{\"x\":8.75,\"y\":9.25,\"angle\":37.5,\"mirrored\":false},\"group\":19}]";
        const std::string singleMembers="\"members\":[{\"partIndex\":3,\"instance\":5,\"pose\":{\"x\":6.25,\"y\":4.5,\"angle\":90,\"mirrored\":false},\"group\":20}]";
        const std::string pairRank="\"rank\":[{\"name\":\"negativeUsedSectionUtilization\",\"value\":-0.75},"
            "{\"name\":\"negativeNetMaterial\",\"value\":-12},{\"name\":\"occupiedPlanningArea\",\"value\":100},"
            "{\"name\":\"intrinsicFirstMaterialCornerDistance\",\"value\":0},{\"name\":\"primaryAxis\",\"value\":1.25},"
            "{\"name\":\"secondaryAxis\",\"value\":2.5},{\"name\":\"part\",\"value\":2},"
            "{\"name\":\"template\",\"value\":23},{\"name\":\"columns\",\"value\":1},{\"name\":\"rows\",\"value\":1}]";
        const std::string singleRank="\"rank\":[{\"name\":\"negativeUsedSectionUtilization\",\"value\":-0.625},"
            "{\"name\":\"negativeNetMaterial\",\"value\":-6},{\"name\":\"occupiedPlanningArea\",\"value\":120},"
            "{\"name\":\"intrinsicFirstMaterialCornerDistance\",\"value\":0},{\"name\":\"primaryAxis\",\"value\":4.5},"
            "{\"name\":\"secondaryAxis\",\"value\":6.25},{\"name\":\"part\",\"value\":3},"
            "{\"name\":\"template\",\"value\":4},{\"name\":\"columns\",\"value\":0},{\"name\":\"rows\",\"value\":0}]";
        Require(pair.find(pairMembers)!=std::string::npos&&single.find(singleMembers)!=std::string::npos&&
            pair.find(pairRank)!=std::string::npos&&single.find(singleRank)!=std::string::npos&&
            pair.find("\"candidateID\":71")!=std::string::npos&&single.find("\"candidateID\":72")!=std::string::npos,
            "late placement choice lost a member, stored pose or complete comparison key");
        Require(pair.find("observation-payload-truncated")==std::string::npos&&
            single.find("observation-payload-truncated")==std::string::npos,
            "placement choice reservation silently replaced actual alternatives with a count-only summary");
        std::istringstream lines(events);std::string current;std::size_t summaries=0,summaryBytes=0,queries=0;
        while(std::getline(lines,current))
        {
            const bool query=current.find("\"source\":\"nfp.query-result\"")!=std::string::npos||
                current.find("\"source\":\"array.rigid-query-result\"")!=std::string::npos||
                current.find("\"source\":\"placement.local-pair-result\"")!=std::string::npos;
            if(query)++queries;
            if(query||current.find("\"source\":\"placement.choice\"")!=std::string::npos)
            {++summaries;summaryBytes+=current.size()+1;}
        }
        Require(queries>0&&summaries<=caps.SummaryEventsPerRoute&&summaryBytes<=caps.SummaryBytesPerRoute,
            "reserved placement choices exceeded the route's total summary event or byte limit");
        if(exhaustBytes)Require(queries<caps.SummaryEventsPerRoute*3/4,
            "byte-reservation fixture exhausted events instead of query bytes");
        else Require(queries==6&&summaries==8&&events.find("later-choice-flood")==std::string::npos&&
            events.find("\"source\":\"placement.local-pair-result\"")!=std::string::npos,
            "event reservation did not preserve exactly the bounded late choice capacity");
        const auto hardCap=ByteCap(caps);
        Require(events.size()<=hardCap&&index.find("\"summaryEventsOmitted\":0")==std::string::npos,
            "placement choice flood escaped the global byte bound or hid summary omissions");
        Require(events.find("scheduler.route-result")!=std::string::npos&&
            events.find("scheduler.quality")!=std::string::npos&&events.find("scheduler.final-selection")!=std::string::npos&&
            index.find("\"observerFailed\":false")!=std::string::npos,
            "choice summaries consumed terminal evidence or became an observer failure");
    }
}
void EveryCommittedStateSurvivesAcrossOrderingScopes()
{
    Files files("all-order-states");Options options;
    Tools::NativeConstructionTraceLimits caps;caps.Routes=1;
    caps.ProgressEventsPerRoute=16;caps.ProgressBytesPerRoute=4096;
    caps.DetailEventsPerRoute=16;caps.DetailBytesPerRoute=4096;
    std::vector<std::string> expected;expected.reserve(5*221);
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto noise=Event("route-a","candidate-progress",ConstructionTraceStage::Selected);
        for(std::size_t i=0;i<10000;++i)writer.Record(noise);
        for(std::size_t order=0;order<5;++order)
        {
            auto commit=Event("route-a","constructor.commit",ConstructionTraceStage::Commit);
            commit.Scope="order-"+std::to_string(order)+"/ordinary-array";
            commit.Layouts={{"stock",0,1000,1000,{},0}};
            for(std::size_t i=0;i<220;++i)
            {
                commit.StateID=i+1;commit.ParentStateID=i;commit.Step=order*1000+i+1;
                commit.CandidateID=order*1000+i+101;commit.ProposalID=order*1000+i+501;
                const Pose pose{.5+.25*i,1.5+.125*i,37.5,i%2!=0};
                commit.Members={{i%3,i,pose,i+1}};
                commit.Layouts[0].Placements.push_back({"original",i,pose,i+1,false});
                commit.Layouts[0].MaterialArea=i+1;
                commit.Rank={{"negativeUsedSectionUtilization",-.5-.001*i},
                    {"negativeNetMaterial",-double(i+1)},{"occupiedPlanningArea",1000}};
                writer.Record(commit);expected.push_back(EventJson(commit));
            }
            auto raw=commit;raw.Source="board.raw-construction";raw.Stage=ConstructionTraceStage::Complete;
            raw.Members.clear();writer.Record(raw);expected.push_back(EventJson(raw));
            for(std::size_t i=0;i<100;++i)writer.Record(noise);
        }
        writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
        writer.Record(Event("route-a","scheduler.quality",ConstructionTraceStage::Score));
        writer.Record(Event("final-route","scheduler.final-selection",ConstructionTraceStage::Selected));
        Require(writer.Finish(true),"complete original-state stream unexpectedly failed observation");
    }
    const auto index=Read(files.Index),events=Read(files.Events);
    std::istringstream stream(events);std::string line;std::size_t matched=0;
    const std::string marker=",\"event\":";
    while(std::getline(stream,line))
    {
        if(line.find("\"source\":\"constructor.commit\"")==std::string::npos&&
            line.find("\"source\":\"board.raw-construction\"")==std::string::npos)continue;
        const auto start=line.find(marker);
        Require(start!=std::string::npos&&matched<expected.size(),"state stream has an unexpected or malformed native event");
        const auto begin=start+marker.size();
        Require(line.substr(begin,line.size()-begin-1)==expected[matched++],
            "a real commit/raw state lost its ordering scope, parent, rank, delta or complete stored snapshot");
    }
    Require(matched==1105&&matched==expected.size(),"progress sampling hid a later ordering's actual native states");
    Require(index.find("\"stateEventsSeen\":1105,\"stateEventsWritten\":1105,\"stateEventsOmitted\":0")!=std::string::npos&&
        index.find("\"stateSnapshotsTruncated\":0")!=std::string::npos&&
        index.find("\"coreObservationTruncated\":false")!=std::string::npos,
        "default state reservation did not preserve all five 220-original construction histories");
    Require(events.size()<=ByteCap(caps)&&events.find("scheduler.route-result")!=std::string::npos&&
        events.find("scheduler.quality")!=std::string::npos&&events.find("scheduler.final-selection")!=std::string::npos,
        "full state history escaped its bound or consumed the independent terminal reservation");
}
void StateLimitsExposeLostOrTruncatedEvolution()
{
    for(const int mode:{0,1,2})
    {
        Files files(mode==0?"state-event-limit":mode==1?"state-byte-limit":"state-payload-limit");Options options;
        Tools::NativeConstructionTraceLimits caps;caps.Routes=1;
        caps.TerminalBytesPerRoute=8192;caps.FinalSelectionBytes=8192;
        auto state=Event("route-a","constructor.commit",ConstructionTraceStage::Commit);
        state.StateID=1;state.ParentStateID=0;state.CandidateID=71;state.ProposalID=17;
        state.Members={{2,7,{2.25,3.5,180,true},19}};
        state.Layouts={{"stock",0,1000,1000,{{"original",7,{2.25,3.5,180,true},19,false}},1}};
        if(mode==0){caps.StateEventsPerRoute=2;caps.StateBytesPerRoute=64*1024;}
        else if(mode==1)
        {
            caps.StateEventsPerRoute=16;
            caps.StateBytesPerRoute=std::string("{\"ordinal\":1,\"event\":").size()+EventJson(state).size()+2;
        }
        else
        {
            caps.StateEventsPerRoute=16;caps.StateBytesPerRoute=4096;
            for(std::size_t i=0;i<1000;++i)
                state.Layouts[0].Placements.push_back({"original",i+8,{double(i),.5,37},i+20,false});
        }
        {
            Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
            const std::size_t count=mode==0?3:mode==1?2:1;
            for(std::size_t i=0;i<count;++i)writer.Record(state);
            writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
            writer.Record(Event("route-a","scheduler.quality",ConstructionTraceStage::Score));
            writer.Record(Event("late-final","scheduler.final-selection",ConstructionTraceStage::Selected));
            Require(writer.Finish(true),"bounded state loss incorrectly became an I/O observer failure");
        }
        const auto index=Read(files.Index),events=Read(files.Events);
        const auto counts=mode==0?"\"stateEventsSeen\":3,\"stateEventsWritten\":2,\"stateEventsOmitted\":1":
            mode==1?"\"stateEventsSeen\":2,\"stateEventsWritten\":1,\"stateEventsOmitted\":1":
            "\"stateEventsSeen\":1,\"stateEventsWritten\":1,\"stateEventsOmitted\":0";
        Require(index.find(counts)!=std::string::npos&&index.find("\"coreObservationTruncated\":true")!=std::string::npos&&
            index.find("\"observerFailed\":false")!=std::string::npos,
            "lost actual evolution was not explicitly counted separately from observer failure");
        Require(index.find(mode==2?"\"stateSnapshotsTruncated\":1":"\"stateSnapshotsTruncated\":0")!=std::string::npos,
            "state loss and missing snapshot payloads were conflated");
        if(mode==2)Require(events.find("observation-payload-truncated")!=std::string::npos&&
            events.find("\"observationOriginalPlacements\",\"value\":1001")!=std::string::npos&&
            events.find("\"layouts\":[]")!=std::string::npos,
            "an oversize state silently replaced its true snapshot without diagnostic evidence");
        Require(events.size()<=ByteCap(caps)&&events.find("scheduler.route-result")!=std::string::npos&&
            events.find("scheduler.quality")!=std::string::npos&&events.find("scheduler.final-selection")!=std::string::npos,
            "state overflow consumed the final selection or exceeded the declared bound");
    }
}
void AggregateUnitProofCostsSurviveSeedAndSingleProofFloods()
{
    Files files("unit-proof-summary");Options options;
    Tools::NativeConstructionTraceLimits caps;caps.Routes=1;
    caps.DetailEventsPerRoute=32;caps.DetailBytesPerRoute=64*1024;
    caps.SummaryEventsPerRoute=8;caps.SummaryBytesPerRoute=32*1024;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto proof=Event("route-a","array.unit-original-proof",ConstructionTraceStage::Filter);
        proof.Reason="single-unit-proof";
        for(std::size_t i=0;i<10000;++i)writer.Record(proof);
        auto seed=Event("route-a","array.seed-origin",ConstructionTraceStage::Analysis);
        seed.Reason="seed-summary-before-proof-cost";
        for(std::size_t i=0;i<10000;++i)writer.Record(seed);
        proof.Source="array.unit-original-proof-summary";proof.Stage=ConstructionTraceStage::Complete;
        proof.Reason="aggregate-proof-cost-after-seed-flood";
        proof.Measures={{"proofCalls",10000},{"proofSeconds",12.25}};writer.Record(proof);
        auto choice=Event("route-a","placement.choice",ConstructionTraceStage::Analysis);
        choice.Reason="actual-choice-after-proof-cost";writer.Record(choice);
        Require(writer.Finish(true),"proof summary reservation unexpectedly failed observation");
    }
    const auto index=Read(files.Index),events=Read(files.Events);
    Require(events.find("seed-summary-before-proof-cost")!=std::string::npos&&
        events.find("aggregate-proof-cost-after-seed-flood")!=std::string::npos&&
        events.find("actual-choice-after-proof-cost")!=std::string::npos&&
        events.find("\"proofCalls\",\"value\":10000")!=std::string::npos&&
        events.find("\"proofSeconds\",\"value\":12.25")!=std::string::npos,
        "single proof events or seed summaries hid aggregate original-proof costs and actual choices");
    Require(index.find("\"summaryEventsWritten\":8")!=std::string::npos&&
        index.find("\"coreObservationTruncated\":false")!=std::string::npos&&events.size()<=ByteCap(caps),
        "individual proof records consumed the protected summary lane or escaped its bounded reservation");
}
void ArrayProfilesHaveAnIndependentBoundedReservation()
{
    const Tools::NativeConstructionTraceLimits defaults;
    Require(defaults.PerformanceEventsPerRoute==4096&&defaults.PerformanceBytesPerRoute==8*1024*1024,
        "array performance observation lost its independent default reservation");
    for(const bool exhaustBytes:{false,true})
    {
        Files files(exhaustBytes?"array-profile-bytes":"array-profile-events");Options options;
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        auto first=Event("route-a","array.query-profile",ConstructionTraceStage::Complete);
        first.Reason="late-array-profile-one";first.StateID=7;first.ParentStateID=6;first.Step=10001;
        first.Measures={{"arraysCall",7},{"arraysWallSeconds",101.25},{"singleQueryCalls",80},
            {"leftSeedQueryCalls",40},{"rigidQueryCalls",24},{"queryAttempts",800000},
            {"phasePairPreparationSeconds",12.5},{"phaseFreeDomainBooleanSeconds",3.25},
            {"phasePoseValidationSeconds",75.5},{"phaseTimesNested",1}};
        auto second=first;second.Reason="late-array-profile-two";second.StateID=8;second.ParentStateID=7;second.Step=10002;
        second.Measures[0].second=8;second.Measures[1].second=81.5;
        auto independent=first;independent.Route="route-b";independent.Reason="independent-array-performance-reservation";
        auto commit=Event("route-a","constructor.commit",ConstructionTraceStage::Commit);
        commit.StateID=9;commit.ParentStateID=8;
        commit.Members={{2,7,{2.25,3.5,180,true},19}};
        commit.Layouts={{"stock",0,1000,1000,{{"original",7,{2.25,3.5,180,true},19,false}},1}};
        auto raw=commit;raw.Source="board.raw-construction";raw.Stage=ConstructionTraceStage::Complete;
        auto terminal=Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete);
        terminal.Layouts=commit.Layouts;
        auto quality=Event("route-a","scheduler.quality",ConstructionTraceStage::Score);
        auto final=Event("global-final","scheduler.final-selection",ConstructionTraceStage::Selected);
        final.Layouts=commit.Layouts;
        auto laterProgress=Event("route-a","late-progress-after-performance-flood",ConstructionTraceStage::Complete);
        Tools::NativeConstructionTraceLimits caps;caps.Routes=2;
        caps.DetailEventsPerRoute=16;caps.DetailBytesPerRoute=64*1024;
        caps.ProgressEventsPerRoute=16;caps.ProgressBytesPerRoute=16*1024;
        caps.SummaryEventsPerRoute=8;caps.SummaryBytesPerRoute=16*1024;
        caps.StateEventsPerRoute=2;caps.StateBytesPerRoute=8192;
        caps.TerminalBytesPerRoute=8192;caps.FinalSelectionBytes=8192;
        caps.PerformanceEventsPerRoute=exhaustBytes?128:2;
        // Allow two complete profiles, including any ordinary ordinal width,
        // while leaving less than one serialized record for the later flood.
        caps.PerformanceBytesPerRoute=exhaustBytes?2*(EventJson(first).size()+64):16*1024;
        {
            Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
            auto detail=Event("route-a","nfp.proposal",ConstructionTraceStage::Generate);
            auto summary=Event("route-a","nfp.query-result",ConstructionTraceStage::Analysis);
            auto progress=Event("route-a","candidate-progress",ConstructionTraceStage::Selected);
            for(std::size_t i=0;i<10000;++i)
            {writer.Record(detail);writer.Record(summary);writer.Record(progress);}
            // The source name alone must not reserve performance capacity.
            auto wrongStage=first;wrongStage.Stage=ConstructionTraceStage::Analysis;writer.Record(wrongStage);
            if(exhaustBytes)
            {
                auto oversized=first;oversized.Reason="oversized-array-profile-"+std::string(caps.PerformanceBytesPerRoute,'p');
                writer.Record(oversized);
            }
            writer.Record(first);writer.Record(second);
            auto flood=first;flood.Reason="performance-lane-flood";
            for(std::size_t i=0;i<1000;++i){flood.Step=11000+i;writer.Record(flood);}
            writer.Record(independent);
            writer.Record(laterProgress);
            writer.Record(commit);writer.Record(raw);writer.Record(terminal);writer.Record(quality);writer.Record(final);
            Require(writer.Finish(true),"bounded performance loss incorrectly became an observer failure");
        }
        const auto index=Read(files.Index),events=Read(files.Events);
        const auto completeRecord=[&](const ConstructionTraceEvent& event)
        {
            const auto payload=EventJson(event);
            Require(events.find(",\"event\":"+payload+"}\n")!=std::string::npos,
                "an independent array profile, original state or final record lost its complete native payload");
        };
        completeRecord(first);completeRecord(second);completeRecord(independent);
        completeRecord(laterProgress);
        completeRecord(commit);completeRecord(raw);completeRecord(terminal);completeRecord(quality);completeRecord(final);
        std::istringstream lines(events);std::string line;
        std::size_t profiles=0,profileBytes=0,routeABytes=0,routeBBytes=0,progressWritten=0,summaryWritten=0;
        while(std::getline(lines,line))
        {
            if(line.find("\"source\":\"candidate-progress\"")!=std::string::npos)++progressWritten;
            if(line.find("\"source\":\"nfp.query-result\"")!=std::string::npos)++summaryWritten;
            if(line.find("\"source\":\"array.query-profile\"")==std::string::npos||
                line.find("\"stage\":\"complete\"")==std::string::npos)continue;
            ++profiles;profileBytes+=line.size()+1;
            if(line.find("\"route\":\"route-a\"")!=std::string::npos)routeABytes+=line.size()+1;
            else if(line.find("\"route\":\"route-b\"")!=std::string::npos)routeBBytes+=line.size()+1;
            Require(line.find("observation-payload-truncated")==std::string::npos,
                "array performance reservation silently replaced a complete cumulative profile");
        }
        Require(profiles==3&&routeABytes<=caps.PerformanceBytesPerRoute&&routeBBytes<=caps.PerformanceBytesPerRoute&&
            events.find("performance-lane-flood")==std::string::npos&&events.find("oversized-array-profile-")==std::string::npos,
            "performance event/byte floods escaped their own independent bounded reservation");
        const auto route=[&](const char* id)
        {
            const auto begin=index.find(std::string("\"id\":\"")+id+"\"");
            Require(begin!=std::string::npos,"performance route counters were not published");
            return index.substr(begin,index.find('}',begin)-begin+1);
        };
        const auto a=route("route-a"),b=route("route-b");
        const auto omitted=exhaustBytes?1001u:1000u;
        const auto counts=std::string("\"performanceEventsSeen\":")+std::to_string(1003u+(exhaustBytes?1u:0u))+
            ",\"performanceEventsWritten\":3,\"performanceEventsOmitted\":"+std::to_string(omitted)+
            ",\"performanceBytesWritten\":"+std::to_string(profileBytes);
        const auto routeCounts=std::string("\"performanceEventsSeen\":")+std::to_string(1002u+(exhaustBytes?1u:0u))+
            ",\"performanceEventsWritten\":2,\"performanceEventsOmitted\":"+std::to_string(omitted)+
            ",\"performanceBytesWritten\":"+std::to_string(routeABytes);
        Require(index.find(counts)!=std::string::npos&&a.find(routeCounts)!=std::string::npos&&
            b.find("\"performanceEventsSeen\":1,\"performanceEventsWritten\":1,\"performanceEventsOmitted\":0,\"performanceBytesWritten\":"+
                std::to_string(routeBBytes))!=std::string::npos&&
            a.find("\"performanceObservationTruncated\":true")!=std::string::npos&&
            b.find("\"performanceObservationTruncated\":false")!=std::string::npos,
            "bounded performance loss or per-route/global accounting was hidden or conflated with another lane");
        Require(index.find("\"performanceEventsPerRoute\":"+std::to_string(caps.PerformanceEventsPerRoute))!=std::string::npos&&
            index.find("\"performanceBytesPerRoute\":"+std::to_string(caps.PerformanceBytesPerRoute))!=std::string::npos&&
            index.find("\"eventCap\":"+std::to_string(caps.Routes*(caps.DetailEventsPerRoute+caps.ProgressEventsPerRoute+
                caps.StateEventsPerRoute+caps.PathEventsPerRoute+caps.SummaryEventsPerRoute+caps.PerformanceEventsPerRoute+16)+8))!=std::string::npos&&
            index.find("\"byteCap\":"+std::to_string(ByteCap(caps)))!=std::string::npos&&events.size()<=ByteCap(caps),
            "performance capacity was absent from the published per-route or total byte bounds");
        Require(summaryWritten>0&&progressWritten>0&&
            a.find("\"summaryEventsWritten\":"+std::to_string(summaryWritten)+",\"summaryEventsOmitted\":"+
                std::to_string(10000-summaryWritten))!=std::string::npos&&
            a.find("\"progressEventsOmitted\":"+std::to_string(10000-progressWritten)+",")!=std::string::npos,
            "performance records or their omissions were also charged to summary/progress accounting");
        Require(index.find("\"performanceObservationTruncated\":true")!=std::string::npos&&
            index.find("\"stateEventsSeen\":2,\"stateEventsWritten\":2,\"stateEventsOmitted\":0")!=std::string::npos&&
            index.find("\"coreObservationTruncated\":false")!=std::string::npos&&
            index.find("\"finalSelectionObserved\":true")!=std::string::npos&&
            index.find("\"observerFailed\":false")!=std::string::npos,
            "performance loss consumed core/final reservations or was reported as an observer failure");
    }
}
void ConstructionPhaseBoundariesSurviveObservationFloods()
{
    Files files("construction-phases");Options options;
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    Tools::NativeConstructionTraceLimits caps;caps.Routes=1;
    caps.DetailEventsPerRoute=16;caps.DetailBytesPerRoute=64*1024;
    caps.ProgressEventsPerRoute=16;caps.ProgressBytesPerRoute=16*1024;
    caps.SummaryEventsPerRoute=8;caps.SummaryBytesPerRoute=16*1024;
    caps.PerformanceEventsPerRoute=9;caps.PerformanceBytesPerRoute=32*1024;
    caps.StateEventsPerRoute=2;caps.StateBytesPerRoute=8192;
    caps.TerminalBytesPerRoute=8192;caps.FinalSelectionBytes=8192;
    std::vector<std::string> expected;
    auto profile=Event("route-a","array.query-profile",ConstructionTraceStage::Complete);
    profile.Reason="unchanged-array-query-profile-reservation";
    profile.Measures={{"arrayWallSeconds",1.25},{"queryCalls",3}};
    auto commit=Event("route-a","constructor.commit",ConstructionTraceStage::Commit);
    commit.StateID=42;commit.ParentStateID=41;
    commit.Members={{2,7,{2.25,3.5,180,true},19}};
    commit.Layouts={{"stock",0,1000,1000,{{"original",7,{2.25,3.5,180,true},19,false}},1}};
    auto raw=commit;raw.Source="board.raw-construction";raw.Stage=ConstructionTraceStage::Complete;
    auto final=Event("global-final","scheduler.final-selection",ConstructionTraceStage::Selected);
    final.Layouts=commit.Layouts;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto detail=Event("route-a","nfp.proposal",ConstructionTraceStage::Generate);
        auto summary=Event("route-a","nfp.query-result",ConstructionTraceStage::Analysis);
        auto progress=Event("route-a","candidate-progress",ConstructionTraceStage::Complete);
        for(std::size_t i=0;i<10000;++i)
        {writer.Record(detail);writer.Record(summary);writer.Record(progress);}
        auto wrongStage=Event("route-a","construction.phase",ConstructionTraceStage::Analysis);
        wrongStage.Reason="unreserved-phase-analysis";writer.Record(wrongStage);
        writer.Record(profile);expected.push_back(EventJson(profile));
        for(const char* reason:{"main-combination-array","gap-combination-array","gap-single-nfp","next-blank-area"})
        {
            auto phase=Event("route-a","construction.phase",ConstructionTraceStage::Begin);
            phase.Reason=reason;phase.StateID=41;phase.ParentStateID=40;phase.Step=10001+expected.size();
            phase.BoardWidth=1000;phase.BoardHeight=1000;
            phase.Measures={{"frozenFillTop",12.5},{"remainingOriginals",23}};
            writer.Record(phase);expected.push_back(EventJson(phase));
            phase.Stage=ConstructionTraceStage::Complete;phase.Step=10001+expected.size();
            phase.Measures.push_back({"phaseWallSeconds",2.5});
            writer.Record(phase);expected.push_back(EventJson(phase));
        }
        auto excess=Event("route-a","construction.phase",ConstructionTraceStage::Begin);
        excess.Reason="phase-event-cap-explicitly-omitted";writer.Record(excess);
        writer.Record(commit);writer.Record(raw);
        writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
        writer.Record(Event("route-a","scheduler.quality",ConstructionTraceStage::Score));writer.Record(final);
        Require(writer.Finish(true),"bounded construction-phase observation became an I/O failure");
    }
    const auto events=Read(files.Events),index=Read(files.Index);
    expected.push_back(EventJson(commit));expected.push_back(EventJson(raw));expected.push_back(EventJson(final));
    for(const auto& payload:expected)
        Require(events.find(",\"event\":"+payload+"}\n")!=std::string::npos,
            "detail, summary or complete-stage progress floods hid a full phase boundary, profile, state or final payload");
    std::istringstream lines(events);std::string line;
    std::size_t begins=0,completes=0,performanceBytes=0,summaryWritten=0,progressWritten=0;
    while(std::getline(lines,line))
    {
        if(line.find("\"source\":\"candidate-progress\"")!=std::string::npos)++progressWritten;
        if(line.find("\"source\":\"nfp.query-result\"")!=std::string::npos)++summaryWritten;
        const bool phase=line.find("\"source\":\"construction.phase\"")!=std::string::npos;
        const bool begin=line.find("\"stage\":\"begin\"")!=std::string::npos;
        const bool complete=line.find("\"stage\":\"complete\"")!=std::string::npos;
        if(phase&&begin)++begins;
        if(phase&&complete)++completes;
        if((phase&&(begin||complete))||line.find("\"source\":\"array.query-profile\"")!=std::string::npos)
        {
            performanceBytes+=line.size()+1;
            Require(line.find("observation-payload-truncated")==std::string::npos,
                "a reserved phase boundary or array profile was replaced by an incomplete metric payload");
        }
    }
    Require(begins==4&&completes==4&&events.find("phase-event-cap-explicitly-omitted")==std::string::npos&&
        performanceBytes<=caps.PerformanceBytesPerRoute&&events.size()<=ByteCap(caps),
        "construction phase boundaries escaped or failed their shared independent performance event/byte bound");
    const auto counts=std::string("\"performanceEventsSeen\":10,\"performanceEventsWritten\":9,\"performanceEventsOmitted\":1,")+
        "\"performanceBytesWritten\":"+std::to_string(performanceBytes);
    Require(index.find(counts)!=std::string::npos&&
        index.find("\"performanceObservationTruncated\":true")!=std::string::npos&&
        index.find("\"summaryEventsWritten\":"+std::to_string(summaryWritten)+",\"summaryEventsOmitted\":"+
            std::to_string(10000-summaryWritten))!=std::string::npos&&
        index.find("\"progressEventsOmitted\":"+std::to_string(10000-progressWritten)+",")!=std::string::npos,
        "phase begin/end records were miscounted as ordinary summaries/progress or their bounded loss was hidden");
    Require(index.find("\"stateEventsSeen\":2,\"stateEventsWritten\":2,\"stateEventsOmitted\":0")!=std::string::npos&&
        index.find("\"coreObservationTruncated\":false")!=std::string::npos&&
        index.find("\"finalSelectionObserved\":true")!=std::string::npos&&
        index.find("\"observerFailed\":false")!=std::string::npos,
        "construction phase reservations damaged original state/final evidence or became an observer failure");
}
void CurrentSpaceScoresSurviveObservationFloods()
{
    Files files("current-space-scores");Options options;
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    Tools::NativeConstructionTraceLimits caps;caps.Routes=1;
    caps.DetailEventsPerRoute=16;caps.DetailBytesPerRoute=64*1024;
    caps.ProgressEventsPerRoute=16;caps.ProgressBytesPerRoute=16*1024;
    caps.SummaryEventsPerRoute=8;caps.SummaryBytesPerRoute=16*1024;
    caps.PerformanceEventsPerRoute=6;caps.PerformanceBytesPerRoute=32*1024;
    caps.StateEventsPerRoute=1;caps.StateBytesPerRoute=8192;
    caps.TerminalBytesPerRoute=8192;caps.FinalSelectionBytes=8192;
    auto profile=Event("route-a","array.query-profile",ConstructionTraceStage::Complete);
    profile.Reason="array-profile-still-reserved-beside-space-scores";
    profile.Measures={{"arrayWallSeconds",1.25},{"queryCalls",3}};
    auto phase=Event("route-a","construction.phase",ConstructionTraceStage::Begin);
    phase.Reason="gap-single-nfp";phase.StateID=42;phase.Measures={{"constructionPhase",3},{"frozenFillTop",12.5}};
    auto score=Event("route-a","placement.space-rank",ConstructionTraceStage::Score);
    score.StateID=42;score.ParentStateID=41;score.PartIndex=2;
    score.BoardWidth=1000;score.BoardHeight=1000;
    score.Reason="current-original-free-space-no-future-parts";score.Accepted=true;
    auto commit=Event("route-a","fill.commit",ConstructionTraceStage::Commit);
    commit.StateID=43;commit.ParentStateID=42;commit.CandidateID=102;commit.ProposalID=202;
    commit.Members={{2,7,{4.25,3.5,180,true},19}};
    commit.Layouts={{"stock",0,1000,1000,{{"original",7,{4.25,3.5,180,true},19,true}},20}};
    auto final=Event("global-final","scheduler.final-selection",ConstructionTraceStage::Selected);final.Layouts=commit.Layouts;
    std::vector<std::string> expected;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto detail=Event("route-a","nfp.proposal",ConstructionTraceStage::Generate);
        auto ordinaryScore=Event("route-a","placement.rank",ConstructionTraceStage::Score);
        auto summary=Event("route-a","nfp.query-result",ConstructionTraceStage::Analysis);
        auto progress=Event("route-a","candidate-progress",ConstructionTraceStage::Complete);
        for(std::size_t i=0;i<10000;++i)
        {writer.Record(detail);writer.Record(ordinaryScore);writer.Record(summary);writer.Record(progress);}
        auto wrongStage=score;wrongStage.Stage=ConstructionTraceStage::Analysis;
        wrongStage.Reason="space-score-analysis-not-reserved";writer.Record(wrongStage);
        auto wrongSource=score;wrongSource.Source="placement.space-rank.debug";
        wrongSource.Reason="space-score-other-source-not-reserved";writer.Record(wrongSource);
        writer.Record(profile);expected.push_back(EventJson(profile));
        writer.Record(phase);expected.push_back(EventJson(phase));
        auto oversized=score;oversized.Reason="space-score-byte-cap-omitted-"+std::string(caps.PerformanceBytesPerRoute,'s');
        writer.Record(oversized);
        for(std::size_t i=0;i<3;++i)
        {
            score.Step=10001+i;score.CandidateID=100+i;score.ProposalID=200+i;
            score.Members={{2,7,{2.25+double(i),3.5,180,true},19}};
            score.Rank={{"negativeUsedSectionUtilization",-.75},{"negativeNetMaterial",-20},
                {"negativeLargeRegularFreeSpace",-50.-double(i)},{"fragmentFreeArea",4.-double(i)},
                {"freeBoundaryPerimeter",100.+double(i)},{"negativeAttachment",-2.-double(i)},
                {"orientationOrOccupiedArea",80},{"intrinsicFirstMaterialCornerDistance",0},
                {"primaryAxis",2.25+double(i)},{"secondaryAxis",3.5},{"part",2},{"template",7},{"columns",1},{"rows",2}};
            score.Measures={{"largestFreeArea",100.+double(i)},{"largeRegularFreeSpace",50.+double(i)},
                {"fragmentFreeArea",4.-double(i)},{"freeBoundaryPerimeter",100.+double(i)},
                {"attachment",2.+double(i)},{"freeArea",104},{"freeComponents",2},{"spaceCircleProxy",0},
                {"spaceEvaluationSeconds",.001*double(i+1)},{"spaceCandidatesEvaluated",8},
                {"originalSentinel",1.234e100}};
            const auto original=EventJson(score);writer.Record(score);
            Require(EventJson(score)==original,"observation mutated a current-space candidate");
            expected.push_back(original);
        }
        phase.Stage=ConstructionTraceStage::Complete;writer.Record(phase);expected.push_back(EventJson(phase));
        auto excess=score;excess.Reason="space-score-event-cap-omitted";writer.Record(excess);
        writer.Record(commit);expected.push_back(EventJson(commit));
        writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
        writer.Record(Event("route-a","scheduler.quality",ConstructionTraceStage::Score));
        writer.Record(final);expected.push_back(EventJson(final));
        Require(writer.Finish(true),"bounded current-space observation became an observer failure");
    }
    const auto events=Read(files.Events),index=Read(files.Index);
    for(const auto& payload:expected)
        Require(events.find(",\"event\":"+payload+"}\n")!=std::string::npos,
            "an earlier observation flood changed or hid a full space score/rank/metric/identity, array profile, phase or final payload");
    std::istringstream lines(events);std::string line;std::size_t scores=0,performanceBytes=0;
    while(std::getline(lines,line))
    {
        const bool space=line.find("\"source\":\"placement.space-rank\"")!=std::string::npos&&
            line.find("\"stage\":\"score\"")!=std::string::npos;
        if(space)++scores;
        if(space||line.find("\"source\":\"array.query-profile\"")!=std::string::npos||
            line.find("\"source\":\"construction.phase\"")!=std::string::npos)
        {
            performanceBytes+=line.size()+1;
            Require(line.find("observation-payload-truncated")==std::string::npos,
                "a current-space score was replaced by a partial rank or metric payload");
        }
    }
    Require(scores==3&&performanceBytes<=caps.PerformanceBytesPerRoute&&events.size()<=ByteCap(caps)&&
        events.find("space-score-byte-cap-omitted-")==std::string::npos&&
        events.find("space-score-event-cap-omitted")==std::string::npos&&
        events.find("space-score-analysis-not-reserved")==std::string::npos&&
        events.find("space-score-other-source-not-reserved")==std::string::npos,
        "space-score reservations ignored their exact source/stage or shared event/byte bound");
    Require(index.find("\"performanceEventsSeen\":8,\"performanceEventsWritten\":6,\"performanceEventsOmitted\":2,")!=std::string::npos&&
        index.find("\"performanceBytesWritten\":"+std::to_string(performanceBytes))!=std::string::npos&&
        index.find("\"performanceObservationTruncated\":true")!=std::string::npos&&
        index.find("\"stateEventsSeen\":1,\"stateEventsWritten\":1,\"stateEventsOmitted\":0")!=std::string::npos&&
        index.find("\"payloadsTruncated\":0")!=std::string::npos&&
        index.find("\"coreObservationTruncated\":false")!=std::string::npos&&
        index.find("\"finalSelectionObserved\":true")!=std::string::npos&&
        index.find("\"observerFailed\":false")!=std::string::npos,
        "space-score bounded loss was hidden or damaged original state/final evidence");
}
void LiveReadersCanInspectAnActiveWriter()
{
    Files files("live-read");Options options;
    Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson);
    auto event=Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete);
    event.Reason="flushed-while-construction-is-active";writer.Record(event);
    std::ifstream reader(files.Events,std::ios::binary);
    Require(reader.is_open(),"active construction denied the live report read access");
    const std::string data{std::istreambuf_iterator<char>(reader),{}};
    Require(data.find("flushed-while-construction-is-active")!=std::string::npos,
        "live observer could not read the already-flushed native event");
    Require(Read(files.Index).find("\"runCompleted\":false")!=std::string::npos,
        "live-reader regression accidentally waited until construction finished");
    reader.close();Require(writer.Finish(true),"live observation damaged the construction writer");
}
void LargeTerminalAndGlobalReservation()
{
    Files files("large");Options options;options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    Tools::NativeConstructionTraceLimits caps;caps.Routes=1;caps.DetailBytesPerRoute=2048;caps.ProgressBytesPerRoute=2048;
    caps.TerminalBytesPerRoute=8192;caps.FinalSelectionBytes=8192;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto terminal=Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete);
        terminal.Layouts={{"stock",0,40,40,{},100}};
        for(std::size_t i=0;i<1000;++i)terminal.Layouts[0].Placements.push_back({"original",i,{double(i),.5,37},0,false});
        writer.Record(terminal);writer.Record(Event("route-a","scheduler.quality",ConstructionTraceStage::Score));
        writer.Record(Event("late-route","nfp.proposal",ConstructionTraceStage::Generate));
        writer.Record(Event("new-global-route","scheduler.final-selection",ConstructionTraceStage::Selected));
        Require(writer.Finish(true),"large snapshot truncation became an observer failure");
    }
    const auto index=Read(files.Index),events=Read(files.Events);
    Require(index.find("\"terminalSnapshotsTruncated\":1")!=std::string::npos&&
        index.find("\"finalSelectionObserved\":true")!=std::string::npos&&
        events.find("observationOriginalPlacements")!=std::string::npos&&events.find("scheduler.quality")!=std::string::npos,
        "oversize terminal omission was hidden or consumed final/rank reservations");
}
void SelectedPathsSurviveUnselectedBranchFloods()
{
    Files files("selected-path");Options options;
    Tools::NativeConstructionTraceLimits caps;caps.StateEventsPerRoute=1;caps.PathEventsPerRoute=3;
    {
        Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson,caps);
        auto branch=Event("route-a","constructor.commit",ConstructionTraceStage::Commit);
        branch.Layouts={{"stock",0,20,20,{{"original",0,{.5,.5,0},1,false}},1}};
        for(std::size_t i=1;i<=50;++i){branch.StateID=i;writer.Record(branch);}
        auto path=branch;path.StateID=50;path.Stage=ConstructionTraceStage::Selected;
        path.Source="construction.path.commit";path.Reason="retained-original-commit:constructor.commit";
        writer.Record(path);path.Source="construction.path.raw";path.Stage=ConstructionTraceStage::Complete;writer.Record(path);
        path.Source="strategy.construction-selection";path.Stage=ConstructionTraceStage::Selected;writer.Record(path);
        path.Source="competition.board-selection";writer.Record(path);
        writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
        writer.Record(Event("route-a","scheduler.final-selection",ConstructionTraceStage::Selected));
        Require(writer.Finish(true),"selected-path retention became an observer failure");
    }
    const auto events=Read(files.Events),index=Read(files.Index);
    Require(events.find("construction.path.commit")!=std::string::npos&&events.find("construction.path.raw")!=std::string::npos&&
        events.find("strategy.construction-selection")!=std::string::npos&&events.find("competition.board-selection")==std::string::npos,
        "unselected branches consumed the selected path reservation or the path lane escaped its cap");
    Require(index.find("\"pathEventsWritten\":3")!=std::string::npos&&index.find("\"pathEventsOmitted\":1")!=std::string::npos&&
        index.find("\"pathSnapshotsTruncated\":0")!=std::string::npos&&index.find("\"finalSelectionObserved\":true")!=std::string::npos,
        "selected-path losses or the independently retained final selection were hidden");
    Require(events.size()<=ByteCap(caps),"selected-path reservation escaped the published total byte cap");
}
void IoFailureIsObservable()
{
    Files files("failure");Options options;options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    auto missing=files.Index.parent_path()/files.Index.stem()/files.Index.filename();
    Tools::NativeConstructionTraceWriter writer(missing,files.Result,{},options,ProblemJson);
    writer.Record(Event("route-a","nfp.proposal",ConstructionTraceStage::Generate));
    Require(!writer.Finish(true)&&options.ConstructionObservation->Failed.load(),"I/O failure was not exposed to the caller");
    std::ostringstream status;writer.StatusJson(status);
    Require(status.str().find("\"observerFailed\":true")!=std::string::npos,"final native output cannot report observation failure");
}
void WrittenEventIsNotAlsoOmittedOnIndexFailure()
{
    Files files("index-failure");Options options;
    Tools::NativeConstructionTraceWriter writer(files.Index,files.Result,{},options,ProblemJson);
    auto staging=files.Index;staging+=".tmp";
    std::filesystem::create_directory(staging);
    writer.Record(Event("route-a","scheduler.route-result",ConstructionTraceStage::Complete));
    Require(!writer.Finish(true),"index publication failure was hidden");
    std::ostringstream status;writer.StatusJson(status);
    Require(status.str().find("\"eventsSeen\":1,\"eventsWritten\":1,\"eventsOmitted\":0")!=std::string::npos,
        "an event already written to JSONL was also counted as omitted");
    std::filesystem::remove(staging);
}
}
int main()
{
    try{SelectedPathsSurviveUnselectedBranchFloods();IndependentReservations();PairSummariesSurviveDetailSampling();RigidPhaseSummariesKeepTheirOwnBoundedReservation();PlacementChoicesSurviveQuerySummaryFloods();EveryCommittedStateSurvivesAcrossOrderingScopes();StateLimitsExposeLostOrTruncatedEvolution();AggregateUnitProofCostsSurviveSeedAndSingleProofFloods();ArrayProfilesHaveAnIndependentBoundedReservation();ConstructionPhaseBoundariesSurviveObservationFloods();CurrentSpaceScoresSurviveObservationFloods();LiveReadersCanInspectAnActiveWriter();LargeTerminalAndGlobalReservation();IoFailureIsObservable();WrittenEventIsNotAlsoOmittedOnIndexFailure();
        std::cout<<"PASS bounded unsampled state histories, explicit state/performance loss, protected proof costs, independent detail/progress/performance/phase/current-space/terminal/rank reservations, final selection and I/O failure\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
