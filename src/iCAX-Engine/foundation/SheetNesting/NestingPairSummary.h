#pragma once
#include "NestingInternal.h"
#include "NestingTrace.h"
#include <chrono>
#include <optional>
#include <set>

namespace iCAX::SheetNesting::Detail
{
// One bounded record per source specification, independent of candidate-event
// sampling. No material polygons or growing template pools are copied here.
class PairPreparationSummary
{
    using Clock=std::chrono::steady_clock;
public:
    struct Record
    {
        std::size_t PreworkVisits=0,PlannedAngles=0,Entered=0,Queries=0,QueryFailed=0,Generated=0,Exact=0,Legal=0;
        std::size_t EnvelopeRejected=0,ConflictRejected=0,NormalizationRejected=0;
        std::size_t StockRejected=0,FillRejected=0,DuplicateRejected=0,Retained=0;
        std::size_t AngleVisitsOmitted=0,ValidatedAngleVisitsOmitted=0;
        std::set<std::pair<double,double>> Angles;
        std::set<double> ValidatedAngles;
        std::optional<Pattern> Best;
        std::vector<Pattern> PublishedSamples;
        double BestSaving=-1,Seconds=0,PreworkSeconds=0;
        std::string SkipReason;
    };
    struct WorkTime
    {
        Record* Value=nullptr;Clock::time_point Started;bool Preparation=false;
        explicit WorkTime(Record* value,bool preparation=false):Value(value),Preparation(preparation){if(Value)Started=Clock::now();}
        ~WorkTime(){if(Value){const double seconds=std::chrono::duration<double>(Clock::now()-Started).count();
            if(Preparation)Value->PreworkSeconds+=seconds;else Value->Seconds+=seconds;}}
    };
private:
    const Problem& P;const Options& O;const char* Mechanism;
    std::vector<Record> Records;
    std::function<bool()> Stop;
public:
    static constexpr std::size_t MaxParts=256,MaxAngleSamples=32;
    PairPreparationSummary(const Problem& p,const Options& o,const char* mechanism,std::function<bool()> stop)
        :P(p),O(o),Mechanism(mechanism),Records(o.ConstructionTrace?std::min(MaxParts,p.Parts.size()):0),Stop(std::move(stop)){}
    Record* For(std::size_t part){return part<Records.size()?&Records[part]:nullptr;}
    static void Angle(Record* record,double fixed,double moving)
    {
        if(!record)return;
        const std::pair<double,double> pair{NormalizeAngle(fixed),NormalizeAngle(moving)};
        if(record->Angles.contains(pair))return;
        if(record->Angles.size()<MaxAngleSamples)record->Angles.insert(pair);else ++record->AngleVisitsOmitted;
    }
    static void ValidatedAngle(Record* record,double angle)
    {
        if(!record)return;angle=NormalizeAngle(angle);
        if(record->ValidatedAngles.contains(angle))return;
        if(record->ValidatedAngles.size()<MaxAngleSamples)record->ValidatedAngles.insert(angle);else ++record->ValidatedAngleVisitsOmitted;
    }
    void Legal(std::size_t part,const Pattern& pattern)
    {
        auto* record=For(part);if(!record||pattern.Members.size()!=2)return;
        ++record->Legal;double separate=0;
        for(const auto& member:pattern.Members)
        {const auto box=Bounds(P.Parts[member.PartIndex].Geometry,member.Transform);separate+=box.Width*box.Height;}
        const double saving=separate>0?1-pattern.Width*pattern.Height/separate:-1;
        if(!record->Best||saving>record->BestSaving||
            (saving==record->BestSaving&&pattern.Width*pattern.Height<record->Best->Width*record->Best->Height))
        {record->Best=pattern;record->BestSaving=saving;}
    }
    void Retain(const Pattern& pattern)
    {
        if(pattern.Members.size()!=2||pattern.Members[0].PartIndex!=pattern.Members[1].PartIndex)return;
        if(auto* record=For(pattern.Members[0].PartIndex))
        {++record->Retained;if(record->PublishedSamples.size()<2)record->PublishedSamples.push_back(pattern);}
    }
    ~PairPreparationSummary() noexcept
    {
        if(!O.ConstructionTrace)return;
        try
        {
            const bool stopped=Stop&&Stop();
            for(std::size_t i=0;i<Records.size();++i)
            {
                const auto& r=Records[i];const auto& part=P.Parts[i];
                ObserveConstruction(O,ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                {
                    event.Source="preparation.pair-summary";event.Scope+="/";event.Scope+=Mechanism;
                    event.PartIndex=i;event.ElapsedSeconds=r.Seconds;event.Attempts=r.Exact;
                    event.Accepted=r.Retained>0;event.SearchTruncated=stopped;
                    event.Reason=r.Retained?"retained":!r.SkipReason.empty()?r.SkipReason:part.Quantity<2?"insufficient-self-demand":
                        !r.Entered?"not-entered":!r.Queries&&!r.Generated?"no-contact-proposals":
                        !r.Legal?"no-certified-pair":stopped?"budget-before-publication":"discarded-by-carrier-selection";
                    event.Measures={{"sourceQuantity",static_cast<double>(part.Quantity)},
                        {"preworkVisits",static_cast<double>(r.PreworkVisits)},{"preworkSeconds",r.PreworkSeconds},
                        {"plannedAngles",static_cast<double>(r.PlannedAngles)},
                        {"entered",static_cast<double>(r.Entered)},{"nfpQueries",static_cast<double>(r.Queries)},
                        {"nfpQueriesFailed",static_cast<double>(r.QueryFailed)},
                        {"generated",static_cast<double>(r.Generated)},{"exact",static_cast<double>(r.Exact)},
                        {"legal",static_cast<double>(r.Legal)},{"retained",static_cast<double>(r.Retained)},
                        {"envelopeRejected",static_cast<double>(r.EnvelopeRejected)},
                        {"conflictRejected",static_cast<double>(r.ConflictRejected)},
                        {"normalizationRejected",static_cast<double>(r.NormalizationRejected)},
                        {"stockRejected",static_cast<double>(r.StockRejected)},{"fillRejected",static_cast<double>(r.FillRejected)},
                        {"duplicateRejected",static_cast<double>(r.DuplicateRejected)},
                        {"angleSamples",static_cast<double>(r.Angles.size())},
                        {"angleSampleVisitsOmitted",static_cast<double>(r.AngleVisitsOmitted)},
                        {"validatedAngleSamples",static_cast<double>(r.ValidatedAngles.size())},
                        {"validatedAngleSampleVisitsOmitted",static_cast<double>(r.ValidatedAngleVisitsOmitted)},
                        {"summaryPartsOmitted",static_cast<double>(P.Parts.size()-Records.size())}};
                    std::size_t n=0;
                    for(const auto [fixed,moving]:r.Angles)
                    {event.Measures.emplace_back("fixedAngle["+std::to_string(n)+"]",fixed);event.Measures.emplace_back("movingAngle["+std::to_string(n++)+"]",moving);}
                    n=0;for(const auto angle:r.ValidatedAngles)event.Measures.emplace_back("validatedAngle["+std::to_string(n++)+"]",angle);
                    event.Measures.emplace_back("publishedSamples",static_cast<double>(r.PublishedSamples.size()));
                    event.Measures.emplace_back("publishedSamplesOmitted",static_cast<double>(r.Retained-r.PublishedSamples.size()));
                    for(std::size_t k=0;k<r.PublishedSamples.size();++k)
                    {
                        const auto& sample=r.PublishedSamples[k];const auto prefix="published["+std::to_string(k)+"].";
                        event.Measures.emplace_back(prefix+"width",sample.Width);event.Measures.emplace_back(prefix+"height",sample.Height);
                        event.Measures.emplace_back(prefix+"fill",sample.MaterialArea/(sample.Width*sample.Height));
                        for(std::size_t m=0;m<sample.Members.size();++m)
                        {
                            const auto& pose=sample.Members[m].Transform;const auto member=prefix+"member["+std::to_string(m)+"].";
                            event.Measures.emplace_back(member+"x",pose.X);event.Measures.emplace_back(member+"y",pose.Y);
                            event.Measures.emplace_back(member+"angle",pose.AngleDegrees);
                            event.Measures.emplace_back(member+"mirrored",pose.Mirrored?1.:0.);
                        }
                    }
                    if(r.Best)
                    {
                        const auto& best=*r.Best;event.Width=best.Width;event.Height=best.Height;
                        event.MaterialArea=best.MaterialArea;event.EnvelopeArea=best.Width*best.Height;
                        event.Fill=event.EnvelopeArea>0?best.MaterialArea/event.EnvelopeArea:0;event.Saving=r.BestSaving;
                        for(const auto& member:best.Members)event.Members.push_back({member.PartIndex,
                            std::numeric_limits<std::size_t>::max(),member.Transform,0});
                    }
                });
            }
            if(P.Parts.size()>Records.size())ObserveConstruction(O,ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
            {
                event.Source="preparation.pair-summary";event.Scope+="/";event.Scope+=Mechanism;
                event.Reason="summary-specification-cap";event.Truncated=true;
                event.Measures={{"sourceSpecifications",static_cast<double>(P.Parts.size())},
                    {"summarySpecifications",static_cast<double>(Records.size())},
                    {"summaryPartsOmitted",static_cast<double>(P.Parts.size()-Records.size())}};
            });
        }
        catch(...){if(O.ConstructionObservation){O.ConstructionObservation->Failed=true;++O.ConstructionObservation->Omitted;}}
    }
};
}
