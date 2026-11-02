#include "SheetNesting.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    struct Case { std::string ID, Title; Problem Input; };
    Shape Box(double w,double h) { Shape s; s.Outer={{0,0},{w,0},{w,h},{0,h}}; return s; }
    Shape Tri(double w,double h) { Shape s; s.Outer={{0,0},{w,0},{0,h}}; return s; }
    Part MakePart(std::string id,Shape shape,std::size_t n=1)
    { Part p; p.ID=std::move(id); p.Geometry=std::move(shape); p.Quantity=n; return p; }
    Stock MakeStock(std::string id,double w,double h,std::size_t n=1)
    { Stock s; s.ID=std::move(id); s.Width=w; s.Height=h; s.Quantity=n; return s; }
    const char* State(Status s)
    {
        switch(s) { case Status::Complete:return "Complete"; case Status::Partial:return "Partial";
            case Status::InvalidInput:return "InvalidInput"; case Status::Cancelled:return "Cancelled"; default:return "Failed"; }
    }
    std::string Escape(std::string s,bool xml=false)
    {
        std::string out;
        for(unsigned char c:s)
        {
            if(xml)
            {
                if(c=='&') out+="&amp;"; else if(c=='<') out+="&lt;"; else if(c=='>') out+="&gt;";
                else if(c=='\"') out+="&quot;"; else out+=static_cast<char>(c);
            }
            else
            {
                if(c=='\\') out+="\\\\"; else if(c=='\"') out+="\\\"";
                else if(c=='\n') out+="\\n"; else if(c=='\r') out+="\\r"; else if(c=='\t') out+="\\t";
                else if(c<32) { std::ostringstream o; o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<int>(c); out+=o.str(); }
                else out+=static_cast<char>(c);
            }
        }
        return out;
    }
    const Part& Find(const Problem& p,const std::string& id)
    {
        for(const auto& part:p.Parts) if(part.ID==id) return part;
        throw std::runtime_error("unknown part in result: "+id);
    }
    std::size_t Count(const Result& r)
    { std::size_t n=0; for(const auto& l:r.Layouts)n+=l.Placements.size(); return n; }
    std::vector<Case> Cases()
    {
        std::vector<Case> cases;
        Case c; c.ID="triangles-composite"; c.Title="Six triangles, three square composite groups";
        c.Input.Parts={MakePart("triangle",Tri(100,100),6)}; c.Input.Stocks={MakeStock("sheet",300,100)}; cases.push_back(c);
        c={}; c.ID="circle-hexagonal"; c.Title="23 circles in a 100 x 90 sheet";
        Shape circle; circle.Radius=10; c.Input.Parts={MakePart("circle",circle,23)}; c.Input.Stocks={MakeStock("sheet",100,90)}; cases.push_back(c);
        c={}; c.ID="frame-hole-fill"; c.Title="Four small squares fill the real hole of a frame";
        auto frame=Box(200,200); frame.Holes={{{60,60},{60,140},{140,140},{140,60}}};
        c.Input.Parts={MakePart("frame",frame),MakePart("small",Box(40,40),4)}; c.Input.Stocks={MakeStock("sheet",200,200)}; cases.push_back(c);
        c={}; c.ID="trapezoids"; c.Title="Trapezoid combinations with original part identities";
        Shape trap; trap.Outer={{0,0},{100,0},{70,50},{30,50}};
        c.Input.Parts={MakePart("trapezoid",trap,8)}; c.Input.Stocks={MakeStock("sheet",300,120,2)}; cases.push_back(c);
        c={}; c.ID="mixed-positive-gap"; c.Title="Mixed near rectangles, trapezoids and small fillers; 2 mm gap";
        auto near=Box(110,65); near.Outer={{0,0},{110,0},{110,59},{104,65},{0,65}};
        Shape disk; disk.Radius=8;
        c.Input.Parts={MakePart("near",near,3),MakePart("trapezoid",trap,3),MakePart("small-triangle",Tri(30,30),8),MakePart("disk",disk,8)};
        c.Input.Stocks={MakeStock("sheet",320,180,2)}; c.Input.PartGap=2; c.Input.EdgeMargin=3; cases.push_back(c);
        return cases;
    }
    void DrawSvg(const std::filesystem::path& file,const Case& c,const JobResult& job)
    {
        std::ofstream out(file); if(!out)throw std::runtime_error("cannot write SVG");
        double width=360,height=100;
        for(const auto& l:job.Output.Layouts) { width=std::max(width,l.Width+60); height+=l.Height+55; }
        out<<std::setprecision(12)<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" viewBox=\"0 0 "<<width<<' '<<height<<"\">\n";
        out<<"<rect width=\"100%\" height=\"100%\" fill=\"#f5f7fb\"/>\n";
        out<<"<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#16243a}.part{stroke:#26354d;stroke-width:0.45;stroke-linejoin:round}.filler{stroke:#08783e;stroke-width:0.9} .label{font-size:4.5px;text-anchor:middle}</style>\n";
        out<<"<text x=\"20\" y=\"20\" font-size=\"12\" font-weight=\"600\">"<<Escape(c.Title,true)<<"</text>\n";
        out<<"<text x=\"20\" y=\"36\" font-size=\"8\">"<<Escape(job.StrategyID,true)<<" | "<<State(job.Output.State)<<" | placed "<<Count(job.Output)<<" | geometry "<<(job.Checked.Valid?"VALID":"INVALID")<<"</text>\n";
        out<<"<text x=\"20\" y=\"49\" font-size=\"7\">Dashed grey: usable border. Green outline: gap filler. IDs retain original instances.</text>\n";
        static const char* colors[]={"#a8ccf6","#f7c27f","#a6dcc3","#d4b8e9","#f4aeb9","#c8d481"};
        double top=74;
        for(const auto& l:job.Output.Layouts)
        {
            out<<"<text x=\"25\" y=\""<<top-7<<"\" font-size=\"7\">"<<Escape(l.StockID,true)<<" #"<<l.StockInstance<<" | "<<l.Width<<" x "<<l.Height<<" mm</text>\n";
            out<<"<g transform=\"translate(25 "<<top+l.Height<<") scale(1 -1)\">\n";
            out<<"<rect width=\""<<l.Width<<"\" height=\""<<l.Height<<"\" fill=\"#fffdf6\" stroke=\"#31435c\" stroke-width=\"0.7\"/>\n";
            if(c.Input.EdgeMargin>0)
                out<<"<rect x=\""<<c.Input.EdgeMargin<<"\" y=\""<<c.Input.EdgeMargin<<"\" width=\""<<l.Width-2*c.Input.EdgeMargin<<"\" height=\""<<l.Height-2*c.Input.EdgeMargin<<"\" fill=\"none\" stroke=\"#8a96a6\" stroke-dasharray=\"2 1\" stroke-width=\"0.4\"/>\n";
            for(const auto& placed:l.Placements)
            {
                const auto& part=Find(c.Input,placed.PartID); const auto& shape=part.Geometry;
                const auto index=static_cast<std::size_t>(&part-c.Input.Parts.data());
                out<<"<g transform=\"translate("<<placed.Transform.X<<' '<<placed.Transform.Y<<") rotate("<<placed.Transform.AngleDegrees<<")\">\n";
                out<<"<title>"<<Escape(placed.PartID,true)<<" #"<<placed.Instance<<"; group="<<placed.Group<<"; filler="<<placed.GapFiller<<"; angle="<<placed.Transform.AngleDegrees<<"</title>\n";
                const std::string css=placed.GapFiller?"part filler":"part";
                double cx=0,cy=0;
                if(shape.Radius>0)
                {
                    cx=shape.Center.X; cy=shape.Center.Y;
                    out<<"<circle class=\""<<css<<"\" cx=\""<<cx<<"\" cy=\""<<cy<<"\" r=\""<<shape.Radius<<"\" fill=\""<<colors[index%6]<<"\"/>\n";
                }
                else
                {
                    out<<"<path class=\""<<css<<"\" fill-rule=\"evenodd\" fill=\""<<colors[index%6]<<"\" d=\"";
                    auto path=[&](const std::vector<Point>& ring) {
                        for(std::size_t i=0;i<ring.size();++i)out<<(i==0?'M':'L')<<ring[i].X<<' '<<ring[i].Y<<' ';
                        out<<"Z ";
                    };
                    path(shape.Outer); for(const auto& hole:shape.Holes)path(hole); out<<"\"/>\n";
                    for(const auto& q:shape.Outer) { cx+=q.X; cy+=q.Y; }
                    cx/=shape.Outer.size(); cy/=shape.Outer.size();
                    if(!shape.Holes.empty()) { cx=shape.Outer[0].X+20; cy=shape.Outer[0].Y+20; }
                }
                out<<"<text class=\"label\" transform=\"translate("<<cx<<' '<<cy<<") scale(1 -1)\">"<<Escape(placed.PartID,true)<<'#'<<placed.Instance<<"</text>\n</g>\n";
            }
            out<<"</g>\n"; top+=l.Height+55;
        }
        if(job.Output.Layouts.empty())out<<"<text x=\"25\" y=\"80\" font-size=\"9\">No placements produced.</text>\n";
        out<<"</svg>\n";
    }
    void WriteJob(std::ostream& out,const JobResult& j,const std::string& svg)
    {
        out<<"{\"id\":\""<<Escape(j.ID)<<"\",\"strategy\":\""<<Escape(j.StrategyID)<<"\",\"status\":\""<<State(j.Output.State)<<"\",\"validated\":"<<(j.Checked.Valid?"true":"false")
           <<",\"complete\":"<<(j.Checked.Complete?"true":"false")<<",\"placed\":"<<Count(j.Output)<<",\"sheets\":"<<j.Output.Layouts.size()
           <<",\"materialCost\":"<<j.Checked.MaterialCost<<",\"materialArea\":"<<j.Checked.MaterialArea<<",\"sheetArea\":"<<j.Checked.SheetArea
           <<",\"utilization\":"<<(j.Checked.SheetArea>0?j.Checked.MaterialArea/j.Checked.SheetArea:0)
           <<",\"elapsedSeconds\":"<<j.Output.Stats.ElapsedSeconds<<",\"patterns\":"<<j.Output.Stats.CandidatePatterns
           <<",\"combinationGroups\":"<<j.Output.Stats.CombinationGroups<<",\"circleLatticeGroups\":"<<j.Output.Stats.CircleLatticeGroups
           <<",\"gapFilledParts\":"<<j.Output.Stats.GapFilledParts<<",\"placementAttempts\":"<<j.Output.Stats.PlacementAttempts<<",\"svg\":\""<<Escape(svg)<<"\",\"remaining\":[";
        bool first=true;
        for(const auto& u:j.Output.Remaining) { if(!first)out<<','; first=false; out<<"{\"part\":\""<<Escape(u.PartID)<<"\",\"quantity\":"<<u.Quantity<<'}'; }
        out<<"],\"diagnostics\":["; first=true;
        for(const auto& d:j.Output.Diagnostics) { if(!first)out<<','; first=false; out<<'\"'<<Escape(d)<<'\"'; }
        out<<"],\"validationErrors\":["; first=true;
        for(const auto& e:j.Checked.Errors) { if(!first)out<<','; first=false; out<<'\"'<<Escape(e)<<'\"'; }
        out<<"],\"layouts\":["; first=true;
        for(const auto& l:j.Output.Layouts)
        {
            if(!first)out<<','; first=false;
            out<<"{\"stock\":\""<<Escape(l.StockID)<<"\",\"instance\":"<<l.StockInstance<<",\"width\":"<<l.Width<<",\"height\":"<<l.Height<<",\"placements\":[";
            bool initial=true;
            for(const auto& p:l.Placements)
            {
                if(!initial)out<<','; initial=false;
                out<<"{\"part\":\""<<Escape(p.PartID)<<"\",\"instance\":"<<p.Instance<<",\"x\":"<<p.Transform.X<<",\"y\":"<<p.Transform.Y<<",\"angle\":"<<p.Transform.AngleDegrees<<",\"group\":"<<p.Group<<",\"filler\":"<<(p.GapFiller?"true":"false")<<'}';
            }
            out<<"]}";
        }
        out<<"]}";
    }
    void WriteInput(std::ostream& out,const Problem& p)
    {
        const auto points=[&](const std::vector<Point>& ring) {
            out<<'[';
            for(std::size_t i=0;i<ring.size();++i) { if(i)out<<','; out<<'['<<ring[i].X<<','<<ring[i].Y<<']'; }
            out<<']';
        };
        out<<"{\"partGap\":"<<p.PartGap<<",\"edgeMargin\":"<<p.EdgeMargin<<",\"tolerance\":"<<p.Tolerance<<",\"parts\":[";
        for(std::size_t i=0;i<p.Parts.size();++i)
        {
            if(i)out<<','; const auto& part=p.Parts[i];
            out<<"{\"id\":\""<<Escape(part.ID)<<"\",\"quantity\":"<<part.Quantity<<",\"material\":\""<<Escape(part.Material)<<"\",\"freeRotation\":"<<(part.FreeRotation?"true":"false")<<",\"allowedAngles\":[";
            for(std::size_t j=0;j<part.AllowedAnglesDegrees.size();++j) { if(j)out<<','; out<<part.AllowedAnglesDegrees[j]; }
            out<<"],\"geometry\":{\"radius\":"<<part.Geometry.Radius<<",\"center\":["<<part.Geometry.Center.X<<','<<part.Geometry.Center.Y<<"],\"outer\":";
            points(part.Geometry.Outer); out<<",\"holes\":[";
            for(std::size_t j=0;j<part.Geometry.Holes.size();++j) { if(j)out<<','; points(part.Geometry.Holes[j]); }
            out<<"]}}";
        }
        out<<"],\"stocks\":[";
        for(std::size_t i=0;i<p.Stocks.size();++i)
        {
            if(i)out<<','; const auto& stock=p.Stocks[i];
            out<<"{\"id\":\""<<Escape(stock.ID)<<"\",\"width\":"<<stock.Width<<",\"height\":"<<stock.Height<<",\"quantity\":"<<stock.Quantity<<",\"cost\":"<<stock.Cost<<",\"material\":\""<<Escape(stock.Material)<<"\"}";
        }
        out<<"]}";
    }
}

int main(int argc,char** argv)
{
    try
    {
        const auto directory=std::filesystem::absolute(argc>1?argv[1]:"sheet-nesting-demo");
        std::filesystem::create_directories(directory);
        std::ofstream json(directory/"summary.json"); if(!json)throw std::runtime_error("cannot write summary");
        json<<std::setprecision(12)<<"{\"schema\":\"icax.sheet-nesting.demo/1\",\"cases\":[\n";
        const auto cases=Cases();
        const auto expectedJobs=cases.size()*DefaultStrategies().IDs().size();
        bool first=true; std::size_t validated=0;
        for(const auto& c:cases)
        {
            PortfolioOptions options; options.Workers=3; options.PerJob.TimeLimitSeconds=10; options.PerJob.MaxPlacementAttempts=1000000; options.PerJob.MaxPatterns=512;
            const auto result=RunPortfolio(c.Input,options);
            if(!first)json<<",\n"; first=false;
            json<<"{\"id\":\""<<c.ID<<"\",\"title\":\""<<c.Title<<"\",\"input\":"; WriteInput(json,c.Input); json<<",\"bestJob\":";
            if(result.BestJob<result.Jobs.size())json<<result.BestJob;else json<<"null";
            json<<",\"jobs\":[";
            for(std::size_t i=0;i<result.Jobs.size();++i)
            {
                if(i)json<<',';
                auto name=c.ID+"-"+result.Jobs[i].StrategyID+".svg";
                DrawSvg(directory/name,c,result.Jobs[i]); WriteJob(json,result.Jobs[i],name);
                if(result.Jobs[i].Checked.Valid)++validated;
            }
            json<<"]}";
            if(result.BestJob<result.Jobs.size())
            {
                const auto& best=result.Jobs[result.BestJob]; DrawSvg(directory/(c.ID+".svg"),c,best);
                std::cout<<c.ID<<": "<<best.StrategyID<<", "<<State(best.Output.State)<<", "<<Count(best.Output)<<" parts, "<<best.Output.Layouts.size()<<" sheets, fillers="<<best.Output.Stats.GapFilledParts<<'\n';
            }
            else std::cout<<c.ID<<": NO VALID WINNER\n";
        }
        json<<"\n],\"validatedJobs\":"<<validated<<"}\n";
        std::cout<<"Output: "<<directory.string()<<'\n';
        return expectedJobs>0 && validated==expectedJobs?0:1;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
