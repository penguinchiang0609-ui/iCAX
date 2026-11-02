#include "SheetNesting.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
    constexpr double Pi=3.14159265358979323846;
    struct Settings { std::size_t Count=200,Offset=0,Attempts=20000,Workers=3,Tiles=1,Order=0;double Time=.1;std::string Suite="base";std::filesystem::path Out="fake-bench";std::vector<std::string> Strategies; };
    struct ReferenceLayout { std::string Label;Result Output;std::vector<Rect> FreeRectangles;std::vector<double> AreaUpperBounds; };
    struct Sample { std::size_t ID=0,TilesRequested=1,TilesApplied=1,Order=0;std::uint64_t Seed=0;std::string Family,Suite="base";bool Known=true;double WitnessValidationSeconds=0;Problem Input;Result Witness;std::vector<ReferenceLayout> References; };
    struct Metrics { std::size_t Missing=0,Boards=0;double AABB=0,Top=0,Right=0,TopSlack=0,RightSlack=0,Wall=0; };
    struct TimedJob { JobResult Job;Metrics Metric; };
    Shape Box(double w,double h) { Shape s;s.Outer={{0,0},{w,0},{w,h},{0,h}};return s; }
    Shape Para(double w,double h,double shear) { Shape s;s.Outer={{0,0},{w,0},{w+shear,h},{shear,h}};return s; }
    Shape Disk(double r) { Shape s;s.Radius=r;return s; }
    Point Rotate(Point p,double degrees) { const double a=degrees*Pi/180;return {p.X*std::cos(a)-p.Y*std::sin(a),p.X*std::sin(a)+p.Y*std::cos(a)}; }
    Point World(Point p,Pose pose) { p=Rotate(p,pose.AngleDegrees);return {p.X+pose.X,p.Y+pose.Y}; }
    std::string Escape(const std::string& s)
    {
        std::string r;for(unsigned char c:s) { if(c=='\\')r+="\\\\";else if(c=='\"')r+="\\\"";else if(c=='\n')r+="\\n";else if(c=='\r')r+="\\r";else if(c=='\t')r+="\\t";else if(c<32)r+='?';else r+=static_cast<char>(c); }return r;
    }
    const char* State(Status s) { switch(s) { case Status::Complete:return "Complete";case Status::Partial:return "Partial";case Status::InvalidInput:return "InvalidInput";case Status::Cancelled:return "Cancelled";default:return "Failed"; } }
    void Strings(std::ostream& o,const std::vector<std::string>& strings) { o<<'[';for(std::size_t i=0;i<strings.size();++i) { if(i)o<<',';o<<'\"'<<Escape(strings[i])<<'\"'; }o<<']'; }
    void Points(std::ostream& o,const std::vector<Point>& points) { o<<'[';for(std::size_t i=0;i<points.size();++i) { if(i)o<<',';o<<'['<<points[i].X<<','<<points[i].Y<<']'; }o<<']'; }
    void ProblemJson(std::ostream& o,const Problem& p)
    {
        o<<"{\"partGap\":"<<p.PartGap<<",\"edgeMargin\":"<<p.EdgeMargin<<",\"tolerance\":"<<p.Tolerance<<",\"parts\":[";
        for(std::size_t i=0;i<p.Parts.size();++i)
        {
            if(i)o<<',';const auto& part=p.Parts[i];o<<"{\"id\":\""<<Escape(part.ID)<<"\",\"quantity\":"<<part.Quantity<<",\"material\":\""<<Escape(part.Material)<<"\",\"freeRotation\":"<<(part.FreeRotation?"true":"false")<<",\"allowedAngles\":[";
            for(std::size_t j=0;j<part.AllowedAnglesDegrees.size();++j) { if(j)o<<',';o<<part.AllowedAnglesDegrees[j]; }
            o<<"],\"geometry\":{\"radius\":"<<part.Geometry.Radius<<",\"center\":["<<part.Geometry.Center.X<<','<<part.Geometry.Center.Y<<"],\"outer\":";Points(o,part.Geometry.Outer);o<<",\"holes\":[";
            for(std::size_t j=0;j<part.Geometry.Holes.size();++j) { if(j)o<<',';Points(o,part.Geometry.Holes[j]); }o<<"]}}";
        }
        o<<"],\"stocks\":[";for(std::size_t i=0;i<p.Stocks.size();++i) { if(i)o<<',';const auto& s=p.Stocks[i];o<<"{\"id\":\""<<Escape(s.ID)<<"\",\"width\":"<<s.Width<<",\"height\":"<<s.Height<<",\"quantity\":"<<s.Quantity<<",\"cost\":"<<s.Cost<<",\"material\":\""<<Escape(s.Material)<<"\"}"; }o<<"]}";
    }
    template<class Stats> void PhaseTimings(std::ostream& o,const Stats& s)
    {
        if constexpr(requires { s.PreparationSeconds; s.PlacementSeconds; s.VerificationSeconds; })
        {
            const auto number=[&](double value) { if(std::isfinite(value)&&value>=0)o<<value;else o<<"null"; };
            o<<"{\"preparation\":";number(s.PreparationSeconds);o<<",\"placement\":";number(s.PlacementSeconds);o<<",\"verification\":";number(s.VerificationSeconds);o<<",\"evaluation\":";
            if constexpr(requires { s.EvaluationSeconds; })number(s.EvaluationSeconds);else o<<"null";o<<'}';
        }
        else o<<"null";
    }
    template<class Stats> constexpr bool HasPhaseTimings()
    { return requires(const Stats& s) { s.PreparationSeconds;s.PlacementSeconds;s.VerificationSeconds; }; }
    void RectangleJson(std::ostream& o,const Rect& r)
    { o<<"{\"x\":"<<r.X<<",\"y\":"<<r.Y<<",\"width\":"<<r.Width<<",\"height\":"<<r.Height<<'}'; }
    template<class J> std::optional<double> RemnantArea(const J& job)
    {
        if constexpr(requires { job.Remnants.Measured;job.Remnants.LastSheetRegularArea; })
            if(job.Remnants.Measured&&std::isfinite(job.Remnants.LastSheetRegularArea)&&job.Remnants.LastSheetRegularArea>=0)return job.Remnants.LastSheetRegularArea;
        return {};
    }
    template<class J> std::optional<double> RemnantUpper(const J& job)
    {
        if constexpr(requires { job.Remnants.Measured;job.Remnants.LastSheetAreaUpperBound; })
            if(job.Remnants.Measured&&std::isfinite(job.Remnants.LastSheetAreaUpperBound)&&job.Remnants.LastSheetAreaUpperBound>=0)return job.Remnants.LastSheetAreaUpperBound;
        return {};
    }
    template<class J> void RemnantsJson(std::ostream& o,const Problem& problem,const J& job)
    {
        if constexpr(requires { job.Remnants.PerLayout;job.Remnants.FinalLayoutIndices;job.Remnants.Measured;job.Remnants.LastSheetRegularArea;job.Remnants.LastSheetAreaUpperBound; })
        {
            const auto& evaluation=job.Remnants;
            const auto number=[&](double value) { if(evaluation.Measured&&std::isfinite(value)&&value>=0)o<<value;else o<<"null"; };
            o<<"{\"measured\":"<<(evaluation.Measured?"true":"false")<<",\"perLayout\":[";
            for(std::size_t i=0;i<evaluation.PerLayout.size();++i)
            {
                if(i)o<<',';const auto& r=evaluation.PerLayout[i];o<<"{\"bounds\":";
                if(evaluation.Measured&&r.Verified)RectangleJson(o,r.Bounds);else o<<"null";
                o<<",\"area\":";number(r.Area);o<<",\"areaUpperBound\":";number(r.AreaUpperBound);o<<",\"verified\":"<<(r.Verified?"true":"false")<<",\"resolution\":"<<r.Resolution<<'}';
            }
            o<<"],\"finalLayoutIndices\":[";for(std::size_t i=0;i<evaluation.FinalLayoutIndices.size();++i) { if(i)o<<',';o<<evaluation.FinalLayoutIndices[i]; }
            o<<"],\"lastSheetRegularArea\":";number(evaluation.LastSheetRegularArea);o<<",\"lastSheetAreaUpperBound\":";number(evaluation.LastSheetAreaUpperBound);o<<",\"finalGroups\":[";
            for(std::size_t i=0;i<evaluation.FinalLayoutIndices.size();++i)
            {
                if(i)o<<',';const auto index=evaluation.FinalLayoutIndices[i];std::optional<std::string> material;
                if(index<job.Output.Layouts.size())for(const auto& stock:problem.Stocks)if(stock.ID==job.Output.Layouts[index].StockID) { material=stock.Material;break; }
                o<<"{\"materialKey\":";if(material)o<<'\"'<<Escape(*material)<<'\"';else o<<"null";o<<",\"layoutIndex\":"<<index<<",\"area\":";
                if(index<evaluation.PerLayout.size())number(evaluation.PerLayout[index].Area);else o<<"null";o<<'}';
            }
            o<<"]}";
        }
        else o<<"null";
    }
    void ResultJson(std::ostream& o,const Result& r)
    {
        o<<"{\"status\":\""<<State(r.State)<<"\",\"geometryValidated\":"<<(r.GeometryValidated?"true":"false")<<",\"layouts\":[";
        for(std::size_t i=0;i<r.Layouts.size();++i)
        {
            if(i)o<<',';const auto& l=r.Layouts[i];o<<"{\"stock\":\""<<Escape(l.StockID)<<"\",\"instance\":"<<l.StockInstance<<",\"width\":"<<l.Width<<",\"height\":"<<l.Height<<",\"materialArea\":"<<l.MaterialArea<<",\"placements\":[";
            for(std::size_t j=0;j<l.Placements.size();++j) { if(j)o<<',';const auto& q=l.Placements[j];o<<"{\"part\":\""<<Escape(q.PartID)<<"\",\"instance\":"<<q.Instance<<",\"x\":"<<q.Transform.X<<",\"y\":"<<q.Transform.Y<<",\"angle\":"<<q.Transform.AngleDegrees<<",\"group\":"<<q.Group<<",\"filler\":"<<(q.GapFiller?"true":"false")<<'}'; }o<<"]}";
        }
        o<<"],\"remaining\":[";for(std::size_t i=0;i<r.Remaining.size();++i) { if(i)o<<',';o<<"{\"part\":\""<<Escape(r.Remaining[i].PartID)<<"\",\"quantity\":"<<r.Remaining[i].Quantity<<'}'; }
        o<<"],\"diagnostics\":";Strings(o,r.Diagnostics);o<<",\"stats\":{\"attempts\":"<<r.Stats.PlacementAttempts<<",\"patterns\":"<<r.Stats.CandidatePatterns<<",\"combinationGroups\":"<<r.Stats.CombinationGroups<<",\"circleLatticeGroups\":"<<r.Stats.CircleLatticeGroups<<",\"fillers\":"<<r.Stats.GapFilledParts<<",\"reportedSeconds\":"<<r.Stats.ElapsedSeconds<<",\"phaseSeconds\":";PhaseTimings(o,r.Stats);o<<"}}";
    }
    Metrics Measure(const Problem& p,const Result& r)
    {
        Metrics m;m.Boards=r.Layouts.size();std::size_t demanded=0,placed=0;for(const auto& part:p.Parts)demanded+=part.Quantity;
        for(const auto& l:r.Layouts)
        {
            double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;
            for(const auto& q:l.Placements)
            {
                ++placed;const auto it=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& part){return part.ID==q.PartID;});if(it==p.Parts.end())continue;const auto& s=it->Geometry;
                if(s.Radius>0) { const auto c=World(s.Center,q.Transform);x0=std::min(x0,c.X-s.Radius);y0=std::min(y0,c.Y-s.Radius);x1=std::max(x1,c.X+s.Radius);y1=std::max(y1,c.Y+s.Radius); }
                else for(auto point:s.Outer) { point=World(point,q.Transform);x0=std::min(x0,point.X);y0=std::min(y0,point.Y);x1=std::max(x1,point.X);y1=std::max(y1,point.Y); }
            }
            if(std::isfinite(x0)) { m.AABB+=(x1-x0)*(y1-y0);m.Top+=y1;m.Right+=x1;m.TopSlack+=l.Height-p.EdgeMargin-y1;m.RightSlack+=l.Width-p.EdgeMargin-x1; }
        }
        m.Missing=demanded-std::min(demanded,placed);return m;
    }
    void MetricsJson(std::ostream& o,const Metrics& m)
    { o<<"{\"missing\":"<<m.Missing<<",\"boards\":"<<m.Boards<<",\"usedAABBarea\":"<<m.AABB<<",\"top\":"<<m.Top<<",\"right\":"<<m.Right<<",\"sumTop\":"<<m.Top<<",\"sumRight\":"<<m.Right<<",\"topSlack\":"<<m.TopSlack<<",\"rightSlack\":"<<m.RightSlack<<",\"wallSeconds\":"<<m.Wall<<'}'; }
    void FinalizeWitness(Sample& c,std::size_t tiles)
    {
        c.TilesRequested=tiles;c.TilesApplied=c.Known&&c.Witness.Layouts.size()==1?tiles:1;
        if(c.Known&&tiles>1&&c.Witness.Layouts.size()==1)
        {
            if(c.Input.Stocks.size()!=1||c.Witness.Layouts.size()!=1)throw std::runtime_error("tiling currently requires a single-sheet witness");
            const auto copies=tiles*tiles;std::size_t total=0;for(const auto& part:c.Input.Parts)total+=part.Quantity;
            if(total>100000/copies)throw std::runtime_error("tiled demand exceeds solver's 100000-instance input limit");
            const double margin=c.Input.EdgeMargin,gap=c.Input.PartGap;
            const double usableWidth=c.Input.Stocks[0].Width-2*margin,usableHeight=c.Input.Stocks[0].Height-2*margin;
            if(usableWidth<=0||usableHeight<=0)throw std::runtime_error("cannot tile an empty usable witness sheet");
            const double stepX=usableWidth+gap,stepY=usableHeight+gap;
            const auto original=c.Witness.Layouts[0].Placements;std::map<std::string,std::size_t> originalQuantities;
            for(auto& part:c.Input.Parts) { originalQuantities[part.ID]=part.Quantity;part.Quantity*=copies; }
            c.Witness.Layouts[0].Placements.clear();c.Witness.Layouts[0].Placements.reserve(original.size()*copies);
            for(std::size_t row=0;row<tiles;++row)for(std::size_t col=0;col<tiles;++col)
            {
                const auto tile=row*tiles+col;
                for(auto placed:original)
                {
                    placed.Instance+=tile*originalQuantities.at(placed.PartID);
                    placed.Transform.X+=col*stepX;placed.Transform.Y+=row*stepY;
                    c.Witness.Layouts[0].Placements.push_back(std::move(placed));
                }
            }
            auto& expandedStock=c.Input.Stocks[0];expandedStock.Width=tiles*usableWidth+(tiles-1)*gap+2*margin;expandedStock.Height=tiles*usableHeight+(tiles-1)*gap+2*margin;
            c.Witness.Layouts[0].Width=expandedStock.Width;c.Witness.Layouts[0].Height=expandedStock.Height;
        }
        if(c.Known)
        {
            const auto begin=std::chrono::steady_clock::now();auto v=Validate(c.Input,c.Witness);c.WitnessValidationSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            if(!v.Valid||!v.Complete) { std::string e;for(const auto& x:v.Errors)e+=x+"; ";throw std::runtime_error("invalid generated witness "+std::to_string(c.ID)+": "+e); }c.Witness.GeometryValidated=true;
            if(c.Witness.Layouts.size()==1)c.Witness.Layouts[0].MaterialArea=v.MaterialArea;
            else for(auto& layout:c.Witness.Layouts)
            {
                layout.MaterialArea=0;
                for(const auto& placed:layout.Placements)for(const auto& part:c.Input.Parts)if(part.ID==placed.PartID) { layout.MaterialArea+=Classify(part.Geometry,c.Input.Tolerance).Area;break; }
            }
        }
    }
    bool VerifyFreeRectangle(const Problem& problem,const Result& result,std::size_t index,const Rect& rectangle)
    {
        if(index>=result.Layouts.size()||rectangle.Width<=0||rectangle.Height<=0)return false;
        auto p=problem;auto r=result;Part free;free.ID="__reference-free-rectangle";free.Geometry=Box(rectangle.Width,rectangle.Height);free.FreeRotation=false;free.AllowedAnglesDegrees={0};
        for(const auto& stock:p.Stocks)if(stock.ID==r.Layouts[index].StockID)free.Material=stock.Material;
        p.Parts.push_back(free);r.Layouts[index].Placements.push_back({free.ID,0,{rectangle.X,rectangle.Y,0}});r.Layouts[index].MaterialArea+=rectangle.Width*rectangle.Height;
        const auto checked=Validate(p,r);return checked.Valid&&checked.Complete;
    }
    Sample GenerateResiduals(std::size_t id)
    {
        Sample c;c.ID=id;c.Seed=4000003+id*7919;c.Suite="residuals";c.Family="residuals.equal-aabb-"+std::to_string(id%5);
        const auto variant=id%5;const double scale=1+.1*static_cast<double>((id/5)%6),side=100*scale;
        const bool frame=variant==1,circles=variant==2;const std::size_t groups=variant==4?2:1,copies=variant==3?2:1;
        c.Witness.State=Status::Complete;ReferenceLayout good,bad;good.Label="clustered";bad.Label="fragmented";bad.Output.State=Status::Complete;
        for(std::size_t group=0;group<groups;++group)
        {
            const std::string material=groups==2?(group?"aluminium":"steel"):"steel",suffix=std::to_string(group);
            Stock stock;stock.ID="stock-"+suffix;stock.Width=stock.Height=side;stock.Quantity=copies;stock.Material=material;c.Input.Stocks.push_back(stock);
            Shape anchor;
            if(frame) { anchor=Box(side,side);anchor.Holes={{{5*scale,5*scale},{5*scale,95*scale},{95*scale,95*scale},{95*scale,5*scale}}}; }
            else anchor.Outer={{0,0},{side,0},{side,5*scale},{5*scale,5*scale},{5*scale,side},{0,side}};
            Part large;large.ID="anchor-"+suffix;large.Geometry=std::move(anchor);large.Quantity=copies;large.Material=material;large.FreeRotation=false;large.AllowedAnglesDegrees={0};c.Input.Parts.push_back(large);
            Part small;small.ID="small-"+suffix;small.Geometry=circles?Disk(5*scale):Box((frame?15:10)*scale,(frame?8:10)*scale);small.Quantity=3*copies;small.Material=material;small.FreeRotation=false;small.AllowedAnglesDegrees={0};c.Input.Parts.push_back(small);
            for(std::size_t copy=0;copy<copies;++copy)
            {
                Layout clustered;clustered.StockID=stock.ID;clustered.StockInstance=copy;clustered.Width=clustered.Height=side;
                clustered.Placements.push_back({large.ID,copy,{0,0,0}});auto fragmented=clustered;
                for(std::size_t piece=0;piece<3;++piece)
                {
                    const double x=circles?10+10*static_cast<double>(piece):5+(frame?15:10)*static_cast<double>(piece),y=circles?10:5;
                    clustered.Placements.push_back({small.ID,copy*3+piece,{x*scale,y*scale,0}});
                    const double bx=circles?(piece==1?70:40):(frame?(piece==1?60:30):(piece==1?65:35));
                    const double by=circles?30+25*static_cast<double>(piece):25+25*static_cast<double>(piece);
                    fragmented.Placements.push_back({small.ID,copy*3+piece,{bx*scale,by*scale,0}});
                }
                c.Witness.Layouts.push_back(clustered);bad.Output.Layouts.push_back(fragmented);
                good.FreeRectangles.push_back({5*scale,(frame?13:15)*scale,(frame?90:95)*scale,(frame?82:85)*scale});
                bad.FreeRectangles.push_back({5*scale,5*scale,(frame?25:30)*scale,(frame?90:95)*scale});
                good.AreaUpperBounds.push_back((circles?9025:frame?7380:8075)*scale*scale);
                bad.AreaUpperBounds.push_back((circles?6650:frame?5580:6175)*scale*scale);
            }
        }
        FinalizeWitness(c,1);good.Output=c.Witness;
        const auto badCheck=Validate(c.Input,bad.Output);if(!badCheck.Valid||!badCheck.Complete)throw std::runtime_error("invalid fragmented residual witness");bad.Output.GeometryValidated=true;
        for(std::size_t i=0;i<bad.Output.Layouts.size();++i)bad.Output.Layouts[i].MaterialArea=c.Witness.Layouts[i].MaterialArea;
        c.References={good,bad};
        for(const auto& reference:c.References)for(std::size_t i=0;i<reference.FreeRectangles.size();++i)
            if(!VerifyFreeRectangle(c.Input,reference.Output,i,reference.FreeRectangles[i]))throw std::runtime_error("unverified residual rectangle certificate");
        const auto a=Measure(c.Input,good.Output),b=Measure(c.Input,bad.Output);
        if(a.Boards!=b.Boards||std::abs(a.AABB-b.AABB)>c.Input.Tolerance||good.FreeRectangles[0].Width*good.FreeRectangles[0].Height<=bad.AreaUpperBounds[0])
            throw std::runtime_error("residual pair must have equal board/AABB metrics and separated regular-remnant bounds");
        return c;
    }
    Sample Generate(std::size_t id,std::size_t tiles=1)
    {
        Sample c;c.ID=id;c.Seed=1000003+id*7919;std::mt19937_64 rng(c.Seed);auto integer=[&](int a,int b){return std::uniform_int_distribution<int>(a,b)(rng);};
        c.Witness.State=Status::Complete;c.Witness.Layouts.push_back({});
        auto stock=[&](double w,double h) { Stock s;s.ID="S0";s.Width=w;s.Height=h;c.Input.Stocks={s};c.Witness.Layouts[0].StockID="S0";c.Witness.Layouts[0].Width=w;c.Witness.Layouts[0].Height=h; };
        auto add=[&](std::string name,Shape shape,const std::vector<Pose>& poses,std::vector<double> angles=std::vector<double>{0}) {
            Part p;p.ID=std::move(name);p.Geometry=std::move(shape);p.Quantity=poses.size();p.FreeRotation=false;p.AllowedAnglesDegrees=std::move(angles);
            for(std::size_t i=0;i<poses.size();++i)c.Witness.Layouts[0].Placements.push_back({p.ID,i,poses[i]});c.Input.Parts.push_back(std::move(p));
        };
        switch(id%10)
        {
        case 0:
        {
            c.Family="witness.rectangles";const int columns=integer(2,3),rows=integer(2,3);std::vector<double> widths(columns),heights(rows);for(auto& w:widths)w=integer(20,65);for(auto& h:heights)h=integer(15,50);
            const double gap=integer(0,2),margin=integer(0,2);c.Input.PartGap=gap;c.Input.EdgeMargin=margin;
            stock(std::accumulate(widths.begin(),widths.end(),0.0)+(columns-1)*gap+2*margin,std::accumulate(heights.begin(),heights.end(),0.0)+(rows-1)*gap+2*margin);
            double y=margin;for(int row=0;row<rows;++row) { double x=margin;for(int col=0;col<columns;++col) { add("R"+std::to_string(row*columns+col),Box(widths[col],heights[row]),{{x,y,0}},{0,90});x+=widths[col]+gap; }y+=heights[row]+gap; }break;
        }
        case 1:
        {
            c.Family="witness.parallelograms";const int rows=integer(2,3),columns=integer(2,3);std::vector<double> heights(rows),shears(rows);std::vector<std::vector<double>> bases(rows,std::vector<double>(columns));double width=0,height=0;
            for(int row=0;row<rows;++row) { heights[row]=integer(20,45);shears[row]=integer(8,20);for(auto& b:bases[row])b=integer(30,75);width=std::max(width,std::accumulate(bases[row].begin(),bases[row].end(),shears[row]));height+=heights[row]; }
            stock(width,height);double y=0;for(int row=0;row<rows;++row) { double x=0;for(int col=0;col<columns;++col) { add("P"+std::to_string(row*columns+col),Para(bases[row][col],heights[row],shears[row]),{{x,y,0}});x+=bases[row][col]; }y+=heights[row]; }break;
        }
        case 2:
        {
            c.Family="witness.circles-central";const double r=integer(8,25),gap=integer(0,1),margin=integer(0,2),pitch=2*r+gap;
            c.Input.PartGap=gap;c.Input.EdgeMargin=margin;stock(4*r+gap+2*margin,4*r+gap+2*margin);
            const double a=margin+r,b=a+pitch,small=(pitch/std::sqrt(2.0)-r-gap)*(.65+.05*integer(0,4));
            add("large-A",Disk(r),{{a,a,0},{b,a,0}});add("large-B",Disk(r),{{a,b,0},{b,b,0}});add("small",Disk(small),{{(a+b)/2,(a+b)/2,0}});break;
        }
        case 3:
        {
            c.Family="witness.cross-basic";const double scale=integer(6,15)/10.0;stock(220*scale,100*scale);
            add("rectangles",Box(60*scale,40*scale),{{0,0,0},{60*scale,0,0}});
            add("parallelograms",Para(50*scale,40*scale,15*scale),{{0,45*scale,0},{50*scale,45*scale,0}});
            add("circles",Disk(10*scale),{{150*scale,20*scale,0},{175*scale,20*scale,0},{200*scale,20*scale,0}});break;
        }
        case 4:
        {
            c.Family="witness.concave-complements";const double w=integer(90,150),h=integer(60,100),x=std::floor(w*.3),y=std::floor(h*.4);Shape s;
            if((id/10)%2==0) { s.Outer={{0,0},{w,0},{w,y},{x,y},{x,h},{0,h}};stock(2*w,h);add("L",s,{{0,0,0},{w,0,0}},{0,180});add("notch",Box(w-x,h-y),{{x,y,0},{w+x,y,0}},{0,180}); }
            else { s.Outer={{0,h-y},{x,h-y},{x,0},{w-x,0},{w-x,h-y},{w,h-y},{w,h},{0,h}};stock(w,h);add("T",s,{{0,0,0}},{0,180});add("notch",Box(x,h-y),{{0,0,0},{w-x,0,0}},{0,180}); }break;
        }
        case 5:
        {
            c.Family="witness.hole-fill";const double w=integer(120,200),h=integer(100,150),x=w*.25,y=h*.25;stock(w,h);auto frame=Box(w,h);frame.Holes={{{x,y},{x,h-y},{w-x,h-y},{w-x,y}}};
            add("frame",frame,{{0,0,0}});add("hole-parts",Box((w-2*x)/2,h-2*y),{{x,y,0},{w/2,y,0}},{0,90});break;
        }
        case 6:
        {
            c.Family="witness.slender-positive-gap";const double w=integer(4,10),h=integer(100,180),gap=integer(1,3),margin=2;c.Input.PartGap=gap;c.Input.EdgeMargin=margin;
            stock(4*w+3*gap+2*margin,h+gap+20+2*margin);std::vector<Pose> poses;for(int i=0;i<4;++i)poses.push_back({margin+i*(w+gap),margin,0});add("slender",Box(w,h),poses);
            add("short",Box(2*w+gap,20),{{margin,margin+h+gap,0},{margin+2*(w+gap),margin+h+gap,0}});break;
        }
        case 7:
        {
            c.Family="witness.restricted-angles";stock(104,59);c.Input.EdgeMargin=2;
            for(int row=0;row<2;++row)for(int col=0;col<2;++col)
            {
                const double w=col?60:40,h=row?35:20,angle=integer(0,1)?17:33;auto shape=Box(w,h);for(auto& q:shape.Outer)q=Rotate(q,angle);
                add("rotated-"+std::to_string(row*2+col),shape,{{2.0+(col?40:0),2.0+(row?20:0),360-angle}},{360-angle});
            }break;
        }
        case 8:
        {
            c.Family="witness.three-radii";const double s=integer(5,15)/10.0;stock(100*s,82*s);
            add("R20",Disk(20*s),{{20*s,20*s,0},{60*s,20*s,0}});add("R12",Disk(12*s),{{12*s,52*s,0},{36*s,52*s,0},{60*s,52*s,0}});
            std::vector<Pose> poses;for(int i=0;i<6;++i)poses.push_back({(5+10*i)*s,75*s,0});add("R5",Disk(5*s),poses);break;
        }
        default:
        {
            c.Family="random.unproven";c.Known=false;stock(integer(140,230),integer(90,170));c.Input.Stocks[0].Quantity=integer(1,2);c.Input.PartGap=integer(0,2);c.Input.EdgeMargin=1;
            const int kinds=integer(3,6);for(int i=0;i<kinds;++i) { Shape s;switch(integer(0,2)) { case 0:s=Box(integer(15,75),integer(15,65));break;case 1:s=Para(integer(20,65),integer(15,45),integer(5,18));break;default:s=Disk(integer(5,25)); }
                Part p;p.ID="random-"+std::to_string(i);p.Geometry=std::move(s);p.Quantity=integer(2,5);p.FreeRotation=false;p.AllowedAnglesDegrees={0,90};c.Input.Parts.push_back(std::move(p)); }
            c.Witness={};break;
        }
        }
        // Shift each source's local origin without changing its physical witness geometry.
        for(auto& part:c.Input.Parts)if(integer(0,2)==0)
        {
            const Point shift{static_cast<double>(integer(-15,15)),static_cast<double>(integer(-15,15))};
            for(auto& q:part.Geometry.Outer) { q.X+=shift.X;q.Y+=shift.Y; }for(auto& ring:part.Geometry.Holes)for(auto& q:ring) { q.X+=shift.X;q.Y+=shift.Y; }part.Geometry.Center.X+=shift.X;part.Geometry.Center.Y+=shift.Y;
            if(c.Known)for(auto& q:c.Witness.Layouts[0].Placements)if(q.PartID==part.ID) { const auto rotated=Rotate(shift,q.Transform.AngleDegrees);q.Transform.X-=rotated.X;q.Transform.Y-=rotated.Y; }
        }
        std::shuffle(c.Input.Parts.begin(),c.Input.Parts.end(),rng);
        FinalizeWitness(c,tiles);
        return c;
    }
    Sample GenerateMotif(std::size_t id,std::size_t tiles,const std::string& suite)
    {
        Sample c;c.ID=id;c.Seed=2000003+id*7919;c.Suite=suite;const auto variant=id%6;
        const std::size_t pieces=variant==0||variant==2?3:4;
        const std::size_t families=variant<2?1:variant<4?4:variant==4?8:12;
        const std::size_t quantity=variant<2?1:variant==5?3:2;
        const double w=800+50*static_cast<double>((id/6)%9),h=700+50*static_cast<double>((id/54)%9),shear=suite=="adversarial"?(id%2?.4:.25):0;
        c.Family=(shear>0?"adversarial.sheared-nested-":"motifs.nested-")+std::to_string(pieces);
        Stock stock;stock.ID="S0";stock.Width=w+shear*h;stock.Height=h;stock.Quantity=families*quantity;c.Input.Stocks={stock};
        for(std::size_t f=0;f<families;++f)
        {
            double x=0,y=0;
            for(std::size_t k=0;k<pieces;++k)
            {
                Shape shape;
                if(k+1<pieces)
                {
                    const double nx=x+(w-x)*(.28+.035*((f+2*k+id/6)%5)),ny=y+(h-y)*(.32+.025*((f+k+id/6)%4));
                    shape.Outer={{x,y},{w,y},{w,ny},{nx,ny},{nx,h},{x,h}};x=nx;y=ny;
                }
                else shape.Outer={{x,y},{w,y},{w,h},{x,h}};
                if(shear>0)for(auto& point:shape.Outer)point.X+=shear*point.Y;
                Part part;part.ID="family-"+std::to_string(f)+"-piece-"+std::to_string(k);part.Geometry=std::move(shape);part.Quantity=quantity;part.FreeRotation=false;part.AllowedAnglesDegrees={0};c.Input.Parts.push_back(std::move(part));
            }
            for(std::size_t q=0;q<quantity;++q)
            {
                Layout layout;layout.StockID="S0";layout.StockInstance=f*quantity+q;layout.Width=stock.Width;layout.Height=stock.Height;
                for(std::size_t k=0;k<pieces;++k)layout.Placements.push_back({"family-"+std::to_string(f)+"-piece-"+std::to_string(k),q,{0,0,0}});
                c.Witness.Layouts.push_back(std::move(layout));
            }
        }
        c.Witness.State=Status::Complete;std::mt19937_64 rng(c.Seed);
        for(auto& part:c.Input.Parts)if(rng()%3==0)
        {
            const Point shift{static_cast<double>(static_cast<int>(rng()%31)-15),static_cast<double>(static_cast<int>(rng()%31)-15)};
            for(auto& point:part.Geometry.Outer) { point.X+=shift.X;point.Y+=shift.Y; }
            for(auto& layout:c.Witness.Layouts)for(auto& placed:layout.Placements)if(placed.PartID==part.ID) { placed.Transform.X-=shift.X;placed.Transform.Y-=shift.Y; }
        }
        std::shuffle(c.Input.Parts.begin(),c.Input.Parts.end(),rng);FinalizeWitness(c,tiles);return c;
    }
    Sample GenerateJigsaw(std::size_t id,std::size_t tiles)
    {
        constexpr int validSeeds[]={5,6,10,15,20,24,25,30,35,36,39,40,45,48,50,54,55,60,65,69,70,75,80,85};
        constexpr int side=12;const int seed=validSeeds[id%24];const double scale=50+5*static_cast<double>((id/96)%8);int labels[side][side];
        for(int y=0;y<side;++y)for(int x=0;x<side;++x)
        {
            const int cut=6+((y*seed+seed/2)%5)-2;const bool left=x<cut;
            const int height=left?6+((x*(seed+3)+seed)%3)-1:6+((x*(seed+5)+seed/3)%3)-1;
            labels[y][x]=(left?0:2)+(y>=height?1:0);
        }
        Sample c;c.ID=id;c.Seed=3000003+id*7919;c.Suite="jigsaw";c.Family="motifs.four-jigsaw";c.Witness.State=Status::Complete;
        Stock stock;stock.ID="S0";stock.Width=stock.Height=side*scale;c.Input.Stocks={stock};Layout layout;layout.StockID="S0";layout.Width=layout.Height=side*scale;
        using Vertex=std::pair<int,int>;
        for(int label=0;label<4;++label)
        {
            std::map<Vertex,Vertex> edges;
            const auto edge=[&](Vertex a,Vertex b) { if(!edges.emplace(a,b).second)throw std::runtime_error("ambiguous jigsaw boundary"); };
            for(int y=0;y<side;++y)for(int x=0;x<side;++x)if(labels[y][x]==label)
            {
                if(y==0||labels[y-1][x]!=label)edge({x,y},{x+1,y});if(x+1==side||labels[y][x+1]!=label)edge({x+1,y},{x+1,y+1});
                if(y+1==side||labels[y+1][x]!=label)edge({x+1,y+1},{x,y+1});if(x==0||labels[y][x-1]!=label)edge({x,y+1},{x,y});
            }
            if(edges.empty())throw std::runtime_error("empty jigsaw region");const auto start=edges.begin()->first;auto vertex=start;Shape shape;
            do
            {
                Point point{vertex.first*scale,vertex.second*scale};
                for(std::size_t turn=0;turn<(id/24)%4;++turn)point={side*scale-point.Y,point.X};
                shape.Outer.push_back(point);const auto found=edges.find(vertex);
                if(found==edges.end()||shape.Outer.size()>edges.size())throw std::runtime_error("open jigsaw boundary");vertex=found->second;
            }while(vertex!=start);
            if(shape.Outer.size()!=edges.size())throw std::runtime_error("disconnected jigsaw region");
            Part part;part.ID="jigsaw-"+std::to_string(label);part.Geometry=std::move(shape);part.FreeRotation=false;part.AllowedAnglesDegrees={0};
            layout.Placements.push_back({part.ID,0,{0,0,0}});c.Input.Parts.push_back(std::move(part));
        }
        c.Witness.Layouts.push_back(std::move(layout));std::mt19937_64 rng(c.Seed);
        for(auto& part:c.Input.Parts)if(rng()%3==0)
        {
            const Point shift{static_cast<double>(static_cast<int>(rng()%31)-15),static_cast<double>(static_cast<int>(rng()%31)-15)};
            for(auto& point:part.Geometry.Outer) { point.X+=shift.X;point.Y+=shift.Y; }
            for(auto& placed:c.Witness.Layouts[0].Placements)if(placed.PartID==part.ID) { placed.Transform.X-=shift.X;placed.Transform.Y-=shift.Y; }
        }
        std::shuffle(c.Input.Parts.begin(),c.Input.Parts.end(),rng);FinalizeWitness(c,tiles);return c;
    }
    Sample GenerateForSuite(std::size_t id,const Settings& settings)
    {
        auto c=settings.Suite=="base"?Generate(id,settings.Tiles):settings.Suite=="jigsaw"?GenerateJigsaw(id,settings.Tiles):settings.Suite=="residuals"?GenerateResiduals(id):GenerateMotif(id,settings.Tiles,settings.Suite);c.Order=settings.Order;
        if(c.Order==1)std::reverse(c.Input.Parts.begin(),c.Input.Parts.end());
        if(c.Order==2||c.Order==3)std::stable_sort(c.Input.Parts.begin(),c.Input.Parts.end(),[&](const Part& a,const Part& b){return c.Order==2?a.ID<b.ID:a.ID>b.ID;});
        if(c.Order==4) { std::mt19937_64 rng(c.Seed^0x5eed1234ULL);std::shuffle(c.Input.Parts.begin(),c.Input.Parts.end(),rng); }
        return c;
    }
    void ReferencesJson(std::ostream& o,const Sample& c)
    {
        o<<'[';
        for(std::size_t r=0;r<c.References.size();++r)
        {
            if(r)o<<',';const auto& reference=c.References[r];o<<"{\"label\":\""<<reference.Label<<"\",\"sameBoardsAndAABB\":true,\"result\":";ResultJson(o,reference.Output);o<<",\"metrics\":";MetricsJson(o,Measure(c.Input,reference.Output));
            o<<",\"certificates\":[";std::map<std::string,std::pair<double,double>> materialAreas;
            for(std::size_t i=0;i<reference.FreeRectangles.size();++i)
            {
                if(i)o<<',';const auto& rectangle=reference.FreeRectangles[i];const double area=rectangle.Width*rectangle.Height;
                o<<"{\"layoutIndex\":"<<i<<",\"bounds\":";RectangleJson(o,rectangle);o<<",\"area\":"<<area<<",\"areaUpperBound\":"<<reference.AreaUpperBounds[i]<<",\"verified\":true,\"upperBoundBasis\":\"analytic usable-area or separating-obstacle bound, not a grid estimate\"}";
                for(const auto& stock:c.Input.Stocks)if(stock.ID==reference.Output.Layouts[i].StockID) { auto& group=materialAreas[stock.Material];group.first=std::max(group.first,area);group.second=std::max(group.second,reference.AreaUpperBounds[i]);break; }
            }
            double lower=0,upper=0;for(const auto& [material,areas]:materialAreas) { static_cast<void>(material);lower+=areas.first;upper+=areas.second; }
            o<<"],\"lastSheetRegularAreaLowerBound\":"<<lower<<",\"lastSheetRegularAreaUpperBound\":"<<upper<<'}';
        }
        o<<']';
    }
    bool Eligible(const TimedJob& j) { return j.Job.Checked.Valid&&(j.Job.Output.State==Status::Complete||j.Job.Output.State==Status::Partial||j.Job.Output.State==Status::Cancelled); }
    std::size_t Select(const std::vector<TimedJob>& jobs,bool lowLeft)
    {
        std::size_t best=static_cast<std::size_t>(-1);
        for(std::size_t i=0;i<jobs.size();++i)if(Eligible(jobs[i]))
        {
            const auto key=[&](const TimedJob& j) { const auto& m=j.Metric;return std::tuple(m.Missing,m.Boards,lowLeft?m.Top:m.AABB,lowLeft?m.Right:m.Top,lowLeft?m.AABB:m.Right); };
            if(best==static_cast<std::size_t>(-1)||key(jobs[i])<key(jobs[best]))best=i;
        }return best;
    }
    std::size_t SelectRemnant(const std::vector<TimedJob>& jobs)
    {
        std::optional<std::pair<std::size_t,std::size_t>> prefix;
        for(const auto& j:jobs)if(Eligible(j)) { const auto key=std::pair(j.Metric.Missing,j.Metric.Boards);if(!prefix||key<*prefix)prefix=key; }
        std::size_t best=static_cast<std::size_t>(-1);
        if(!prefix)return best;
        for(std::size_t i=0;i<jobs.size();++i)if(Eligible(jobs[i])&&std::pair(jobs[i].Metric.Missing,jobs[i].Metric.Boards)==*prefix)
        {
            const auto area=RemnantArea(jobs[i].Job);if(!area)continue;
            if(best==static_cast<std::size_t>(-1)||std::tuple(-*area,jobs[i].Metric.Top,jobs[i].Metric.Right,jobs[i].Metric.AABB)<
                std::tuple(-*RemnantArea(jobs[best].Job),jobs[best].Metric.Top,jobs[best].Metric.Right,jobs[best].Metric.AABB))best=i;
        }
        return best;
    }
    std::vector<TimedJob> Run(const Sample& c,const Settings& settings,const StrategyRegistry& registry,const std::vector<std::string>& ids)
    {
        std::vector<TimedJob> jobs(ids.size());std::atomic<std::size_t> next{0};
        const auto work=[&] {
            for(;;) { const auto i=next.fetch_add(1);if(i>=ids.size())break;Options o;o.TimeLimitSeconds=settings.Time;o.MaxPlacementAttempts=settings.Attempts;o.MaxPatterns=128;o.Seed=c.Seed+i;
                const auto begin=std::chrono::steady_clock::now();jobs[i].Job=RunJob({std::to_string(c.ID)+":"+ids[i],ids[i],c.Input,o},registry);jobs[i].Metric=Measure(c.Input,jobs[i].Job.Output);jobs[i].Metric.Wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count(); }
        };
        std::vector<std::jthread> workers;const auto count=std::min(settings.Workers,ids.size());for(std::size_t i=1;i<count;++i)workers.emplace_back(work);work();for(auto& worker:workers)worker.join();return jobs;
    }
    bool BudgetHit(const Result& r) { for(const auto& d:r.Diagnostics)if(d.find("budget")!=std::string::npos||d.find("Budget")!=std::string::npos)return true;return false; }
    void CheckedJson(std::ostream& o,const Validation& v)
    { o<<"{\"valid\":"<<(v.Valid?"true":"false")<<",\"complete\":"<<(v.Complete?"true":"false")<<",\"placed\":"<<v.PlacedCount<<",\"materialCost\":"<<v.MaterialCost<<",\"materialArea\":"<<v.MaterialArea<<",\"sheetArea\":"<<v.SheetArea<<",\"errors\":";Strings(o,v.Errors);o<<'}'; }
    Settings Arguments(int argc,char** argv)
    {
        Settings s;for(int i=1;i<argc;++i) { const std::string name=argv[i];if(i+1>=argc)throw std::runtime_error("missing CLI value");const std::string value=argv[++i];
            if(name=="--count")s.Count=std::stoull(value);else if(name=="--offset")s.Offset=std::stoull(value);else if(name=="--out")s.Out=value;else if(name=="--time")s.Time=std::stod(value);else if(name=="--attempts")s.Attempts=std::stoull(value);else if(name=="--workers")s.Workers=std::stoull(value);else if(name=="--tiles")s.Tiles=std::stoull(value);else if(name=="--order")s.Order=std::stoull(value);else if(name=="--suite")s.Suite=value;
            else if(name=="--strategies") { std::istringstream in(value);std::string id;while(std::getline(in,id,','))if(!id.empty())s.Strategies.push_back(id); }else throw std::runtime_error("unknown option "+name);
        }
        if(!s.Count||s.Count>1000000||!std::isfinite(s.Time)||s.Time<=0||!s.Attempts||!s.Workers||s.Workers>32||!s.Tiles||s.Tiles>16||s.Order>4||(s.Suite!="base"&&s.Suite!="motifs"&&s.Suite!="adversarial"&&s.Suite!="jigsaw"&&s.Suite!="residuals")||(s.Suite=="residuals"&&s.Tiles!=1))throw std::runtime_error("invalid options: tiles 1..16, order 0..4, suite base/motifs/adversarial/jigsaw/residuals (residual reference pairs require tiles1)");return s;
    }
}

int main(int argc,char** argv)
{
    try
    {
        const auto settings=Arguments(argc,argv);const auto registry=DefaultStrategies();const auto ids=settings.Strategies.empty()?registry.IDs():settings.Strategies;const auto available=registry.IDs();
        for(const auto& id:ids)if(std::find(available.begin(),available.end(),id)==available.end())throw std::runtime_error("unknown requested strategy: "+id);
        std::filesystem::create_directories(settings.Out);std::ofstream cases(settings.Out/"cases.jsonl"),csv(settings.Out/"jobs.csv");if(!cases||!csv)throw std::runtime_error("cannot create output files");cases<<std::setprecision(17);csv<<std::setprecision(17);
        csv<<"case,suite,orderVariant,family,knownFeasible,tilesRequested,tilesApplied,strategy,valid,complete,missing,boards,usedAABBarea,sumTop,sumRight,topSlack,rightSlack,materialCost,materialArea,sheetArea,wallSeconds,reportedSeconds,attempts,patterns,combinationGroups,fillers,zeroPlacementBudgetHit,lastRegularArea,lastRegularUpper,remnantsMeasured,witnessBoards,witnessAABB,witnessTop,witnessRight\n";
        std::size_t known=0,knownComplete=0,invalidJobs=0,zeroBudget=0;std::map<std::string,std::pair<std::size_t,std::size_t>> families;std::vector<std::pair<double,std::string>> worst;
        const auto studyBegin=std::chrono::steady_clock::now();
        for(std::size_t n=0;n<settings.Count;++n)
        {
            const auto c=GenerateForSuite(settings.Offset+n,settings);const auto begin=std::chrono::steady_clock::now();const auto jobs=Run(c,settings,registry,ids);const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            const auto compact=Select(jobs,false),low=Select(jobs,true),remnant=SelectRemnant(jobs);const auto best=remnant<jobs.size()?remnant:compact;
            const auto wm=c.Known?Measure(c.Input,c.Witness):Metrics{};const bool complete=best<jobs.size()&&jobs[best].Job.Checked.Complete;
            if(c.Known) { ++known;if(complete)++knownComplete; }auto& family=families[c.Family];++family.first;if(complete)++family.second;
            std::ostringstream line;line<<std::setprecision(17)<<"{\"schema\":\"icax.sheet-nesting.fake/1\",\"provenance\":{\"instrumentedgeometryroundtrip17\":true,\"serializationDigits\":17},\"case\":"<<c.ID<<",\"baseCase\":"<<c.ID<<",\"suite\":\""<<c.Suite<<"\",\"orderVariant\":"<<c.Order<<",\"seed\":"<<c.Seed<<",\"family\":\""<<c.Family<<"\",\"knownFeasible\":"<<(c.Known?"true":"false")
                <<",\"tilesRequested\":"<<c.TilesRequested<<",\"tilesApplied\":"<<c.TilesApplied<<",\"tilingTag\":\""<<(c.Known?(c.TilesApplied>1?"known-witness-expanded":c.TilesRequested>1&&c.Witness.Layouts.size()>1?"multi-sheet-not-expanded":"base-witness"):"random-not-expanded")<<"\",\"witnessValidationSeconds\":"<<c.WitnessValidationSeconds
                <<",\"optimality\":\""<<(c.Known?"A supplied feasible witness; compactness, low-left metrics and board optimality are not generally proven.":"Unproven random instance; missing parts do not establish infeasibility or algorithm failure.")<<"\",\"settings\":{\"time\":"<<settings.Time<<",\"attempts\":"<<settings.Attempts<<",\"workers\":"<<settings.Workers<<",\"tiles\":"<<settings.Tiles<<",\"order\":"<<settings.Order<<",\"suite\":\""<<settings.Suite<<"\"},\"problem\":";
            ProblemJson(line,c.Input);line<<",\"witness\":";if(c.Known)ResultJson(line,c.Witness);else line<<"null";line<<",\"witnessMetrics\":";if(c.Known)MetricsJson(line,wm);else line<<"null";line<<",\"referenceLayouts\":";ReferencesJson(line,c);
            line<<",\"caseWallSeconds\":"<<wall<<",\"selectionPolicy\":\""<<(remnant<jobs.size()?"regular-remnant":"compact-fallback-unmeasured")<<"\",\"bestJob\":";if(best<jobs.size())line<<best;else line<<"null";
            line<<",\"bestRemnant\":";if(remnant<jobs.size())line<<remnant;else line<<"null";line<<",\"bestCompact\":";if(compact<jobs.size())line<<compact;else line<<"null";line<<",\"bestLowLeft\":";if(low<jobs.size())line<<low;else line<<"null";line<<",\"jobs\":[";
            for(std::size_t i=0;i<jobs.size();++i)
            {
                const auto& j=jobs[i];const bool zero=j.Job.Checked.PlacedCount==0&&BudgetHit(j.Job.Output);if(!j.Job.Checked.Valid)++invalidJobs;if(zero)++zeroBudget;
                if(i)line<<',';line<<"{\"id\":\""<<Escape(j.Job.ID)<<"\",\"strategy\":\""<<Escape(j.Job.StrategyID)<<"\",\"seed\":"<<c.Seed+i<<",\"result\":";ResultJson(line,j.Job.Output);line<<",\"checked\":";CheckedJson(line,j.Job.Checked);line<<",\"metrics\":";MetricsJson(line,j.Metric);line<<",\"remnants\":";RemnantsJson(line,c.Input,j.Job);line<<",\"zeroPlacementBudgetHit\":"<<(zero?"true":"false")<<'}';
                const auto& m=j.Metric;csv<<c.ID<<','<<c.Suite<<','<<c.Order<<','<<c.Family<<','<<c.Known<<','<<c.TilesRequested<<','<<c.TilesApplied<<','<<j.Job.StrategyID<<','<<j.Job.Checked.Valid<<','<<j.Job.Checked.Complete<<','<<m.Missing<<','<<m.Boards<<','<<m.AABB<<','<<m.Top<<','<<m.Right<<','<<m.TopSlack<<','<<m.RightSlack<<','<<j.Job.Checked.MaterialCost<<','<<j.Job.Checked.MaterialArea<<','<<j.Job.Checked.SheetArea<<','<<m.Wall<<','<<j.Job.Output.Stats.ElapsedSeconds<<','<<j.Job.Output.Stats.PlacementAttempts<<','<<j.Job.Output.Stats.CandidatePatterns<<','<<j.Job.Output.Stats.CombinationGroups<<','<<j.Job.Output.Stats.GapFilledParts<<','<<zero<<',';
                const auto remainder=RemnantArea(j.Job),upper=RemnantUpper(j.Job);if(remainder)csv<<*remainder;csv<<',';if(upper)csv<<*upper;csv<<','<<remainder.has_value()<<',';
                if(c.Known)csv<<wm.Boards<<','<<wm.AABB<<','<<wm.Top<<','<<wm.Right;else csv<<",,,";csv<<'\n';
            }
            line<<"]}";const auto record=line.str();cases<<record<<'\n';cases.flush();csv.flush();
            const double severity=c.Known?(best>=jobs.size()?1e9:jobs[best].Metric.Missing*1e6+jobs[best].Metric.AABB/std::max(1.0,wm.AABB)):0;
            worst.emplace_back(severity,record);std::stable_sort(worst.begin(),worst.end(),[](const auto& a,const auto& b){return a.first>b.first;});if(worst.size()>20)worst.pop_back();
            if(n%10==0||n+1==settings.Count)std::cout<<"case "<<c.ID<<" "<<c.Family<<" complete="<<complete<<" known-complete="<<knownComplete<<'/'<<known<<" wall="<<wall<<"s\n"<<std::flush;
        }
        std::ofstream failures(settings.Out/"worst-cases.jsonl");for(const auto& row:worst)failures<<row.second<<'\n';
        std::ofstream summary(settings.Out/"summary.json");summary<<std::setprecision(17)<<"{\"provenance\":{\"instrumentedgeometryroundtrip17\":true,\"serializationDigits\":17},\"offset\":"<<settings.Offset<<",\"count\":"<<settings.Count<<",\"suite\":\""<<settings.Suite<<"\",\"orderVariant\":"<<settings.Order<<",\"tilesRequested\":"<<settings.Tiles<<",\"knownFeasibleCases\":"<<known<<",\"knownFeasibleComplete\":"<<knownComplete<<",\"invalidJobs\":"<<invalidJobs<<",\"zeroPlacementBudgetHits\":"<<zeroBudget
            <<",\"wallSeconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-studyBegin).count()<<",\"strategies\":";Strings(summary,ids);summary<<",\"families\":{";bool first=true;for(const auto& [name,count]:families) { if(!first)summary<<',';first=false;summary<<'\"'<<name<<"\":{\"cases\":"<<count.first<<",\"complete\":"<<count.second<<'}'; }summary<<"},\"phaseTimingAvailable\":"<<(HasPhaseTimings<Statistics>()?"true":"false")<<"}\n";
        std::cout<<"DONE "<<settings.Out.string()<<" known-complete="<<knownComplete<<'/'<<known<<" invalid-jobs="<<invalidJobs<<" zero-placement-budget-hits="<<zeroBudget<<'\n';return invalidJobs?2:0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
