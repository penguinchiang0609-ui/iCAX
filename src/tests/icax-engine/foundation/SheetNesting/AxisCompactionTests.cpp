#include "SheetNestingAxisCompaction.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

using namespace iCAX::SheetNesting;

namespace
{
    int Passed=0,Failed=0,Unsupported=0;
    std::filesystem::path ArtifactDirectory;
    void Require(bool value,const std::string& message)
    {if(!value)throw std::runtime_error(message);}
    void Test(const char* name,const std::function<void()>& body)
    {
        try{body();++Passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++Failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    }
    Part Box(std::string id,double width,double height,std::size_t quantity=1,Point origin={})
    {
        Part part;part.ID=std::move(id);part.Material="metal";part.Quantity=quantity;
        part.FreeRotation=false;part.AllowedAnglesDegrees={0};
        part.Geometry.Outer={{origin.X,origin.Y},{origin.X+width,origin.Y},
            {origin.X+width,origin.Y+height},{origin.X,origin.Y+height}};
        return part;
    }
    Problem Sheet(double width=200,double height=200,std::size_t quantity=1)
    {
        Problem problem;problem.Tolerance=1e-7;
        problem.Stocks.push_back({"sheet",width,height,quantity,1,"metal"});return problem;
    }
    Layout Board(const Problem& problem,std::size_t instance=0)
    {
        Layout layout;layout.StockID=problem.Stocks.front().ID;layout.StockInstance=instance;
        layout.Width=problem.Stocks.front().Width;layout.Height=problem.Stocks.front().Height;
        return layout;
    }
    Result At(const Problem& problem,std::vector<Placement> placements)
    {
        Result result;result.State=Status::Complete;auto layout=Board(problem);
        layout.Placements=std::move(placements);result.Layouts.push_back(std::move(layout));return result;
    }
    Options Budget()
    {Options options;options.TimeLimitSeconds=30;options.MaxPlacementAttempts=2000000;return options;}
    std::string Errors(const Validation& checked)
    {
        std::string errors;for(const auto& error:checked.Errors)errors+=error+"; ";return errors;
    }
    void Valid(const Problem& problem,const Result& result,bool complete=true)
    {
        const auto checked=Validate(problem,result);
        Require(checked.Valid,Errors(checked));
        Require(checked.Complete==complete,"original inventory completeness changed");
    }
    using Identity=std::pair<std::string,std::size_t>;
    using BoundPose=std::tuple<std::string,std::size_t,Pose,std::size_t,bool>;
    std::map<Identity,BoundPose> Poses(const Result& result)
    {
        std::map<Identity,BoundPose> poses;
        for(const auto& board:result.Layouts)for(const auto& placement:board.Placements)
            Require(poses.emplace(Identity{placement.PartID,placement.Instance},BoundPose{board.StockID,
                board.StockInstance,placement.Transform,placement.Group,placement.GapFiller}).second,
                "duplicate identity in compaction output");
        return poses;
    }
    std::string GeometrySignature(const Result& result)
    {
        std::ostringstream text;text.precision(17);
        text<<static_cast<int>(result.State)<<';';
        for(const auto& board:result.Layouts)
        {
            text<<board.StockID<<':'<<board.StockInstance<<':'<<board.Width<<':'<<board.Height<<':'<<board.MaterialArea<<';';
            for(const auto& placement:board.Placements)
                text<<placement.PartID<<':'<<placement.Instance<<':'<<placement.Transform.X<<':'<<placement.Transform.Y
                    <<':'<<placement.Transform.AngleDegrees<<':'<<placement.Group<<':'<<placement.GapFiller<<';';
        }
        for(const auto& remaining:result.Remaining)text<<remaining.PartID<<':'<<remaining.Quantity<<';';
        return text.str();
    }
    void Conserved(const Problem& problem,const Result& before,const Result& after)
    {
        Valid(problem,before,before.Remaining.empty());Valid(problem,after,before.Remaining.empty());
        Require(before.Layouts.size()==after.Layouts.size(),"basic translation changed consumed sheet count");
        const auto old=Poses(before),now=Poses(after);Require(old.size()==now.size(),"original placement count changed");
        for(const auto& [key,value]:old)
        {
            const auto it=now.find(key);Require(it!=now.end(),"original instance disappeared");
            const auto& next=it->second;
            Require(std::get<0>(value)==std::get<0>(next)&&std::get<1>(value)==std::get<1>(next),
                "basic translation moved an original to another physical sheet");
            Require(std::get<2>(value).AngleDegrees==std::get<2>(next).AngleDegrees,
                "fixed authoritative rotation changed");
            Require(std::get<3>(value)==std::get<3>(next)&&std::get<4>(value)==std::get<4>(next),
                "construction group or gap-filler provenance changed");
            Require(std::get<2>(next).X<=std::get<2>(value).X+problem.Tolerance&&
                std::get<2>(next).Y<=std::get<2>(value).Y+problem.Tolerance,
                "left/down translation moved an original right or up");
        }
        std::map<std::string,std::size_t> missingBefore,missingAfter;
        for(const auto& item:before.Remaining)missingBefore[item.PartID]+=item.Quantity;
        for(const auto& item:after.Remaining)missingAfter[item.PartID]+=item.Quantity;
        Require(missingBefore==missingAfter,"translation changed remaining original demand");
    }
    double Movement(const Result& before,const Result& after)
    {
        const auto a=Poses(before),b=Poses(after);double movement=0;
        for(const auto& [key,value]:a)
        {const auto& pose=std::get<2>(value);const auto& next=std::get<2>(b.at(key));movement+=std::abs(pose.X-next.X)+std::abs(pose.Y-next.Y);}
        return movement;
    }
    struct StrictBox
    {
        double Left=0,Bottom=0,Right=0,Top=0;
    };
    Point QuarterTurn(Point point,double angle)
    {
        const auto quarter=static_cast<int>(std::llround(angle/90.));
        Require(angle==quarter*90.,"strict rectangle checker requires an authoritative exact quarter turn");
        switch((quarter%4+4)%4)
        {
        case 0:return point;
        case 1:return {-point.Y,point.X};
        case 2:return {-point.X,-point.Y};
        default:return {point.Y,-point.X};
        }
    }
    StrictBox IndependentRectangleBounds(const Part& part,const Pose& pose)
    {
        Require(part.Geometry.Radius==0&&part.Geometry.Holes.empty()&&part.Geometry.Outer.size()==4,
            "strict rectangle fixture contains a different material shape");
        StrictBox box;box.Left=box.Bottom=std::numeric_limits<double>::infinity();
        box.Right=box.Top=-std::numeric_limits<double>::infinity();
        for(const auto source:part.Geometry.Outer)
        {
            auto point=QuarterTurn(source,pose.AngleDegrees);point.X+=pose.X;point.Y+=pose.Y;
            box.Left=std::min(box.Left,point.X);box.Right=std::max(box.Right,point.X);
            box.Bottom=std::min(box.Bottom,point.Y);box.Top=std::max(box.Top,point.Y);
        }
        return box;
    }
    void StrictRectangleGeometry(const Problem& problem,const Result& result)
    {
        // Compare the actual written binary64 positions with nominal material,
        // gap and margin. No Problem::Tolerance or native conflict helper enters
        // these assertions. Quarter turns are exact coordinate permutations.
        std::map<std::string,const Part*> parts;
        for(const auto& part:problem.Parts)parts.emplace(part.ID,&part);
        for(const auto& board:result.Layouts)
        {
            std::vector<StrictBox> boxes;
            for(const auto& placement:board.Placements)
            {
                const auto box=IndependentRectangleBounds(*parts.at(placement.PartID),placement.Transform);
                Require(box.Left>=problem.EdgeMargin&&box.Bottom>=problem.EdgeMargin&&
                    box.Right<=board.Width-problem.EdgeMargin&&box.Top<=board.Height-problem.EdgeMargin,
                    "strict written rectangle endpoint crossed the nominal usable sheet boundary");
                for(std::size_t j=0;j<boxes.size();++j)
                {
                    const auto& other=boxes[j];
                    const double overlapX=std::min(box.Right,other.Right)-std::max(box.Left,other.Left);
                    const double overlapY=std::min(box.Top,other.Top)-std::max(box.Bottom,other.Bottom);
                    std::ostringstream detail;detail<<std::setprecision(17)<<board.Placements[j].PartID<<" / "
                        <<placement.PartID<<" overlap="<<overlapX<<','<<overlapY;
                    Require(!(overlapX>0&&overlapY>0),"strict original rectangle material overlap: "+detail.str());
                    const double dx=std::max({other.Left-box.Right,box.Left-other.Right,0.});
                    const double dy=std::max({other.Bottom-box.Top,box.Bottom-other.Top,0.});
                    const double distance=std::hypot(dx,dy);
                    detail<<" distance="<<distance<<" nominalGap="<<problem.PartGap;
                    Require(distance>=problem.PartGap,"strict original rectangle gap deficit: "+detail.str());
                }
                boxes.push_back(box);
            }
        }
    }
    struct LongPoint{long double X=0,Y=0;};
    std::vector<LongPoint> IndependentConvexWorld(const Part& part,const Pose& pose)
    {
        Require(part.Geometry.Radius==0&&part.Geometry.Holes.empty(),"independent SAT fixture must be a simple convex polygon");
        std::vector<LongPoint> ring;
        const auto quarter=std::llround(pose.AngleDegrees/90.);
        const bool exactQuarter=pose.AngleDegrees==quarter*90.;
        const long double radians=static_cast<long double>(pose.AngleDegrees)*std::acos(-1.L)/180;
        const long double cosine=std::cos(radians),sine=std::sin(radians);
        for(const auto source:part.Geometry.Outer)
        {
            if(exactQuarter)
            {
                const auto point=QuarterTurn(source,pose.AngleDegrees);
                ring.push_back({static_cast<long double>(point.X)+pose.X,static_cast<long double>(point.Y)+pose.Y});
            }
            else ring.push_back({source.X*cosine-source.Y*sine+pose.X,source.X*sine+source.Y*cosine+pose.Y});
        }
        return ring;
    }
    bool ConvexInteriorOverlap(const std::vector<LongPoint>& a,const std::vector<LongPoint>& b,long double roundoff)
    {
        for(const auto* edges:{&a,&b})for(std::size_t i=0;i<edges->size();++i)
        {
            const auto start=(*edges)[i],end=(*edges)[(i+1)%edges->size()];
            const long double nx=start.Y-end.Y,ny=end.X-start.X,length=std::hypot(nx,ny);
            if(length==0)continue;
            const auto range=[&](const std::vector<LongPoint>& ring)
            {
                long double low=std::numeric_limits<long double>::infinity(),high=-low;
                for(const auto point:ring)
                {
                    const long double projection=((point.X-start.X)*nx+(point.Y-start.Y)*ny)/length;
                    low=std::min(low,projection);high=std::max(high,projection);
                }
                return std::make_pair(low,high);
            };
            const auto first=range(a),second=range(b);
            if(std::min(first.second,second.second)-std::max(first.first,second.first)<=roundoff)return false;
        }
        return true;
    }
    long double PointSegmentDistance(LongPoint point,LongPoint a,LongPoint b)
    {
        const long double dx=b.X-a.X,dy=b.Y-a.Y,length2=dx*dx+dy*dy;
        const long double parameter=length2==0?0:std::clamp(((point.X-a.X)*dx+(point.Y-a.Y)*dy)/length2,0.L,1.L);
        return std::hypot(point.X-a.X-parameter*dx,point.Y-a.Y-parameter*dy);
    }
    void IndependentConvexGeometry(const Problem& problem,const Result& result)
    {
        // This independent SAT/distance oracle detects material intrusion above
        // binary64 projection roundoff. MSVC long double is also binary64, so it
        // is a visible-penetration regression, not an exact rotation certificate.
        std::map<std::string,const Part*> parts;
        for(const auto& part:problem.Parts)parts.emplace(part.ID,&part);
        for(const auto& board:result.Layouts)
        {
            const long double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,board.Width,board.Height});
            std::vector<std::vector<LongPoint>> rings;
            for(const auto& placement:board.Placements)
            {
                const auto ring=IndependentConvexWorld(*parts.at(placement.PartID),placement.Transform);
                for(const auto point:ring)
                    Require(point.X+roundoff>=problem.EdgeMargin&&point.Y+roundoff>=problem.EdgeMargin&&
                        point.X-roundoff<=board.Width-problem.EdgeMargin&&point.Y-roundoff<=board.Height-problem.EdgeMargin,
                        "independent convex vertex crossed the original usable sheet");
                for(std::size_t j=0;j<rings.size();++j)
                {
                    const auto& other=rings[j];
                    const auto pair=board.Placements[j].PartID+" / "+placement.PartID;
                    Require(!ConvexInteriorOverlap(ring,other,roundoff),"independent SAT found original convex material intrusion: "+pair);
                    if(problem.PartGap>0)
                    {
                        long double distance=std::numeric_limits<long double>::infinity();
                        for(const auto* first:{&ring,&other})
                        {
                            const auto& second=first==&ring?other:ring;
                            for(const auto point:*first)for(std::size_t edge=0;edge<second.size();++edge)
                                distance=std::min(distance,PointSegmentDistance(point,second[edge],second[(edge+1)%second.size()]));
                        }
                        Require(distance+roundoff>=problem.PartGap,"independent convex edge distance lost the nominal original gap: "+pair);
                    }
                }
                rings.push_back(ring);
            }
        }
    }
    struct Fixture{Problem Input;Result Floating;};
    Fixture FloatingCells(std::uint64_t seed,std::size_t types,std::size_t quantity,std::size_t sheets,
        bool arbitraryAngles,double gap,double margin)
    {
        Fixture fixture;fixture.Input=Sheet(800,800,sheets);fixture.Input.PartGap=gap;fixture.Input.EdgeMargin=margin;
        std::mt19937_64 random(seed);std::uniform_real_distribution<double> size(7,27),offset(0,13),angle(-177,179);
        for(std::size_t i=0;i<types;++i)
        {
            auto part=Box("type-"+std::to_string(i),size(random),size(random),quantity,{-2.-i%3,3.+i%5});
            if(arbitraryAngles){part.FreeRotation=true;part.AllowedAnglesDegrees.clear();}
            fixture.Input.Parts.push_back(std::move(part));
        }
        fixture.Floating.State=Status::Complete;
        for(std::size_t i=0;i<sheets;++i)fixture.Floating.Layouts.push_back(Board(fixture.Input,i));
        std::vector<std::size_t> cells(sheets);
        for(std::size_t i=0;i<types;++i)for(std::size_t instance=0;instance<quantity;++instance)
        {
            const auto sheet=(i*quantity+instance)%sheets,cell=cells[sheet]++;
            Require(cell<36,"floating generator exceeded independent legal cells");
            Pose pose;pose.AngleDegrees=arbitraryAngles?angle(random):0;
            const auto bounds=Detail::Bounds(fixture.Input.Parts[i].Geometry,pose);
            pose.X=margin+45+(cell%6)*100+offset(random)-bounds.X;
            pose.Y=margin+45+(cell/6)*100+offset(random)-bounds.Y;
            fixture.Floating.Layouts[sheet].Placements.push_back({fixture.Input.Parts[i].ID,instance,pose,
                (instance%3==0)?i+1:0,instance%5==0});
        }
        Valid(fixture.Input,fixture.Floating);return fixture;
    }
    struct Outcome
    {
        Result Output;
        AxisCompactionReport Report;
        bool Closed=false,Supported=true;
        std::string StopReason;
        double ClosureTolerance=0;
    };
    // This test adapter is bound to the public stage/report API below. Tests
    // inspect physical originals, not the compactor's internal carrier shapes.
    Outcome CompactWithSettings(const Problem& problem,const Result& before,const AxisCompactionSettings& settings,
        const Options& options=Budget())
    {
        Outcome outcome;outcome.Output=MakeAxisCompactionStage(settings).Factory()->Run(problem,before,options);
        const auto report=GetAxisCompactionReport(problem,outcome.Output);
        Require(report.has_value(),"stage did not publish an output-bound compaction report");outcome.Report=*report;
        outcome.Closed=report->Closed;outcome.Supported=report->Supported;
        outcome.ClosureTolerance=report->ClosureTolerance;
        outcome.StopReason="reason="+std::to_string(static_cast<int>(report->StopReason))+
            ", passes="+std::to_string(report->CompletedPasses)+", moves="+std::to_string(report->Moves);
        if(outcome.Closed)
            Require(report->GeometryValidated&&report->StopReason==AxisCompactionStopReason::EventNeighborhoodClosed,
                "closure did not come from a validated complete event-neighborhood cycle");
        return outcome;
    }
    Outcome Compact(const Problem& problem,const Result& before,Options options=Budget(),std::size_t maxCycles=32)
    {
        AxisCompactionSettings settings;settings.MaxPasses=maxCycles;
        return CompactWithSettings(problem,before,settings,options);
    }
    void Closed(const Outcome& outcome)
    {
        Require(outcome.Supported,"expected supported polygon fixture: "+outcome.StopReason);
        Require(outcome.Closed,"expected a completed no-move cycle: "+outcome.StopReason);
    }
    void NoMutation(const Result& before,const Result& after)
    {
        auto normalized=after;normalized.State=before.State;
        Require(GeometrySignature(before)==GeometrySignature(normalized),"stopped or invalid-source operation changed original geometry");
    }
    bool SupportedOrExplicit(const Result& before,const Outcome& outcome,const char* name)
    {
        if(outcome.Supported)return true;
        Require(!outcome.Closed,"unsupported geometry was labelled closed");NoMutation(before,outcome.Output);
        ++Unsupported;std::cout<<"UNSUPPORTED "<<name<<": "<<outcome.StopReason<<'\n';return false;
    }
    void Repeatable(const Problem& problem,const Result& before,const Outcome& once)
    {
        const auto twice=Compact(problem,before);Closed(twice);
        Require(GeometrySignature(once.Output)==GeometrySignature(twice.Output),"same originals/settings did not produce deterministic placements");
        const auto again=Compact(problem,once.Output);Closed(again);Conserved(problem,once.Output,again.Output);
        const double threshold=std::max(once.ClosureTolerance,again.ClosureTolerance);
        const auto a=Poses(once.Output),b=Poses(again.Output);
        for(const auto& [key,value]:a)
        {
            const auto old=std::get<2>(value),next=std::get<2>(b.at(key));
            Require(std::abs(old.X-next.X)<=threshold+problem.Tolerance&&std::abs(old.Y-next.Y)<=threshold+problem.Tolerance,
                "closed output still had a threshold-exceeding translation on rerun");
        }
    }
    struct ArtifactEntry
    {
        std::string Stem,Title;
        std::size_t Parts=0,Sheets=0,Moves=0,Passes=0;
        double Gap=0,Margin=0,ClosureTolerance=0;
        bool Closed=false;
    };
    std::vector<ArtifactEntry> Artifacts;

    std::string Escape(const std::string& value)
    {
        std::string escaped;
        for(const char letter:value)
        {
            switch(letter)
            {
            case '&':escaped+="&amp;";break;
            case '<':escaped+="&lt;";break;
            case '>':escaped+="&gt;";break;
            case '"':escaped+="&quot;";break;
            default:escaped+=letter;break;
            }
        }
        return escaped;
    }
    void WriteArtifact(const std::string& name,const std::string& contents)
    {
        std::ofstream file(ArtifactDirectory/name,std::ios::binary);
        Require(static_cast<bool>(file),"could not open artifact "+name);
        file<<contents;
        Require(static_cast<bool>(file),"could not write artifact "+name);
    }
    std::pair<double,double> SharedWindow(const Problem& problem,const Result& before,const Result& after)
    {
        double right=0,top=0;
        std::map<std::string,const Part*> parts;
        for(const auto& part:problem.Parts)parts.emplace(part.ID,&part);
        for(const auto* result:{&before,&after})
            for(const auto& board:result->Layouts)
                for(const auto& placement:board.Placements)
                {
                    const auto bounds=Detail::Bounds(parts.at(placement.PartID)->Geometry,placement.Transform);
                    right=std::max(right,bounds.X+bounds.Width);
                    top=std::max(top,bounds.Y+bounds.Height);
                }
        return {std::ceil(right+15),std::ceil(top+15)};
    }
    void RingPath(std::ostream& text,const std::vector<Point>& ring)
    {
        if(ring.empty())return;
        text<<"M "<<ring.front().X<<' '<<ring.front().Y;
        for(std::size_t i=1;i<ring.size();++i)text<<" L "<<ring[i].X<<' '<<ring[i].Y;
        text<<" Z ";
    }
    std::string LayoutSvg(const Problem& problem,const Result& result,const std::string& caption,
        std::pair<double,double> window,bool fullBoard)
    {
        std::map<std::string,std::pair<const Part*,std::size_t>> parts;
        for(std::size_t i=0;i<problem.Parts.size();++i)parts.emplace(problem.Parts[i].ID,std::make_pair(&problem.Parts[i],i));
        std::vector<const Layout*> boards;
        double width=0,height=0;
        for(const auto& board:result.Layouts)
        {
            boards.push_back(&board);
            width=std::max(width,fullBoard?board.Width:std::min(board.Width,window.first));
            height=std::max(height,fullBoard?board.Height:std::min(board.Height,window.second));
        }
        std::sort(boards.begin(),boards.end(),[](const Layout* a,const Layout* b)
        {return std::tie(a->StockID,a->StockInstance)<std::tie(b->StockID,b->StockInstance);});
        const auto columns=std::min<std::size_t>(2,boards.size());
        const auto rows=(boards.size()+columns-1)/columns;
        const double padding=width*.04,fontSize=width/32,heading=fontSize*3;
        const double lineWidth=std::min(width,height)/400;
        const double canvasWidth=columns*(width+padding)+padding;
        const double canvasHeight=rows*(height+heading+padding)+padding;
        std::ostringstream svg;svg<<std::setprecision(17);
        svg<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<canvasWidth<<"\" height=\""<<canvasHeight
            <<"\" viewBox=\"0 0 "<<canvasWidth<<' '<<canvasHeight<<"\" role=\"img\"><title>"<<Escape(caption)<<"</title>"
            <<"<rect width=\"100%\" height=\"100%\" fill=\"#f0f3f5\"/>";
        for(std::size_t i=0;i<boards.size();++i)
        {
            const auto& board=*boards[i];
            const double x=padding+(i%columns)*(width+padding);
            const double y=padding+heading+(i/columns)*(height+heading+padding);
            const double shownWidth=fullBoard?board.Width:std::min(board.Width,window.first);
            const double shownHeight=fullBoard?board.Height:std::min(board.Height,window.second);
            svg<<"<text x=\""<<x<<"\" y=\""<<y-fontSize*1.6<<"\" font-size=\""<<fontSize<<"\" fill=\"#263646\" font-family=\"sans-serif\">"
                <<Escape(board.StockID)<<" #"<<board.StockInstance+1<<" · "<<board.Width<<"×"<<board.Height;
            svg<<"</text><text x=\""<<x<<"\" y=\""<<y-fontSize*.35<<"\" font-size=\""<<fontSize<<"\" fill=\"#5a6b78\" font-family=\"sans-serif\">";
            if(fullBoard)svg<<"完整板材";
            else svg<<"左下视窗 "<<shownWidth<<"×"<<shownHeight;
            svg<<"</text><defs><clipPath id=\"clip-"<<i<<"\"><rect width=\""<<shownWidth<<"\" height=\""<<shownHeight
                <<"\"/></clipPath></defs><g transform=\"translate("<<x<<' '<<y+shownHeight<<") scale(1 -1)\" clip-path=\"url(#clip-"<<i<<")\">"
                <<"<rect width=\""<<board.Width<<"\" height=\""<<board.Height<<"\" fill=\"white\" stroke=\"#37495b\" stroke-width=\""<<lineWidth*1.5<<"\"/>";
            if(problem.EdgeMargin>0)
                svg<<"<rect x=\""<<problem.EdgeMargin<<"\" y=\""<<problem.EdgeMargin<<"\" width=\""<<board.Width-2*problem.EdgeMargin
                    <<"\" height=\""<<board.Height-2*problem.EdgeMargin<<"\" fill=\"none\" stroke=\"#a2adb8\" stroke-width=\""<<lineWidth
                    <<"\" stroke-dasharray=\""<<lineWidth*6<<' '<<lineWidth*4<<"\"/>";
            for(const auto& placement:board.Placements)
            {
                const auto& entry=parts.at(placement.PartID);const auto& shape=entry.first->Geometry;
                const auto& pose=placement.Transform;
                svg<<"<g data-part=\""<<Escape(placement.PartID)<<"\" data-instance=\""<<placement.Instance
                    <<"\" data-x=\""<<pose.X<<"\" data-y=\""<<pose.Y<<"\" data-angle=\""<<pose.AngleDegrees
                    <<"\" transform=\"translate("<<pose.X<<' '<<pose.Y<<") rotate("<<pose.AngleDegrees<<")\" fill=\"hsl("
                    <<(entry.second*137+25)%360<<" 52% 64%)\" stroke=\"#243849\" stroke-width=\""<<lineWidth<<"\">"
                    <<"<title>"<<Escape(placement.PartID)<<" #"<<placement.Instance<<" · "<<pose.AngleDegrees<<"°</title>";
                if(shape.Radius>0)
                    svg<<"<circle cx=\""<<shape.Center.X<<"\" cy=\""<<shape.Center.Y<<"\" r=\""<<shape.Radius<<"\"/>";
                else
                {
                    svg<<"<path fill-rule=\"evenodd\" d=\"";RingPath(svg,shape.Outer);
                    for(const auto& hole:shape.Holes)RingPath(svg,hole);
                    svg<<"\"/>";
                }
                svg<<"</g>";
            }
            svg<<"</g>";
        }
        svg<<"</svg>";return svg.str();
    }
    void RecordArtifact(const std::string& stem,const std::string& title,const Problem& problem,
        const Result& before,const Outcome& outcome)
    {
        if(ArtifactDirectory.empty())return;
        Conserved(problem,before,outcome.Output);
        const auto report=GetAxisCompactionReport(problem,outcome.Output);
        Require(report.has_value(),"artifact output lost its original problem/pose binding");
        const auto window=SharedWindow(problem,before,outcome.Output);
        WriteArtifact(stem+"-before.svg",LayoutSvg(problem,before,title+" · 压紧前",window,false));
        WriteArtifact(stem+"-after.svg",LayoutSvg(problem,outcome.Output,title+" · 压紧后",window,false));
        WriteArtifact(stem+"-before-full.svg",LayoutSvg(problem,before,title+" · 压紧前完整板",window,true));
        WriteArtifact(stem+"-after-full.svg",LayoutSvg(problem,outcome.Output,title+" · 压紧后完整板",window,true));
        const auto checked=Validate(problem,outcome.Output);
        std::ostringstream metadata;metadata<<std::setprecision(17)<<std::boolalpha;
        metadata<<"{\"title\":\""<<title<<"\",\"beforePlaced\":"<<Validate(problem,before).PlacedCount
            <<",\"afterPlaced\":"<<checked.PlacedCount<<",\"beforeSheets\":"<<before.Layouts.size()
            <<",\"afterSheets\":"<<outcome.Output.Layouts.size()<<",\"beforeAndAfterOriginalValidationPassed\":true"
            <<",\"physicalIdentitySheetAngleAndProvenanceConserved\":true,\"partGap\":"<<problem.PartGap
            <<",\"edgeMargin\":"<<problem.EdgeMargin<<",\"tolerance\":"<<problem.Tolerance
            <<",\"moves\":"<<report->Moves<<",\"singleMoves\":"<<report->SingleMoves<<",\"taggedGroupMoves\":"<<report->TaggedGroupMoves
            <<",\"wholeSheetMoves\":"<<report->WholeSheetMoves<<",\"completedPasses\":"<<report->CompletedPasses
            <<",\"closed\":"<<report->Closed<<",\"closureTolerance\":"<<report->ClosureTolerance
            <<",\"closedScope\":\"fixed-angle left/down event neighborhood, whole-sheet/tagged-group/single-original candidates\""
            <<",\"problemFingerprint\":\""<<report->ProblemFingerprint<<"\",\"outputFingerprint\":\""<<report->OutputFingerprint
            <<"\",\"samePhysicalScaleBeforeAndAfter\":true,\"sharedLowerLeftWindow\":["<<window.first<<','<<window.second
            <<"],\"source\":\"actual independent native stage output from generic mechanism fixture\"}";
        WriteArtifact(stem+"-verification.json",metadata.str());
        Artifacts.push_back({stem,title,checked.PlacedCount,outcome.Output.Layouts.size(),report->Moves,report->CompletedPasses,
            problem.PartGap,problem.EdgeMargin,report->ClosureTolerance,report->Closed});
    }
    void WriteArtifactIndex()
    {
        if(ArtifactDirectory.empty())return;
        std::ostringstream page;page<<std::setprecision(8);
        page<<"<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            <<"<title>基础压紧机制 · 实际原生输出</title><style>body{margin:0;background:#f1f4f6;color:#233443;font:16px/1.65 system-ui,sans-serif}"
            <<"main{max-width:1600px;margin:auto;padding:28px}section{background:white;border:1px solid #d6dfe6;border-radius:10px;padding:22px;margin:24px 0}"
            <<"h1{font-size:28px}h2{font-size:22px;margin-top:0}.sub{color:#5c6e7c}.pair{display:grid;grid-template-columns:1fr 1fr;gap:18px}"
            <<"figure{margin:0;min-width:0}figcaption{font-weight:650;margin-bottom:9px}.view{overflow:auto;max-height:1000px;background:#eef2f5}"
            <<"img{display:block;width:100%;height:auto}.links a{margin-right:16px}button{padding:6px 12px;margin:0 8px 8px 0}"
            <<"code{word-break:break-all}a{color:#1e6089}@media(max-width:850px){.pair{grid-template-columns:1fr}main{padding:12px}}</style><main>"
            <<"<h1>基础压紧机制的实际输出</h1><p>"<<Artifacts.size()<<" 个通用测试夹具。每组展示同一批原零件压紧前后的实际位姿，两侧采用同一物理比例。"
            <<"画面使用标明尺寸的左下局部视窗，可打开完整板图。</p><p class=\"sub\">本轮原生测试 "<<Passed<<" 项通过，"<<Failed
            <<" 项失败。这里检视固定角平移如何减少悬空与无谓间隔，板数及零件数量逐组核对。"
            <<"闭合范围为整板、标签组、单件的固定角 X/Y 事件候选循环，阈值见各组。</p>";
        for(const auto& artifact:Artifacts)
        {
            page<<"<section><h2>"<<Escape(artifact.Title)<<"</h2><p>零件 "<<artifact.Parts<<" → "<<artifact.Parts
                <<" 件，板材 "<<artifact.Sheets<<" → "<<artifact.Sheets<<" 张。件间距 "<<artifact.Gap<<"，留边 "<<artifact.Margin
                <<"。接受 "<<artifact.Moves<<" 次移动，完成 "<<artifact.Passes<<" 轮。</p><p class=\"sub\">限定事件邻域闭合 "
                <<(artifact.Closed?"是":"否")<<"，移动阈值 "<<artifact.ClosureTolerance<<"。图形保留原轮廓、孔与圆，颜色表示原规格。</p>"
                <<"<div><button onclick=\"zoom(this,1)\">适应视窗</button><button onclick=\"zoom(this,2)\">放大两倍</button>"
                <<"<button onclick=\"zoom(this,3)\">放大三倍</button></div><div class=\"pair\">";
            for(const auto* side:{"before","after"})
            {
                const auto name=artifact.Stem+"-"+side;
                page<<"<figure><figcaption>"<<(std::string(side)=="before"?"压紧前":"压紧后")<<" · 同一局部视窗</figcaption><div class=\"view\">"
                    <<"<a href=\""<<name<<".svg\" target=\"_blank\"><img src=\""<<name<<".svg\" alt=\""<<Escape(artifact.Title)<<"\"></a></div>"
                    <<"<p><a href=\""<<name<<"-full.svg\" target=\"_blank\">打开完整板图</a></p></figure>";
            }
            page<<"</div><p class=\"links\"><a href=\""<<artifact.Stem<<"-verification.json\">数量、位姿绑定与闭合记录</a></p></section>";
        }
        page<<"<script>function zoom(button,scale){for(const img of button.closest('section').querySelectorAll('img'))img.style.width=(scale*100)+'%';}</script></main></html>";
        WriteArtifact("index.html",page.str());
    }
}

int main(int argc,char** argv)
{
    if(argc!=1)
    {
        if(argc!=3||std::string(argv[1])!="--artifacts")
        {std::cerr<<"Usage: AxisCompactionTests [--artifacts directory]\n";return 2;}
        ArtifactDirectory=std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(ArtifactDirectory);
    }
    Test("independent strict geometry controls reject sub-tolerance intrusion and nominal-gap loss",[]
    {
        auto problem=Sheet();problem.Parts={Box("A",8.375,9.25),Box("B",7.125,9.25)};
        const auto overlap=At(problem,{{"A",0,{1.125,2.375,0},0,false},{"B",0,{9.5-1e-8,2.375,0},0,false}});
        bool caught=false;
        try{StrictRectangleGeometry(problem,overlap);}catch(const std::runtime_error&){caught=true;}
        Require(caught,"independent rectangle oracle missed actual material intrusion below the native tolerance");
        problem.PartGap=.5;auto deficit=overlap;deficit.Layouts[0].Placements[1].Transform.X=10.-4e-15;
        caught=false;
        try{StrictRectangleGeometry(problem,deficit);}catch(const std::runtime_error&){caught=true;}
        Require(caught,"independent rectangle oracle subtracted the native tolerance from the nominal gap");
        problem.PartGap=0;auto a=Box("A",1,1),b=Box("B",1,1);
        a.Geometry.Outer={{0,0},{18,0},{18,12}};b.Geometry.Outer={{0,0},{18,0},{0,12}};problem.Parts={a,b};
        const auto triangles=At(problem,{{"A",0,{0,0,0},0,false},{"B",0,{18-1e-6,0,0},0,false}});
        caught=false;
        try{IndependentConvexGeometry(problem,triangles);}catch(const std::runtime_error&){caught=true;}
        Require(caught,"independent convex SAT oracle missed visible original material intrusion");
    });
    Test("zero-gap mixed rectangles retain strictly disjoint written original material",[]
    {
        auto problem=Sheet(600,400);problem.EdgeMargin=.375;
        problem.Parts={Box("A",13.25,9.5,3,{.375,-.625}),Box("B",7.75,15.375,2,{-1.125,.875})};
        for(auto& part:problem.Parts)part.AllowedAnglesDegrees={0,90,180,270};
        auto before=At(problem,{});std::size_t cell=0;
        for(const auto& part:problem.Parts)for(std::size_t instance=0;instance<part.Quantity;++instance)
        {
            Pose pose;pose.AngleDegrees=((cell+1)%4)*90.;const auto bounds=IndependentRectangleBounds(part,pose);
            pose.X=30.125+(cell%3)*100.375-bounds.Left;
            pose.Y=35.625+(cell/3)*80.25-bounds.Bottom;
            before.Layouts[0].Placements.push_back({part.ID,instance,pose,0,false});++cell;
        }
        Valid(problem,before);StrictRectangleGeometry(problem,before);
        const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
        StrictRectangleGeometry(problem,out.Output);
        Require(Movement(before,out.Output)>10,"strict material protection disabled all useful rectangle compaction");
    });
    Test("positive-gap mixed rectangles retain the nominal gap without a tolerance discount",[]
    {
        auto problem=Sheet(600,400);problem.PartGap=.65;problem.EdgeMargin=.375;
        problem.Parts={Box("A",13.25,9.5,3,{.375,-.625}),Box("B",7.75,15.375,2,{-1.125,.875})};
        for(auto& part:problem.Parts)part.AllowedAnglesDegrees={0,90,180,270};
        auto before=At(problem,{});std::size_t cell=0;
        for(const auto& part:problem.Parts)for(std::size_t instance=0;instance<part.Quantity;++instance)
        {
            Pose pose;pose.AngleDegrees=(cell%4)*90.;const auto bounds=IndependentRectangleBounds(part,pose);
            pose.X=30.125+(cell%3)*100.375-bounds.Left;
            pose.Y=35.625+(cell/3)*80.25-bounds.Bottom;
            before.Layouts[0].Placements.push_back({part.ID,instance,pose,0,false});++cell;
        }
        Valid(problem,before);StrictRectangleGeometry(problem,before);
        const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
        StrictRectangleGeometry(problem,out.Output);
        Require(Movement(before,out.Output)>10,"strict nominal gap protection disabled all useful compaction");
    });
    Test("original triangles and trapezoids pass independent material and gap guards at retained rotations",[]
    {
        for(const bool trapezoids:{false,true})for(const double angle:{0.,90.,180.,270.,23.7})for(const double gap:{0.,.65})
        {
            auto problem=Sheet(300,200);problem.EdgeMargin=.375;problem.PartGap=gap;
            auto a=Box("A",1,1),b=Box("B",1,1);
            if(trapezoids)
            {
                a.Geometry.Outer={{.375,-.625},{20.625,-.625},{20.625,13.875},{.375,9.625}};
                b.Geometry.Outer={{-1.125,.875},{15.625,.875},{15.625,10.375},{-1.125,13.625}};
            }
            else
            {
                a.Geometry.Outer={{.375,-.625},{18.625,-.625},{18.625,11.875}};
                b.Geometry.Outer={{-1.125,.875},{15.625,.875},{-1.125,13.625}};
            }
            for(auto* part:{&a,&b})
            {
                part->FreeRotation=angle==23.7;
                part->AllowedAnglesDegrees=part->FreeRotation?std::vector<double>{}:std::vector<double>{0,90,180,270};
            }
            problem.Parts={a,b};auto before=At(problem,{});
            for(std::size_t i=0;i<problem.Parts.size();++i)
            {
                Pose pose;pose.AngleDegrees=angle;const auto ring=IndependentConvexWorld(problem.Parts[i],pose);
                long double minX=ring.front().X,minY=ring.front().Y;
                for(const auto point:ring){minX=std::min(minX,point.X);minY=std::min(minY,point.Y);}
                pose.X=static_cast<double>(30.125+i*70.375-minX);pose.Y=static_cast<double>(35.625-minY);
                before.Layouts[0].Placements.push_back({problem.Parts[i].ID,0,pose,0,false});
            }
            Valid(problem,before);IndependentConvexGeometry(problem,before);
            const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
            IndependentConvexGeometry(problem,out.Output);
            Require(Movement(before,out.Output)>10,"strict convex material protection disabled all useful compaction");
        }
    });
    Test("isolated rectangles reach the original usable board corner",[]
    {
        auto problem=Sheet();problem.EdgeMargin=2.75;
        problem.Parts={Box("off-origin",8,13,1,{-3,5})};
        const auto before=At(problem,{{"off-origin",0,{51,67,0},7,true}});const auto saved=GeometrySignature(before);
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        const auto bounds=Detail::Bounds(problem.Parts[0].Geometry,out.Output.Layouts[0].Placements[0].Transform);
        Require(std::abs(bounds.X-problem.EdgeMargin)<2e-6&&std::abs(bounds.Y-problem.EdgeMargin)<2e-6,
            "isolated polygon was not compacted to the real margin");
        Require(GeometrySignature(before)==saved,"const incumbent was mutated");Repeatable(problem,before,out);
    });
    Test("fixed quarter turns and nonzero source origins are preserved",[]
    {
        auto problem=Sheet();problem.EdgeMargin=1.25;auto box=Box("turned",8,13,1,{-3,5});
        box.AllowedAnglesDegrees={0,90,180,270};problem.Parts={box};
        const auto before=At(problem,{{"turned",0,{72,67,90},0,false}});
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        const auto bounds=Detail::Bounds(box.Geometry,out.Output.Layouts[0].Placements[0].Transform);
        Require(std::abs(bounds.X-problem.EdgeMargin)<2e-6&&std::abs(bounds.Y-problem.EdgeMargin)<2e-6,
            "90-degree nonzero-origin boundary was computed incorrectly");
    });
    Test("interlocking concave originals can settle as a connected component",[]
    {
        auto problem=Sheet();auto channel=Box("channel",14,5);
        channel.Geometry.Outer={{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}};
        problem.Parts={channel,Box("insert",10,3)};
        const auto before=At(problem,{{"channel",0,{35,40,0},4,false},{"insert",0,{37,40,0},4,true}});
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        Require(Movement(before,out.Output)>50,"interlocked original component remained floating");
        Repeatable(problem,before,out);
        RecordArtifact("concave-interlock","凹形互锁零件的整体与逐件压紧",problem,before,out);
    });
    Test("planning group tags permit independent tightening of their members",[]
    {
        auto problem=Sheet();problem.Parts={Box("same",10,10,2)};
        const auto before=At(problem,{{"same",0,{30,30,0},9,false},{"same",1,{80,30,0},9,true}});
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        const auto poses=Poses(out.Output);const auto a=std::get<2>(poses.at({"same",0})),b=std::get<2>(poses.at({"same",1}));
        Require(std::abs(a.X-b.X)<=10+2e-6,"a planning tag was treated as a rigid carrier and preserved its empty interior");
    });
    Test("actual arbitrary rotations survive compaction without angle discretization",[]
    {
        const auto fixture=FloatingCells(771,5,3,2,true,.65,2.5);
        const auto out=Compact(fixture.Input,fixture.Floating);Closed(out);Conserved(fixture.Input,fixture.Floating,out.Output);
        Require(Movement(fixture.Floating,out.Output)>10,"free-angle originals were silently skipped");
        Repeatable(fixture.Input,fixture.Floating,out);
        RecordArtifact("actual-free-angles","保留实际任意角姿态的混规格压紧",fixture.Input,fixture.Floating,out);
    });
    Test("more than eight physical boards and 128 distinct specifications are processed",[]
    {
        const auto fixture=FloatingCells(189,135,1,10,false,.5,1);
        const auto out=Compact(fixture.Input,fixture.Floating);Closed(out);Conserved(fixture.Input,fixture.Floating,out.Output);
        const auto original=Poses(fixture.Floating),compacted=Poses(out.Output);
        std::vector<bool> moved(10);
        for(const auto& [identity,value]:original)
        {
            const auto old=std::get<2>(value),now=std::get<2>(compacted.at(identity));
            if(old.X-now.X>1||old.Y-now.Y>1)moved[std::get<1>(value)]=true;
        }
        Require(std::all_of(moved.begin(),moved.end(),[](bool value){return value;}),
            "a physical board was skipped by a hidden global size cap");
    });
    Test("repeated quantities above 128 conserve every original instance",[]
    {
        const auto fixture=FloatingCells(638,1,160,10,false,1.2,3);
        const auto out=Compact(fixture.Input,fixture.Floating);Closed(out);Conserved(fixture.Input,fixture.Floating,out.Output);
        Require(Poses(out.Output).size()==160&&Movement(fixture.Floating,out.Output)>160,
            "large repeated quantity was dropped or silently skipped");
    });
    Test("positive gap and sheet margins remain actual geometry constraints",[]
    {
        const auto fixture=FloatingCells(892,7,3,3,false,2.5,6.25);
        const auto out=Compact(fixture.Input,fixture.Floating);Closed(out);Conserved(fixture.Input,fixture.Floating,out.Output);
        Require(Movement(fixture.Floating,out.Output)>20,"positive-gap scope was silently skipped");
        for(const auto& layout:out.Output.Layouts)for(std::size_t i=0;i<layout.Placements.size();++i)
        {
            const auto& a=layout.Placements[i];const auto ai=std::find_if(fixture.Input.Parts.begin(),fixture.Input.Parts.end(),
                [&](const Part& part){return part.ID==a.PartID;});
            const auto bounds=Detail::Bounds(ai->Geometry,a.Transform);
            Require(bounds.X>=6.25-1e-7&&bounds.Y>=6.25-1e-7,"usable sheet margin was reduced");
            for(std::size_t j=0;j<i;++j)
            {
                const auto& b=layout.Placements[j];const auto bi=std::find_if(fixture.Input.Parts.begin(),fixture.Input.Parts.end(),
                    [&](const Part& part){return part.ID==b.PartID;});
                const auto other=Detail::Bounds(bi->Geometry,b.Transform);
                const double dx=std::max({other.X-bounds.X-bounds.Width,bounds.X-other.X-other.Width,0.});
                const double dy=std::max({other.Y-bounds.Y-bounds.Height,bounds.Y-other.Y-other.Height,0.});
                Require(std::hypot(dx,dy)>=2.5-2e-6,"axis rectangles violate their Euclidean original gap");
            }
        }
        RecordArtifact("positive-gap-margin","保留正间距与留边的不同尺寸混排整理",fixture.Input,fixture.Floating,out);
    });
    Test("validated source boundary rounding permits safe orthogonal compaction",[]
    {
        auto problem=Sheet(600,100);problem.EdgeMargin=.2;
        auto part=Box("source-boundary",440,10);part.AllowedAnglesDegrees={180};problem.Parts={part};
        const Pose original{440.2,30,180};
        const auto before=At(problem,{{part.ID,0,original,0,false}});Valid(problem,before);
        const auto strictSource=AxisSafety::Make(part.Geometry,original);
        Require(AxisSafety::Containment(strictSource,600,100,.2)==AxisSafety::Verdict::Blocked,
            "source control did not reproduce the actual binary boundary shortage");
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        const auto pose=out.Output.Layouts.front().Placements.front().Transform;
        Require(pose.X==original.X&&pose.Y<original.Y-19,"an unchanged rounded X boundary disabled safe Y compaction");
        bool inherited=false;
        const auto strictCandidate=AxisSafety::Make(part.Geometry,pose);
        Require(AxisSafety::ContainmentWithSource(strictSource,strictCandidate,600,100,.2,false,inherited)==AxisSafety::Verdict::Safe&&inherited,
            "candidate did not identify its exactly unchanged orthogonal source constraint");
        Require(AxisSafety::InsideAxisChecked(strictCandidate,100,.2,false),"the changed axis spent validation tolerance");
        auto intrusion=original;intrusion.X=std::nextafter(intrusion.X,-std::numeric_limits<double>::infinity());
        inherited=false;
        Require(AxisSafety::ContainmentWithSource(strictSource,AxisSafety::Make(part.Geometry,intrusion),600,100,.2,true,inherited)==
            AxisSafety::Verdict::Blocked&&!inherited,"a changed source boundary inherited its old tolerance allowance");
        Require(std::any_of(out.Output.Diagnostics.begin(),out.Output.Diagnostics.end(),[](const std::string& note)
            {return note.find(" inherited-boundary-checks=")!=std::string::npos&&note.find(" inherited-boundary-checks=0 ")==std::string::npos;}),
            "source boundary inheritance count was not recorded");
    });
    Test("source contact inheritance requires the same actual material translation",[]
    {
        auto problem=Sheet(40,40);problem.PartGap=.2;problem.EdgeMargin=.2;problem.AllowCommonEdges=true;
        problem.Parts={Box("anchor",1,1),Box("first",4,4),Box("second",4,4)};
        const Pose first{10.2,10,0};
        const Pose second{std::nextafter(14.2,std::numeric_limits<double>::infinity()),10,0};
        const auto before=At(problem,{{"anchor",0,{.2,.2,0},0,false},{"first",0,first,0,true},{"second",0,second,0,false}});
        Valid(problem,before);
        const auto a=AxisSafety::Make(problem.Parts[1].Geometry,first),b=AxisSafety::Make(problem.Parts[2].Geometry,second);
        Require(AxisSafety::Pair(a,b,.2,true,problem.Tolerance)==AxisSafety::Verdict::Blocked,
            "source contact control did not differ from an exact binary common edge");
        auto movedFirst=first,movedSecond=second;movedFirst.X-=1;movedSecond.X-=1;
        bool inherited=false;
        Require(AxisSafety::PairWithSource(a,AxisSafety::Make(problem.Parts[1].Geometry,movedFirst),b,
            AxisSafety::Make(problem.Parts[2].Geometry,movedSecond),.2,true,problem.Tolerance,inherited)==AxisSafety::Verdict::Safe&&inherited,
            "an exactly unchanged validated material relationship was not inherited");
        movedSecond.X=std::nextafter(movedFirst.X+4,-std::numeric_limits<double>::infinity());
        inherited=false;
        Require(AxisSafety::PairWithSource(a,AxisSafety::Make(problem.Parts[1].Geometry,movedFirst),b,
            AxisSafety::Make(problem.Parts[2].Geometry,movedSecond),.2,true,problem.Tolerance,inherited)==AxisSafety::Verdict::Blocked&&!inherited,
            "different actual displacements were accepted as a rigid group");
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        Require(out.Report.CommonEdgeGroupMoves>0&&Movement(before,out.Output)>15,
            "validated rounded common-edge source component remained immobile");
        Require(std::any_of(out.Output.Diagnostics.begin(),out.Output.Diagnostics.end(),[](const std::string& note)
            {return note.find(" inherited-pair-checks=")!=std::string::npos&&note.find(" inherited-pair-checks=0 ")==std::string::npos;}),
            "source pair inheritance count was not recorded");
    });
    Test("oblique ordinary contact remains above the declared gap minus tolerance",[]
    {
        auto problem=Sheet(1800,1800);problem.PartGap=.2;problem.EdgeMargin=.2;
        auto sloped=Box("sloped",688,560);
        sloped.Geometry.Outer={{380,0},{688,0},{688,560},{0,560}};
        problem.Parts={Box("upright",440,1000),sloped,
            Box("foundation",440,552.6716333580627),Box("high-floor",500,1016.8)};
        const auto before=At(problem,{{"upright",0,{.2,553.0716333580627,0},0,false},
            {"sloped",0,{600,1017.2,0},0,false},{"foundation",0,{.2,.2,0},0,false},
            {"high-floor",0,{900,.2,0},0,false}});
        AxisCompactionSettings settings;settings.XFirst=true;
        const auto out=CompactWithSettings(problem,before,settings);Closed(out);Conserved(problem,before,out.Output);
        const auto poses=Poses(out.Output);
        const auto upright=std::get<2>(poses.at({"upright",0}));
        const auto shape=std::get<2>(poses.at({"sloped",0}));
        const long double x=static_cast<long double>(upright.X)+440;
        const long double y=static_cast<long double>(upright.Y)+1000;
        const long double numerator=560*(x-shape.X)+380*(y-shape.Y-560);
        const long double distance=std::abs(numerator)/std::hypot(560.L,380.L);
        Require(shape.X<500,"ordinary oblique contact was skipped");
        Require(distance>=static_cast<long double>(problem.PartGap)-problem.Tolerance,
            "stored contact pose fell below the unchanged original gap-minus-tolerance threshold");
    });
    Test("zero-tag real common-edge components move together without changing provenance",[]
    {
        auto problem=Sheet(40,40);problem.PartGap=.2;problem.EdgeMargin=.2;problem.AllowCommonEdges=true;
        problem.Parts={Box("anchor",1,1),Box("left",4,4),Box("right",4,4)};
        const auto before=At(problem,{{"anchor",0,{.2,.2,0},0,false},
            {"left",0,{10,10,0},0,true},{"right",0,{14,10,0},0,false}});
        Require(Detail::ShapesHaveCommonEdge(problem.Parts[1].Geometry,{10,10,0},
            problem.Parts[2].Geometry,{14,10,0},problem.Tolerance),"real component contact was not detected");
        Require(Detail::ShapesConflict(problem.Parts[1].Geometry,{10-.1,10,0},
            problem.Parts[2].Geometry,{14,10,0},problem.PartGap,problem.Tolerance,true),
            "single-member escape control did not require the original ordinary gap");
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        Require(out.Report.CommonEdgeGroupMoves>0,"zero-tag common-edge component was never proposed as a group");
        const auto poses=Poses(out.Output);
        Require(std::get<2>(poses.at({"left",0})).Y<1&&std::get<2>(poses.at({"right",0})).Y<1,
            "an anchored unrelated original prevented the movable component from reaching the lower margin");
        Require(!Detail::ShapesHaveCommonEdge(problem.Parts[1].Geometry,{10,10,0},
            problem.Parts[2].Geometry,{14.00001,10,0},.01),"nearby boundaries were merged using the full geometry tolerance");
        Require(!Detail::ShapesHaveCommonEdge(problem.Parts[1].Geometry,{10,10,0},
            problem.Parts[2].Geometry,{14,14,0},problem.Tolerance),"isolated corner contact was merged into a component");
    });
    Test("long original contours are not rejected by the old 128-vertex cap",[]
    {
        auto problem=Sheet();auto part=Box("long-contour",20,10);part.Geometry.Outer.clear();
        for(int i=0;i<40;++i)part.Geometry.Outer.push_back({i*.5,0});
        for(int i=0;i<40;++i)part.Geometry.Outer.push_back({20,i*.25});
        for(int i=0;i<40;++i)part.Geometry.Outer.push_back({20-i*.5,10});
        for(int i=0;i<40;++i)part.Geometry.Outer.push_back({0,10-i*.25});
        problem.Parts={part};const auto before=At(problem,{{part.ID,0,{40,50,0},0,false}});
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        Require(Movement(before,out.Output)>80,"long contour was skipped or not compacted");
    });
    Test("holed material keeps an enclosed original in its original free component",[]
    {
        auto problem=Sheet();auto frame=Box("frame",30,30);
        frame.Geometry.Holes={{{10,10},{20,10},{20,20},{10,20}}};
        problem.Parts={frame,Box("inside",2,2)};
        const auto before=At(problem,{{"frame",0,{40,50,0},0,false},{"inside",0,{54,64,0},0,true}});
        const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
        if(!SupportedOrExplicit(before,out,"polygon holes"))return;
        Closed(out);const auto poses=Poses(out.Output);
        const auto shell=std::get<2>(poses.at({"frame",0})),inside=std::get<2>(poses.at({"inside",0}));
        Require(inside.X>=shell.X+10-1e-6&&inside.X+2<=shell.X+20+1e-6&&
            inside.Y>=shell.Y+10-1e-6&&inside.Y+2<=shell.Y+20+1e-6,
            "a safe final pose teleported across the frame material into another component");
        Require(Movement(before,out.Output)>10,"supported holed originals remained floating");
        Repeatable(problem,before,out);
    });
    Test("circles use their exact center radius or are explicitly unsupported",[]
    {
        auto problem=Sheet();problem.EdgeMargin=1.5;auto circle=Box("circle",1,1);
        circle.Geometry.Outer.clear();circle.Geometry.Radius=6;circle.Geometry.Center={3,-2};problem.Parts={circle};
        const auto before=At(problem,{{"circle",0,{40,60,0},0,false}});const auto out=Compact(problem,before);
        Conserved(problem,before,out.Output);if(!SupportedOrExplicit(before,out,"circles"))return;Closed(out);
        const auto pose=out.Output.Layouts[0].Placements[0].Transform;
        Require(std::abs(pose.X+3-6-1.5)<2e-6&&std::abs(pose.Y-2-6-1.5)<2e-6,
            "circle was compacted using a polygon proxy or the wrong source center");
    });
    Test("positive-gap circles stop at analytic circle distance",[]
    {
        auto problem=Sheet();problem.PartGap=2;problem.EdgeMargin=2;
        auto circle=Box("circle",1,1,2);circle.Geometry.Outer.clear();circle.Geometry.Radius=3;problem.Parts={circle};
        const auto before=At(problem,{{"circle",0,{30,20,0},0,false},{"circle",1,{50,20,0},0,false}});
        const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
        if(!SupportedOrExplicit(before,out,"circle-circle positive gap"))return;Closed(out);
        const auto poses=Poses(out.Output);const auto a=std::get<2>(poses.at({"circle",0})),b=std::get<2>(poses.at({"circle",1}));
        Require(std::abs(std::hypot(a.X-b.X,a.Y-b.Y)-8)<3e-6,
            "circle-circle contact did not preserve both radii plus the original positive gap");
    });
    Test("circle polygon gap includes capsule endpoint contact",[]
    {
        auto problem=Sheet();problem.PartGap=2;problem.EdgeMargin=2;
        auto triangle=Box("triangle",20,20);triangle.Geometry.Outer={{0,0},{20,0},{0,20}};
        auto circle=Box("circle",1,1);circle.Geometry.Outer.clear();circle.Geometry.Radius=3;
        problem.Parts={triangle,circle};
        const auto before=At(problem,{{"triangle",0,{2,2,0},0,false},{"circle",0,{50,40,0},0,false}});
        const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
        if(!SupportedOrExplicit(before,out,"circle polygon endpoint distance"))return;Closed(out);
        const auto poses=Poses(out.Output);const auto t=std::get<2>(poses.at({"triangle",0})),c=std::get<2>(poses.at({"circle",0}));
        Require(std::abs(c.Y-5)<3e-6&&std::abs(std::hypot(c.X-t.X-20,c.Y-t.Y)-5)<3e-6,
            "circle corner contact ignored a polygon endpoint or used an infinite supporting line");
    });
    Test("circle polygon gap includes oblique segment contact",[]
    {
        auto problem=Sheet();problem.PartGap=2;
        auto triangle=Box("triangle",20,20);triangle.Geometry.Outer={{0,0},{20,0},{0,20}};
        auto circle=Box("circle",1,1);circle.Geometry.Outer.clear();circle.Geometry.Radius=3;
        problem.Parts={triangle,Box("floor",50,10),circle};
        const auto before=At(problem,{{"triangle",0,{0,0,0},0,false},{"floor",0,{30,0,0},0,false},{"circle",0,{60,40,0},0,false}});
        const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
        if(!SupportedOrExplicit(before,out,"circle oblique-edge distance"))return;Closed(out);
        const auto poses=Poses(out.Output);const auto t=std::get<2>(poses.at({"triangle",0})),c=std::get<2>(poses.at({"circle",0}));
        Require(std::abs(c.Y-15)<3e-6&&std::abs((c.X-t.X+c.Y-t.Y-20)/std::sqrt(2.)-5)<3e-6,
            "circle slanted-edge contact did not preserve radius plus original gap");
    });
    Test("current common-edge permission never becomes a blanket zero-gap waiver",[]
    {
        auto problem=Sheet();problem.PartGap=2;problem.AllowCommonEdges=true;
        problem.Parts={Box("A",10,10),Box("B",10,10)};
        const auto before=At(problem,{{"A",0,{30,40,0},0,false},{"B",0,{40,40,0},0,false}});
        Valid(problem,before);auto ordinary=problem;ordinary.AllowCommonEdges=false;
        Require(!Validate(ordinary,before).Valid,"positive-gap shared-edge control did not reject");
        const auto out=Compact(problem,before);Conserved(problem,before,out.Output);
        if(!SupportedOrExplicit(before,out,"positive-gap common-edge continuation"))return;
        Closed(out);Require(Movement(before,out.Output)>20,"supported common-edge component remained floating");
        // Independent controls ensure only a positive-length straight seam can
        // waive the gap. Point contact and a plain sub-gap cannot do so.
        auto point=out.Output;point.Layouts[0].Placements[0].Transform={20,20,0};
        point.Layouts[0].Placements[1].Transform={30,30,0};
        Require(!Validate(problem,point).Valid,"point contact incorrectly became a common edge");
        point.Layouts[0].Placements[1].Transform={31,20,0};
        Require(!Validate(problem,point).Valid,"ordinary sub-gap was waived by common-edge permission");
    });
    Test("partial valid layouts keep the same unplaced original inventory",[]
    {
        auto problem=Sheet();problem.Parts={Box("present",7,9,3),Box("missing",15,12,2)};
        auto before=At(problem,{{"present",0,{30,40,0},0,false},{"present",1,{60,70,0},0,false}});
        before.State=Status::Partial;before.Remaining={{"present",1},{"missing",2}};
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        Require(out.Output.State==Status::Partial&&Movement(before,out.Output)>20,
            "valid partial was promoted to complete or omitted from compaction");
    });
    Test("a complete axis pair repeats after the other axis releases a blocker",[]
    {
        auto problem=Sheet();problem.Parts={Box("C",2,10),Box("B",4,4),Box("A",4,4)};
        const auto before=At(problem,{{"C",0,{0,0,0},0,false},{"B",0,{10,20,0},0,false},{"A",0,{20,20,0},0,false}});
        AxisCompactionSettings settings;settings.XFirst=true;
        const auto out=CompactWithSettings(problem,before,settings);Closed(out);Conserved(problem,before,out.Output);
        const auto pose=std::get<2>(Poses(out.Output).at({"A",0}));
        Require(std::abs(pose.X-2)<2e-6&&std::abs(pose.Y)<2e-6,
            "compactor stopped after the first X/Y pair although Y released a new left movement");
        Require(out.Report.CompletedPasses>=2,"closure did not include a subsequent complete axis cycle");
        settings.MaxPasses=1;const auto bounded=CompactWithSettings(problem,before,settings);
        Conserved(problem,before,bounded.Output);Require(!bounded.Closed,"pass-limited changed cycle was labelled closed");
        Require(bounded.Report.StopReason==AxisCompactionStopReason::PassLimit,"one-pass stop did not report its actual limit");
        RecordArtifact("blocker-release","另一方向释放阻挡后的多轮压紧",problem,before,out);
    });
    Test("large source origins validate the actual written translation endpoint",[]
    {
        auto problem=Sheet();problem.EdgeMargin=.37;problem.Parts={Box("remote-origin",7,11,1,{9e6,-9e6})};
        const auto before=At(problem,{{"remote-origin",0,{-9e6+40,9e6+50,0},0,false}});
        Valid(problem,before);
        const auto out=Compact(problem,before);Closed(out);Conserved(problem,before,out.Output);
        const auto bounds=Detail::Bounds(problem.Parts[0].Geometry,out.Output.Layouts[0].Placements[0].Transform);
        Require(std::abs(bounds.X-.37)<2e-6&&std::abs(bounds.Y-.37)<2e-6,
            "theoretical event was not recovered as a valid written binary64 coordinate");
    });
    Test("source origins beyond the original geometry limit remain invalid",[]
    {
        auto problem=Sheet();problem.Parts={Box("outside-source-domain",7,11,1,{1e8,-1e8})};
        const auto before=At(problem,{{"outside-source-domain",0,{-1e8+40,1e8+50,0},0,false}});
        Require(!Validate(problem,before).Valid,"original coordinate-domain rejection control must be invalid");
        const auto out=Compact(problem,before);
        Require(!out.Closed&&out.Report.StopReason==AxisCompactionStopReason::InvalidInput,
            "the compactor must preserve the original input coordinate-domain contract");
        NoMutation(before,out.Output);
    });
    Test("closure evidence is bound to the actual retained physical placements",[]
    {
        const auto fixture=FloatingCells(187,4,2,2,false,0,1);const auto out=Compact(fixture.Input,fixture.Floating);Closed(out);
        auto reorder=out.Output;std::reverse(reorder.Layouts.begin(),reorder.Layouts.end());
        for(auto& board:reorder.Layouts)std::reverse(board.Placements.begin(),board.Placements.end());
        Require(GetAxisCompactionReport(fixture.Input,reorder).has_value(),"logical reordering broke an unchanged physical proof binding");
        auto changed=out.Output;changed.Layouts[0].Placements[0].Transform.X+=.5;
        Require(!GetAxisCompactionReport(fixture.Input,changed).has_value(),"a stale candidate closure report was accepted on another retained pose");
        changed=out.Output;changed.Layouts[0].Placements[0].Transform.AngleDegrees+=.1;
        Require(!GetAxisCompactionReport(fixture.Input,changed).has_value(),"closure binding omitted authoritative original rotations");
        changed=out.Output;changed.Layouts[0].Placements[0].Transform.Mirrored=true;
        Require(!GetAxisCompactionReport(changed).has_value()&&!GetAxisCompactionReport(fixture.Input,changed).has_value(),
            "closure binding reused a certificate after original reflection changed");
    });
    Test("closure reports require the current changed-material and source-invariance safety version",[]
    {
        auto problem=Sheet();problem.Parts={Box("A",10,8)};
        const auto before=At(problem,{{"A",0,{30,40,0},0,false}});
        const auto out=Compact(problem,before);Closed(out);
        const std::string marker=" guard=nominal-changes-source-invariants-v2";
        const auto report=std::find_if(out.Output.Diagnostics.begin(),out.Output.Diagnostics.end(),
            [&](const std::string& entry){return entry.find(marker)!=std::string::npos;});
        Require(report!=out.Output.Diagnostics.end(),"new closure report omitted its material safety version");
        const auto index=static_cast<std::size_t>(report-out.Output.Diagnostics.begin());
        const auto geometry=GeometrySignature(out.Output);
        for(const std::string replacement:{""," guard=native-tolerance-v0"," guard=nominal-material-v1"," guard=nominal-changes-source-invariants-v20"})
        {
            auto changed=out.Output;auto& entry=changed.Diagnostics[index];
            entry.replace(entry.find(marker),marker.size(),replacement);
            Require(GeometrySignature(changed)==geometry,"safety-version control changed the physical test layout");
            Require(!GetAxisCompactionReport(changed).has_value()&&!GetAxisCompactionReport(problem,changed).has_value(),
                "missing or different material safety version reused a closure report");
            // A rejected newest certificate must not silently expose an older
            // certificate in the same result's diagnostic history.
            auto history=out.Output;history.Diagnostics.push_back(entry);
            Require(!GetAxisCompactionReport(problem,history).has_value(),
                "invalid newest safety version fell back to historical closure evidence");
        }
    });
    Test("unchanged placements cannot reuse a closure report across different original problems",[]
    {
        const auto fixture=FloatingCells(287,3,2,2,false,1,2);const auto out=Compact(fixture.Input,fixture.Floating);Closed(out);
        const auto rejected=[&](const Problem& changed,const char* field)
        {
            Valid(changed,out.Output);
            Require(!GetAxisCompactionReport(changed,out.Output).has_value(),std::string("closure was reused after changing original ")+field);
        };
        auto changed=fixture.Input;changed.PartGap=.5;rejected(changed,"gap");
        changed=fixture.Input;changed.EdgeMargin=1;rejected(changed,"margin");
        changed=fixture.Input;changed.Parts[0].Geometry.Outer[1].X-=.1;changed.Parts[0].Geometry.Outer[2].X-=.1;
        rejected(changed,"source contour");
        changed=fixture.Input;changed.Parts[0].FreeRotation=true;rejected(changed,"free rotation policy");
        changed=fixture.Input;changed.Parts[0].AllowMirror=true;rejected(changed,"mirror permission");
        changed=fixture.Input;changed.Parts[0].AllowedAnglesDegrees={0,90};rejected(changed,"allowed angle set");
        changed=fixture.Input;changed.Tolerance=1e-6;rejected(changed,"tolerance");
        changed=fixture.Input;changed.AllowCommonEdges=true;rejected(changed,"common-edge permission");
        changed=fixture.Input;changed.Stocks[0].Quantity+=1;rejected(changed,"physical stock inventory");
    });
    Test("random mixed dimensions and quantities validate and close deterministically",[]
    {
        for(std::uint64_t seed=1;seed<=12;++seed)
        {
            const auto fixture=FloatingCells(1009*seed,3+seed%5,2+seed%4,2+seed%3,seed%2==1,.25*(seed%5),1.+seed%4);
            const auto out=Compact(fixture.Input,fixture.Floating);Closed(out);Conserved(fixture.Input,fixture.Floating,out.Output);
            Require(Movement(fixture.Floating,out.Output)>10,"generated legal floating originals did not settle");
            Repeatable(fixture.Input,fixture.Floating,out);
        }
    });
    Test("pre-cancellation retains all original geometry and never claims closure",[]
    {
        const auto fixture=FloatingCells(477,4,3,2,false,1,2);auto options=Budget();options.Cancel=[]{return true;};
        const auto out=Compact(fixture.Input,fixture.Floating,options);
        Require(!out.Closed,"cancelled operation was labelled closed");NoMutation(fixture.Floating,out.Output);
        Conserved(fixture.Input,fixture.Floating,out.Output);
    });
    Test("zero time and exhausted attempt budgets never claim closure",[]
    {
        const auto fixture=FloatingCells(776,5,3,2,false,.5,2);
        for(const bool time:{false,true})
        {
            auto options=Budget();if(time)options.TimeLimitSeconds=0;else options.MaxPlacementAttempts=0;
            const auto out=Compact(fixture.Input,fixture.Floating,options);
            Require(!out.Closed,"unexecuted budget was labelled closed");NoMutation(fixture.Floating,out.Output);
            Conserved(fixture.Input,fixture.Floating,out.Output);
        }
    });
    Test("detailed convex contours retain useful contact events without projection-only event overflow",[]
    {
        for (const double gap:{0.,.25})
        {
            auto problem=Sheet(200,150);problem.PartGap=gap;
            auto ellipse=Box("detailed",1,1,2);ellipse.Geometry.Outer.clear();
            for (int vertex=0;vertex<180;++vertex)
            {
                const double angle=2*Detail::Pi*vertex/180;
                ellipse.Geometry.Outer.push_back({10+10*std::cos(angle),10+10*std::sin(angle)});
            }
            problem.Parts={ellipse};
            const auto before=At(problem,{{"detailed",0,{0,0,0},0,false},
                {"detailed",1,{15,90,0},0,false}});
            Valid(problem,before);IndependentConvexGeometry(problem,before);
            const auto out=Compact(problem,before);
            Conserved(problem,before,out.Output);IndependentConvexGeometry(problem,out.Output);
            Require(out.Report.StopReason!=AxisCompactionStopReason::EventLimit&&out.Report.CandidateTests>0,
                "noncontact projections exhausted the contact-event budget before any legal probe");
            Require(Movement(before,out.Output)>50,"detailed contours were retained at a visibly floating position");
        }
    });
    Test("candidate event and mid-run cancellation limits never become closure",[]
    {
        const auto fixture=FloatingCells(349,5,3,2,true,1.1,2);
        AxisCompactionSettings candidate;candidate.MaxCandidatesPerMove=1;
        const auto capped=CompactWithSettings(fixture.Input,fixture.Floating,candidate);
        Conserved(fixture.Input,fixture.Floating,capped.Output);
        Require(!capped.Closed&&capped.Report.StopReason==AxisCompactionStopReason::CandidateLimit,
            "candidate truncation did not report its actual unclosed neighborhood");
        AxisCompactionSettings events;events.MaxContactEventsPerMove=1;
        const auto truncated=CompactWithSettings(fixture.Input,fixture.Floating,events);
        Conserved(fixture.Input,fixture.Floating,truncated.Output);
        Require(!truncated.Closed&&truncated.Report.StopReason==AxisCompactionStopReason::EventLimit,
            "event truncation did not report its actual unclosed neighborhood");
        auto attempts=Budget();attempts.MaxPlacementAttempts=1;
        const auto exhausted=Compact(fixture.Input,fixture.Floating,attempts);Conserved(fixture.Input,fixture.Floating,exhausted.Output);
        Require(!exhausted.Closed&&exhausted.Report.StopReason==AxisCompactionStopReason::AttemptLimit&&
            exhausted.Output.Stats.PlacementAttempts<=attempts.MaxPlacementAttempts,
            "a genuinely exhausted attempt limit was exceeded or labelled closed");
        auto options=Budget();std::size_t polls=0;options.Cancel=[&]{return ++polls>20;};
        const auto cancelled=Compact(fixture.Input,fixture.Floating,options);
        Conserved(fixture.Input,fixture.Floating,cancelled.Output);
        Require(!cancelled.Closed&&cancelled.Report.StopReason==AxisCompactionStopReason::Cancelled,
            "mid-operation cancellation was labelled complete closure");
    });
    Test("invalid original input is preserved and cannot produce a closure certificate",[]
    {
        auto problem=Sheet();problem.Parts={Box("A",10,10),Box("B",10,10)};
        const auto before=At(problem,{{"A",0,{20,20,0},0,false},{"B",0,{21,21,0},0,false}});
        Require(!Validate(problem,before).Valid,"overlap rejection control is not invalid");
        const auto out=Compact(problem,before);Require(!out.Closed,"invalid original layout received a closure certificate");
        NoMutation(before,out.Output);
    });
    try{WriteArtifactIndex();}
    catch(const std::exception& error){++Failed;std::cerr<<"FAIL artifact index: "<<error.what()<<'\n';}
    std::cout<<Passed<<" passed, "<<Failed<<" failed, "<<Unsupported<<" explicitly unsupported optional scopes\n";return Failed?1:0;
}
