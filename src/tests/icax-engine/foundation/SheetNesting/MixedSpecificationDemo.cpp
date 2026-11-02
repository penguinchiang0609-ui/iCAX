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
    struct Example { std::string ID,Title; Problem Input; };
    Shape Box(double w,double h) { Shape s; s.Outer={{0,0},{w,0},{w,h},{0,h}}; return s; }
    Part P(std::string id,Shape shape,std::size_t quantity)
    { Part p; p.ID=std::move(id); p.Geometry=std::move(shape); p.Quantity=quantity; p.FreeRotation=false; p.AllowedAnglesDegrees={0}; return p; }
    Stock S(double w,double h) { Stock s; s.ID="stock"; s.Width=w; s.Height=h; return s; }
    std::string Escape(const std::string& s,bool xml=false)
    {
        std::string r;
        for(unsigned char c:s)
        {
            if(xml)
            {
                if(c=='&')r+="&amp;";else if(c=='<')r+="&lt;";else if(c=='>')r+="&gt;";else if(c=='\"')r+="&quot;";else r+=static_cast<char>(c);
            }
            else
            {
                if(c=='\\')r+="\\\\";else if(c=='\"')r+="\\\"";else if(c=='\n')r+="\\n";else if(c=='\r')r+="\\r";
                else if(c=='\t')r+="\\t";else if(c<32) { std::ostringstream e; e<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<int>(c);r+=e.str(); }
                else r+=static_cast<char>(c);
            }
        }
        return r;
    }
    const char* State(Status s)
    {
        switch(s) { case Status::Complete:return "Complete";case Status::Partial:return "Partial";case Status::InvalidInput:return "InvalidInput";case Status::Cancelled:return "Cancelled";default:return "Failed"; }
    }
    std::vector<Example> Examples()
    {
        std::vector<Example> cases;
        Example c; c.ID="mixed-rectangles";c.Title="Different rectangle dimensions share one sheet";
        c.Input.Parts={P("R60x40",Box(60,40),2),P("R40x40",Box(40,40),2)};c.Input.Stocks={S(100,80)};cases.push_back(c);
        c={};c.ID="mixed-parallelograms";c.Title="Different parallelogram side lengths share one sheet";
        Shape a;a.Outer={{0,0},{80,0},{100,40},{20,40}};Shape b;b.Outer={{0,0},{60,0},{80,40},{20,40}};
        c.Input.Parts={P("P80",a,2),P("P60",b,2)};c.Input.Stocks={S(180,80)};cases.push_back(c);
        c={};c.ID="mixed-circles";c.Title="Four R20 circles and one R8 circle in the central space";
        a={};a.Radius=20;b={};b.Radius=8;
        c.Input.Parts={P("R20",a,4),P("R8",b,1)};c.Input.Stocks={S(80,80)};cases.push_back(c);
        return cases;
    }
    const Part& Find(const Problem& p,const std::string& id)
    { for(const auto& part:p.Parts)if(part.ID==id)return part;throw std::runtime_error("unknown part"); }
    void Points(std::ostream& out,const std::vector<Point>& points)
    { out<<'[';for(std::size_t i=0;i<points.size();++i) { if(i)out<<',';out<<'['<<points[i].X<<','<<points[i].Y<<']'; }out<<']'; }
    void InputJson(std::ostream& out,const Problem& p)
    {
        out<<"{\"partGap\":"<<p.PartGap<<",\"edgeMargin\":"<<p.EdgeMargin<<",\"parts\":[";
        for(std::size_t i=0;i<p.Parts.size();++i)
        {
            if(i)out<<',';const auto& part=p.Parts[i];
            out<<"{\"id\":\""<<Escape(part.ID)<<"\",\"quantity\":"<<part.Quantity<<",\"freeRotation\":false,\"allowedAngles\":[0],\"geometry\":{\"radius\":"<<part.Geometry.Radius<<",\"center\":["<<part.Geometry.Center.X<<','<<part.Geometry.Center.Y<<"],\"outer\":";
            Points(out,part.Geometry.Outer);out<<",\"holes\":[";
            for(std::size_t j=0;j<part.Geometry.Holes.size();++j) { if(j)out<<',';Points(out,part.Geometry.Holes[j]); }out<<"]}}";
        }
        out<<"],\"stocks\":[";
        for(std::size_t i=0;i<p.Stocks.size();++i) { if(i)out<<',';const auto& s=p.Stocks[i];out<<"{\"id\":\""<<Escape(s.ID)<<"\",\"width\":"<<s.Width<<",\"height\":"<<s.Height<<",\"quantity\":"<<s.Quantity<<'}'; }out<<"]}";
    }
    void JobJson(std::ostream& out,const JobResult& j,const std::string& svg)
    {
        out<<"{\"strategy\":\""<<Escape(j.StrategyID)<<"\",\"status\":\""<<State(j.Output.State)<<"\",\"validated\":"<<(j.Checked.Valid?"true":"false")
            <<",\"complete\":"<<(j.Checked.Complete?"true":"false")<<",\"placed\":"<<j.Checked.PlacedCount<<",\"sheets\":"<<j.Output.Layouts.size()
            <<",\"sheetArea\":"<<j.Checked.SheetArea<<",\"materialArea\":"<<j.Checked.MaterialArea<<",\"utilization\":"<<(j.Checked.SheetArea>0?j.Checked.MaterialArea/j.Checked.SheetArea:0)
            <<",\"gapFilledParts\":"<<j.Output.Stats.GapFilledParts<<",\"elapsedSeconds\":"<<j.Output.Stats.ElapsedSeconds<<",\"svg\":\""<<Escape(svg)<<"\",\"remaining\":[";
        bool first=true;
        for(const auto& u:j.Output.Remaining) { if(!first)out<<',';first=false;out<<"{\"part\":\""<<Escape(u.PartID)<<"\",\"quantity\":"<<u.Quantity<<'}'; }
        out<<"],\"diagnostics\":[";first=true;for(const auto& d:j.Output.Diagnostics) { if(!first)out<<',';first=false;out<<'\"'<<Escape(d)<<'\"'; }
        out<<"],\"validationErrors\":[";first=true;for(const auto& d:j.Checked.Errors) { if(!first)out<<',';first=false;out<<'\"'<<Escape(d)<<'\"'; }
        out<<"],\"layouts\":[";first=true;
        for(const auto& l:j.Output.Layouts)
        {
            if(!first)out<<',';first=false;out<<"{\"stock\":\""<<Escape(l.StockID)<<"\",\"instance\":"<<l.StockInstance<<",\"placements\":[";
            bool start=true;
            for(const auto& q:l.Placements)
            {
                if(!start)out<<',';start=false;
                out<<"{\"part\":\""<<Escape(q.PartID)<<"\",\"instance\":"<<q.Instance<<",\"x\":"<<q.Transform.X<<",\"y\":"<<q.Transform.Y<<",\"angle\":"<<q.Transform.AngleDegrees<<",\"group\":"<<q.Group<<",\"filler\":"<<(q.GapFiller?"true":"false")<<'}';
            }
            out<<"]}";
        }
        out<<"]}";
    }
    void Svg(const std::filesystem::path& file,const Example& c,const JobResult& j)
    {
        double width=320,height=90;for(const auto& l:j.Output.Layouts) { width=std::max(width,l.Width+40);height+=l.Height+35; }
        std::ofstream out(file);if(!out)throw std::runtime_error("cannot write SVG");out<<std::setprecision(12);
        out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" viewBox=\"0 0 "<<width<<' '<<height<<"\"><rect width=\"100%\" height=\"100%\" fill=\"#f4f7fb\"/>\n";
        out<<"<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#172b46}.part{stroke:#263a51;stroke-width:.4}.label{text-anchor:middle;font-size:4px}</style>\n";
        out<<"<text x=\"14\" y=\"16\" font-size=\"9\" font-weight=\"600\">"<<Escape(c.Title,true)<<"</text>\n";
        out<<"<text x=\"14\" y=\"29\" font-size=\"6.5\">"<<Escape(j.StrategyID,true)<<" | "<<State(j.Output.State)<<" | placed "<<j.Checked.PlacedCount<<" | geometry "<<(j.Checked.Valid?"VALID":"INVALID")<<"</text>\n";
        out<<"<text x=\"14\" y=\"40\" font-size=\"6\">All jobs use the same input. Optional combinations and gap filling are disabled.</text>\n";
        static const char* colors[]={"#9fc8f0","#f3b978","#aad9ba","#c5b3e9"};double top=65;
        for(const auto& l:j.Output.Layouts)
        {
            out<<"<text x=\"18\" y=\""<<top-6<<"\" font-size=\"6\">"<<l.Width<<" x "<<l.Height<<" mm | sheet #"<<l.StockInstance<<"</text>\n";
            out<<"<g transform=\"translate(18 "<<top+l.Height<<") scale(1 -1)\"><rect width=\""<<l.Width<<"\" height=\""<<l.Height<<"\" fill=\"#fffdf5\" stroke=\"#31465b\" stroke-width=\".6\"/>\n";
            for(const auto& q:l.Placements)
            {
                const auto& part=Find(c.Input,q.PartID);const auto& shape=part.Geometry;const auto index=static_cast<std::size_t>(&part-c.Input.Parts.data());
                out<<"<g transform=\"translate("<<q.Transform.X<<' '<<q.Transform.Y<<") rotate("<<q.Transform.AngleDegrees<<")\"><title>"<<Escape(q.PartID,true)<<'#'<<q.Instance<<"; angle="<<q.Transform.AngleDegrees<<"; filler="<<q.GapFiller<<"</title>\n";
                double cx=0,cy=0;
                if(shape.Radius>0) { cx=shape.Center.X;cy=shape.Center.Y;out<<"<circle class=\"part\" cx=\""<<cx<<"\" cy=\""<<cy<<"\" r=\""<<shape.Radius<<"\" fill=\""<<colors[index%4]<<"\"/>\n"; }
                else
                {
                    out<<"<path class=\"part\" fill-rule=\"evenodd\" fill=\""<<colors[index%4]<<"\" d=\"";
                    auto ring=[&](const auto& points) { for(std::size_t i=0;i<points.size();++i)out<<(i?'L':'M')<<points[i].X<<' '<<points[i].Y<<' ';out<<"Z "; };
                    ring(shape.Outer);for(const auto& hole:shape.Holes)ring(hole);out<<"\"/>\n";
                    for(const auto& point:shape.Outer) { cx+=point.X;cy+=point.Y; }cx/=shape.Outer.size();cy/=shape.Outer.size();
                }
                out<<"</g>\n";
                const auto angle=q.Transform.AngleDegrees*3.14159265358979323846/180;
                const double x=q.Transform.X+cx*std::cos(angle)-cy*std::sin(angle),y=q.Transform.Y+cx*std::sin(angle)+cy*std::cos(angle);
                out<<"<text class=\"label\" transform=\"translate("<<x<<' '<<y<<") scale(1 -1)\">"<<Escape(q.PartID,true)<<'#'<<q.Instance<<"</text>\n";
            }
            out<<"</g>\n";top+=l.Height+35;
        }
        out<<"</svg>\n";
    }
}
int main(int argc,char** argv)
{
    try
    {
        const auto directory=std::filesystem::absolute(argc>1?argv[1]:"mixed-specification-demo");std::filesystem::create_directories(directory);
        std::ofstream out(directory/"summary.json");if(!out)throw std::runtime_error("cannot write summary");out<<std::setprecision(12);
        const auto examples=Examples();const auto expected=examples.size()*DefaultStrategies().IDs().size();std::size_t validated=0,completed=0;
        out<<"{\"schema\":\"icax.sheet-nesting.mixed-demo/1\",\"optionalCombinations\":false,\"optionalGapFill\":false,\"cases\":[";
        for(std::size_t ci=0;ci<examples.size();++ci)
        {
            const auto& c=examples[ci];PortfolioOptions options;options.Workers=3;options.PerJob.TimeLimitSeconds=30;options.PerJob.MaxPlacementAttempts=1000000;options.PerJob.MaxPatterns=512;options.PerJob.EnableCombinations=false;options.PerJob.EnableGapFill=false;
            const auto result=RunPortfolio(c.Input,options);if(ci)out<<',';
            out<<"{\"id\":\""<<c.ID<<"\",\"input\":";InputJson(out,c.Input);out<<",\"bestJob\":";
            if(result.BestJob<result.Jobs.size())out<<result.BestJob;else out<<"null";out<<",\"jobs\":[";
            for(std::size_t ji=0;ji<result.Jobs.size();++ji)
            {
                if(ji)out<<',';const auto& job=result.Jobs[ji];const auto filename=c.ID+"-"+job.StrategyID+".svg";Svg(directory/filename,c,job);JobJson(out,job,filename);if(job.Checked.Valid)++validated;
            }
            out<<"]}";
            if(result.BestJob<result.Jobs.size())
            {
                const auto& best=result.Jobs[result.BestJob];Svg(directory/(c.ID+".svg"),c,best);if(best.Checked.Complete)++completed;
                std::cout<<c.ID<<": "<<best.StrategyID<<", "<<State(best.Output.State)<<", "<<best.Checked.PlacedCount<<" parts, "<<best.Output.Layouts.size()<<" sheets\n";
            }
        }
        out<<"],\"validatedJobs\":"<<validated<<",\"expectedJobs\":"<<expected<<",\"completeCases\":"<<completed<<"}\n";
        std::cout<<"Output: "<<directory.string()<<'\n';return expected>0&&validated==expected&&completed==examples.size()?0:1;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
