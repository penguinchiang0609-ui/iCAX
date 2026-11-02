#include "SheetNesting.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// A deliberately small, versioned text interchange used to remeasure frozen layouts
// with the current header/library. Frozen executables never share a changing ABI.
using namespace iCAX::SheetNesting;
int main(int argc,char** argv)
{
    try
    {
        if(argc<3)throw std::runtime_error("usage: SheetNestingReplay input.dat output.csv [resolution] [certificates.jsonl]");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);
        if(!in||!out)throw std::runtime_error("Cannot open replay input/output.");
        std::string version;in>>version;
        if(version!="ICAX_NESTING_REPLAY_1")throw std::runtime_error("Unknown replay format.");
        const auto resolution=argc>3?static_cast<std::size_t>(std::stoul(argv[3])):64;
        std::ofstream certificates;
        if(argc>4){certificates.open(argv[4]);if(!certificates)throw std::runtime_error("Cannot open certificate output.");certificates<<std::setprecision(17);}
        out<<std::setprecision(17)<<"case,strategy,valid,complete,layouts,lower,upper,measured,seconds,resolution\n";
        std::size_t caseID=0,nparts=0,nstocks=0,nresults=0,count=0;
        while(in>>caseID>>nparts>>nstocks>>nresults)
        {
            Problem p;in>>p.PartGap>>p.EdgeMargin>>p.Tolerance;
            p.Parts.resize(nparts);p.Stocks.resize(nstocks);
            for(auto& part:p.Parts)
            {
                std::size_t angles=0,points=0,holes=0;
                in>>std::quoted(part.ID)>>part.Quantity>>std::quoted(part.Material)>>part.FreeRotation>>angles;
                part.AllowedAnglesDegrees.resize(angles);for(auto& a:part.AllowedAnglesDegrees)in>>a;
                in>>part.Geometry.Radius>>part.Geometry.Center.X>>part.Geometry.Center.Y>>points>>holes;
                part.Geometry.Outer.resize(points);for(auto& a:part.Geometry.Outer)in>>a.X>>a.Y;
                part.Geometry.Holes.resize(holes);
                for(auto& hole:part.Geometry.Holes){in>>points;hole.resize(points);for(auto& a:hole)in>>a.X>>a.Y;}
            }
            for(auto& stock:p.Stocks)in>>std::quoted(stock.ID)>>stock.Width>>stock.Height>>stock.Quantity>>stock.Cost>>std::quoted(stock.Material);
            for(std::size_t r=0;r<nresults;++r)
            {
                std::string strategy;std::size_t nlayouts=0,nremaining=0;int state=0;
                in>>std::quoted(strategy)>>state>>nlayouts>>nremaining;
                Result result;result.State=static_cast<Status>(state);result.Layouts.resize(nlayouts);result.Remaining.resize(nremaining);
                for(auto& layout:result.Layouts)
                {
                    std::size_t placements=0;
                    in>>std::quoted(layout.StockID)>>layout.StockInstance>>layout.Width>>layout.Height>>placements;
                    layout.Placements.resize(placements);
                    for(auto& a:layout.Placements)in>>std::quoted(a.PartID)>>a.Instance>>a.Transform.X>>a.Transform.Y>>a.Transform.AngleDegrees>>a.Group>>a.GapFiller;
                }
                for(auto& remaining:result.Remaining)in>>std::quoted(remaining.PartID)>>remaining.Quantity;
                if(!in)throw std::runtime_error("Malformed replay input near case "+std::to_string(caseID));
                auto valid=Validate(p,result);RemnantEvaluation metric;
                const auto start=std::chrono::steady_clock::now();if(valid.Valid)metric=EvaluateRemnants(p,result,resolution);
                const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                out<<caseID<<','<<strategy<<','<<valid.Valid<<','<<valid.Complete<<','<<nlayouts<<','<<metric.LastSheetRegularArea<<','<<metric.LastSheetAreaUpperBound<<','<<metric.Measured<<','<<elapsed<<','<<resolution<<'\n';
                if(certificates)
                {
                    certificates<<"{\"case\":"<<caseID<<",\"strategy\":"<<std::quoted(strategy)<<",\"remnants\":{\"measured\":"<<(metric.Measured?"true":"false")<<",\"lastSheetRegularArea\":"<<metric.LastSheetRegularArea<<",\"lastSheetAreaUpperBound\":"<<metric.LastSheetAreaUpperBound<<",\"finalLayoutIndices\":[";
                    for(std::size_t i=0;i<metric.FinalLayoutIndices.size();++i){if(i)certificates<<',';certificates<<metric.FinalLayoutIndices[i];}
                    certificates<<"],\"perLayout\":[";
                    for(std::size_t i=0;i<metric.PerLayout.size();++i)
                    {
                        const auto& m=metric.PerLayout[i];if(i)certificates<<',';
                        certificates<<"{\"bounds\":{\"x\":"<<m.Bounds.X<<",\"y\":"<<m.Bounds.Y<<",\"width\":"<<m.Bounds.Width<<",\"height\":"<<m.Bounds.Height<<"},\"area\":"<<m.Area<<",\"areaUpperBound\":"<<m.AreaUpperBound<<",\"verified\":"<<(m.Verified?"true":"false")<<",\"resolution\":"<<m.Resolution<<'}';
                    }
                    certificates<<"]}}\n";
                }
                ++count;
            }
        }
        if(!in.eof())throw std::runtime_error("Truncated replay input.");
        std::cout<<"remeasured="<<count<<" resolution="<<resolution<<'\n';return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
