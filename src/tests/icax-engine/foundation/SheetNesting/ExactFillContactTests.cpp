#include "SheetNestingExactFill.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
Part part(const char* id,std::vector<Point> v,double angle=0,std::size_t n=1)
{Part p;p.ID=id;p.Material="m";p.Quantity=n;p.FreeRotation=false;p.AllowedAnglesDegrees={angle};p.Geometry.Outer=std::move(v);return p;}
int main(int argc,char** argv) try
{
    Problem p;p.Stocks.push_back({"s",10,10,1,1,"m"});p.Tolerance=1e-7;
    p.Parts.push_back(part("L",{{0,0},{6,0},{6,4},{4,4},{4,10},{0,10}}));
    p.Parts.push_back(part("T1",{{6,0},{10,0},{6,4}}));
    p.Parts.push_back(part("T2",{{10,0},{10,4},{6,4}}));
    p.Parts.push_back(part("Z1",{{4,4},{7,4},{8,10},{4,10}}));
    p.Parts.push_back(part("Z2",{{-7,-4},{-10,-4},{-10,-10},{-8,-10}},180));
    Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=100000;
    auto solve=MakeExactFillContactFactory()();auto r=solve->Run(p,o);auto v=Validate(p,r);
    std::cout<<"mixed "<<v.Valid<<" "<<v.Complete<<" "<<v.PlacedCount<<" attempts "<<r.Stats.PlacementAttempts<<"\n";
    require(v.Valid&&v.Complete&&v.PlacedCount==5,"mixed exact fill failed");
    if(argc==2)
    {
        std::ofstream out(argv[1]);out<<std::setprecision(17)<<"{\"problem\":{\"partGap\":0,\"edgeMargin\":0,\"tolerance\":1e-7,\"stocks\":[{\"id\":\"s\",\"width\":10,\"height\":10,\"quantity\":1,\"cost\":1,\"material\":\"m\"}],\"parts\":[";
        for(std::size_t i=0;i<p.Parts.size();++i)
        {
            if(i)out<<',';const auto& item=p.Parts[i];
            out<<"{\"id\":\""<<item.ID<<"\",\"quantity\":"<<item.Quantity<<",\"material\":\"m\",\"freeRotation\":false,\"allowedAngles\":["<<item.AllowedAnglesDegrees[0]<<"],\"geometry\":{\"radius\":0,\"center\":[0,0],\"holes\":[],\"outer\":[";
            for(std::size_t j=0;j<item.Geometry.Outer.size();++j){if(j)out<<',';out<<'['<<item.Geometry.Outer[j].X<<','<<item.Geometry.Outer[j].Y<<']';}out<<"]}}";
        }
        out<<"]},\"result\":{\"layouts\":[{\"stock\":\"s\",\"instance\":0,\"width\":10,\"height\":10,\"placements\":[";
        for(std::size_t i=0;i<r.Layouts[0].Placements.size();++i)
        {if(i)out<<',';const auto& q=r.Layouts[0].Placements[i];out<<"{\"part\":\""<<q.PartID<<"\",\"instance\":"<<q.Instance<<",\"x\":"<<q.Transform.X<<",\"y\":"<<q.Transform.Y<<",\"angle\":"<<q.Transform.AngleDegrees<<'}';}
        out<<"]}],\"remaining\":[]}}";require(static_cast<bool>(out),"control result write failed");
    }
    auto partial=p;partial.Stocks[0].Width=11;r=solve->Run(partial,o);v=Validate(partial,r);
    require(v.Valid&&!v.Complete&&v.PlacedCount==0,"nonfill must remain partial");
    auto gap=p;gap.PartGap=1;r=solve->Run(gap,o);v=Validate(gap,r);
    require(v.Valid&&!v.Complete&&v.PlacedCount==0,"gap must remain partial");
    auto material=p;material.Parts[0].Material="other";r=solve->Run(material,o);v=Validate(material,r);
    require(v.Valid&&!v.Complete&&v.PlacedCount==0,"different material must remain partial");
    Options cancel=o;cancel.Cancel=[] {return true;};r=solve->Run(p,cancel);v=Validate(p,r);
    require(r.State==Status::Cancelled&&v.Valid&&!v.Complete,"cancel should preserve demand");
    int polls=0;Options midCancel=o;midCancel.Cancel=[&] {return ++polls>=10;};
    r=solve->Run(p,midCancel);v=Validate(p,r);
    require(r.State==Status::Cancelled&&v.Valid&&!v.Complete,"mid-search cancel must preserve demand");
    Options budget=o;budget.MaxPlacementAttempts=1;r=solve->Run(p,budget);v=Validate(p,r);
    require(v.Valid&&!v.Complete&&r.Stats.PlacementAttempts<=1,"budget must preserve demand");
    Options time=o;time.TimeLimitSeconds=0;r=solve->Run(p,time);v=Validate(p,r);
    require(v.Valid&&!v.Complete&&v.PlacedCount==0,"elapsed budget must preserve demand");
    auto reordered=p;std::reverse(reordered.Parts.begin(),reordered.Parts.end());
    r=solve->Run(reordered,o);v=Validate(reordered,r);
    require(v.Valid&&v.Complete&&v.PlacedCount==5,"permuted input failed");
    auto rotated=p;
    for(auto& item:rotated.Parts)for(auto& point:item.Geometry.Outer)
    {const auto x=point.X;point.X=-point.Y;point.Y=x;}
    r=solve->Run(rotated,o);v=Validate(rotated,r);
    require(v.Valid&&v.Complete&&v.PlacedCount==5,"rotated geometry failed");
    auto coarse=p;coarse.Tolerance=.01;r=solve->Run(coarse,o);v=Validate(coarse,r);
    require(v.Valid&&v.Complete&&v.PlacedCount==5,"valid coarse tolerance failed");
    auto near=p;near.Tolerance=.01;near.Parts[0].Geometry.Outer[1].X-=.0001;
    r=solve->Run(near,o);v=Validate(near,r);
    require(v.Valid&&!v.Complete&&v.PlacedCount==0,"tolerance must not falsely treat area as full");
    Problem impossible;impossible.Tolerance=1e-7;impossible.Stocks=p.Stocks;
    impossible.Parts.push_back(part("too-wide",{{0,0},{12,0},{12,5},{0,5}}));
    impossible.Parts.push_back(part("remainder",{{0,0},{4,0},{4,10},{0,10}}));
    r=solve->Run(impossible,o);v=Validate(impossible,r);
    require(v.Valid&&!v.Complete&&v.PlacedCount==0,"failed search must preserve demand");
    Problem repeats;repeats.Tolerance=1e-7;repeats.Stocks=p.Stocks;
    repeats.Parts.push_back(part("R",{{0,0},{5,0},{5,10},{0,10}},0,2));
    r=solve->Run(repeats,o);v=Validate(repeats,r);require(v.Valid&&v.Complete&&v.PlacedCount==2,"repeated instances failed");
    auto identities=repeats;identities.Parts[0].Quantity=1;
    identities.Parts.push_back(identities.Parts[0]);identities.Parts[1].ID="R2";
    r=solve->Run(identities,o);v=Validate(identities,r);
    require(v.Valid&&v.Complete&&v.PlacedCount==2,"identical geometry with distinct IDs failed");
    std::cout<<"controls passed\n";
}
catch(const std::exception& e){std::cerr<<"control failed: "<<e.what()<<"\n";return 1;}
