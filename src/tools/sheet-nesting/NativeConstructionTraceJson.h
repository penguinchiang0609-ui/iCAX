#pragma once
// Shared schema 2 event serialization used by the task CLI and benchmark viewer.
#include "SheetNestingConstructionTrace.h"
#include <cmath>
#include <limits>
#include <ostream>
#include <string>
namespace iCAX::SheetNesting::Tools
{
    inline void TraceText(std::ostream& out, const std::string& text)
    {
        static constexpr char digits[] = "0123456789abcdef";
        out << '"';
        for (const unsigned char c : text)
        {
            switch (c)
            {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u00" << digits[c >> 4] << digits[c & 15];
                else out << static_cast<char>(c);
            }
        }
        out << '"';
    }

    inline void TraceNumber(std::ostream& out,double value)
    {if(std::isfinite(value))out<<value;else out<<"null";}
    inline void TracePoints(std::ostream& out, const std::vector<Point>& points)
    {
        out << '[';
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if (i) out << ',';
            out << '['; TraceNumber(out, points[i].X); out << ','; TraceNumber(out, points[i].Y); out << ']';
        }
        out << ']';
    }

    inline const char* TraceStage(ConstructionTraceStage stage)
    {
        switch(stage)
        {
        case ConstructionTraceStage::Begin:return "begin";
        case ConstructionTraceStage::Analysis:return "analysis";
        case ConstructionTraceStage::Match:return "match";
        case ConstructionTraceStage::Array:return "array";
        case ConstructionTraceStage::Candidate:return "candidate";
        case ConstructionTraceStage::Expand:return "expand";
        case ConstructionTraceStage::Backtrack:return "backtrack";
        case ConstructionTraceStage::Complete:return "complete";
        case ConstructionTraceStage::Selected:return "selected";
        case ConstructionTraceStage::Stopped:return "stopped";
        case ConstructionTraceStage::Generate:return "generate";
        case ConstructionTraceStage::Filter:return "filter";
        case ConstructionTraceStage::Score:return "score";
        case ConstructionTraceStage::Commit:return "commit";
        case ConstructionTraceStage::Budget:return "budget";
        case ConstructionTraceStage::Dispatch:return "dispatch";
        }
        return "unknown";
    }
    inline const char* TraceRole(ConstructionTraceGeometryRole role)
    {
        switch(role)
        {
        case ConstructionTraceGeometryRole::Original:return "original";
        case ConstructionTraceGeometryRole::Carrier:return "carrier";
        case ConstructionTraceGeometryRole::Recess:return "recess";
        case ConstructionTraceGeometryRole::Protrusion:return "protrusion";
        }
        return "unknown";
    }
    inline const char* TraceCarrier(ConstructionTraceCarrierKind kind)
    {
        switch(kind)
        {
        case ConstructionTraceCarrierKind::ConvexHull:return "convex-hull";
        case ConstructionTraceCarrierKind::OuterRectangle:return "outer-rectangle";
        case ConstructionTraceCarrierKind::CoreRectangle:return "core-rectangle";
        }
        return "unknown";
    }
    inline void TraceIndex(std::ostream& out,std::size_t index)
    {
        if(index==std::numeric_limits<std::size_t>::max())out<<"null";
        else out<<index;
    }
    inline void TracePoint(std::ostream& out,Point point)
    {
        out<<'['<<point.X<<','<<point.Y<<']';
    }
    inline void TraceJson(std::ostream& out,const ConstructionTraceEvent& event)
    {
        out<<"{\"stage\":";TraceText(out,TraceStage(event.Stage));
        out<<",\"route\":";TraceText(out,event.Route);out<<",\"scope\":";TraceText(out,event.Scope);out<<",\"source\":";TraceText(out,event.Source);
        const auto values=[&](const char* name,const auto& entries)
        {
            out<<",\""<<name<<"\":[";
            for(std::size_t i=0;i<entries.size();++i)
            {if(i)out<<',';out<<"{\"name\":";TraceText(out,entries[i].first);out<<",\"value\":";
                if(std::isfinite(entries[i].second))out<<entries[i].second;else out<<"null";out<<'}';}
            out<<']';
        };
        values("measures",event.Measures);values("rank",event.Rank);
        out<<",\"layouts\":[";
        for(std::size_t li=0;li<event.Layouts.size();++li)
        {
            if(li)out<<',';const auto& layout=event.Layouts[li];out<<"{\"stockID\":";TraceText(out,layout.StockID);
            out<<",\"stockInstance\":"<<layout.StockInstance<<",\"width\":"<<layout.Width<<",\"height\":"<<layout.Height
                <<",\"materialArea\":"<<layout.MaterialArea<<",\"placements\":[";
            for(std::size_t pi=0;pi<layout.Placements.size();++pi)
            {if(pi)out<<',';const auto& placed=layout.Placements[pi];out<<"{\"part\":";TraceText(out,placed.PartID);
                out<<",\"instance\":"<<placed.Instance<<",\"pose\":{\"x\":"<<placed.Transform.X<<",\"y\":"<<placed.Transform.Y
                    <<",\"angle\":"<<placed.Transform.AngleDegrees<<",\"mirrored\":"<<(placed.Transform.Mirrored?"true":"false")<<"},\"group\":"<<placed.Group<<'}';}
            out<<"]}";
        }
        out<<"],\"remaining\":[";
        for(std::size_t i=0;i<event.Remaining.size();++i)
        {if(i)out<<',';out<<"{\"part\":";TraceText(out,event.Remaining[i].PartID);out<<",\"quantity\":"<<event.Remaining[i].Quantity<<'}';}
        out<<']';
        out<<",\"step\":"<<event.Step<<",\"candidateID\":"<<event.CandidateID<<",\"stateID\":"<<event.StateID
            <<",\"parentStateID\":"<<event.ParentStateID<<",\"proposalID\":"<<event.ProposalID
            <<",\"partIndex\":";TraceIndex(out,event.PartIndex);
        out<<",\"stockIndex\":";TraceIndex(out,event.StockIndex);
        out<<",\"stockInstance\":"<<event.StockInstance<<",\"boardIndex\":"<<event.BoardIndex
            <<",\"boardWidth\":"<<event.BoardWidth<<",\"boardHeight\":"<<event.BoardHeight
            <<",\"width\":"<<event.Width<<",\"height\":"<<event.Height<<",\"materialArea\":"<<event.MaterialArea
            <<",\"saving\":"<<event.Saving<<",\"fill\":"<<event.Fill<<",\"footprintArea\":"<<event.FootprintArea
            <<",\"increment\":"<<event.Increment<<",\"envelopeArea\":"<<event.EnvelopeArea
            <<",\"regularRemnantArea\":"<<event.RegularRemnantArea<<",\"elapsedSeconds\":"<<event.ElapsedSeconds
            <<",\"attempts\":"<<event.Attempts<<",\"expandedStates\":"<<event.ExpandedStates
            <<",\"featureDerived\":"<<(event.FeatureDerived?"true":"false")
            <<",\"arrayDerived\":"<<(event.ArrayDerived?"true":"false")
            <<",\"accepted\":"<<(event.Accepted?"true":"false")
            <<",\"truncated\":"<<(event.Truncated?"true":"false")
            <<",\"analysisTruncated\":"<<(event.AnalysisTruncated?"true":"false")
            <<",\"searchTruncated\":"<<(event.SearchTruncated?"true":"false")
            <<",\"resultStatus\":"<<static_cast<int>(event.ResultStatus)<<",\"reason\":";TraceText(out,event.Reason);
        out<<",\"geometries\":[";
        for(std::size_t i=0;i<event.Geometries.size();++i)
        {
            if(i)out<<',';
            const auto& geometry=event.Geometries[i];
            out<<"{\"role\":";TraceText(out,TraceRole(geometry.Role));
            out<<",\"carrierKind\":";TraceText(out,TraceCarrier(geometry.CarrierKind));
            out<<",\"carrierIndex\":"<<geometry.CarrierIndex<<",\"featureIndex\":"<<geometry.FeatureIndex
                <<",\"radius\":"<<geometry.Radius<<",\"center\":";TracePoint(out,geometry.Center);
            out<<",\"rings\":[";
            for(std::size_t ring=0;ring<geometry.Rings.size();++ring)
            {
                if(ring)out<<',';
                TracePoints(out,geometry.Rings[ring]);
            }
            out<<"]}";
        }
        out<<"],\"mouths\":[";
        for(std::size_t i=0;i<event.Mouths.size();++i)
        {
            if(i)out<<',';
            const auto& mouth=event.Mouths[i];
            out<<"{\"carrierIndex\":"<<mouth.CarrierIndex<<",\"featureIndex\":"<<mouth.FeatureIndex
                <<",\"start\":";TracePoint(out,mouth.Start);
            out<<",\"end\":";TracePoint(out,mouth.End);
            out<<",\"into\":";TracePoint(out,mouth.Into);
            out<<",\"width\":"<<mouth.Width<<",\"depth\":"<<mouth.Depth<<'}';
        }
        out<<"],\"members\":[";
        for(std::size_t i=0;i<event.Members.size();++i)
        {
            if(i)out<<',';
            const auto& member=event.Members[i];
            out<<"{\"partIndex\":"<<member.PartIndex<<",\"instance\":";TraceIndex(out,member.Instance);
            out<<",\"pose\":{\"x\":"<<member.Transform.X<<",\"y\":"<<member.Transform.Y
                <<",\"angle\":"<<member.Transform.AngleDegrees<<",\"mirrored\":"<<(member.Transform.Mirrored?"true":"false")<<"},\"group\":"<<member.Group<<'}';
        }
        out<<"]}";
    }
}
