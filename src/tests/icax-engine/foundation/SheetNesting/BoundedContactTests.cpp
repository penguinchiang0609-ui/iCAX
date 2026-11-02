#include "SheetNestingBoundedContact.h"
#include "SheetNestingNfp.h"
#include "NestingNfpCombinations.h"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace iCAX::SheetNesting;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part rectangle(const char* id,double w,double h,std::size_t quantity=1)
{
    Part p;p.ID=id;p.Quantity=quantity;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0,90};
    p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;
}
int main() try
{
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"s",10,10,1,1,"m"});p.Parts.push_back(rectangle("R",5,10,2));
    Options options;options.TimeLimitSeconds=2;options.MaxPlacementAttempts=100000;
    auto solver=MakeBoundedContactFactory()();auto result=solver->Run(p,options);auto check=Validate(p,result);
    require(check.Valid&&check.Complete&&check.PlacedCount==2,"simple full order failed");
    require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"attempt budget exceeded");
    auto gap=p;gap.PartGap=1;gap.Stocks[0].Width=11;
    result=solver->Run(gap,options);check=Validate(gap,result);
    require(check.Valid&&check.Complete&&check.PlacedCount==2,"positive gap failed");
    auto partial=p;partial.Parts.clear();partial.Parts.push_back(rectangle("A",6,6,3));
    auto capped=options;capped.TimeLimitSeconds=.4;capped.MaxPlacementAttempts=5000;
    result=solver->Run(partial,capped);check=Validate(partial,result);
    require(check.Valid&&!check.Complete&&check.PlacedCount<=2,"partial result invalid");
    require(result.Stats.PlacementAttempts<=capped.MaxPlacementAttempts,"partial attempt budget exceeded");
    auto shared=std::make_shared<NfpCache>();auto factory=MakeBoundedContactFactory(shared);
    Result first,second;auto execute=[&](Result& dst){auto worker=factory();dst=worker->Run(partial,options);};
    std::thread t1(execute,std::ref(first)),t2(execute,std::ref(second));t1.join();t2.join();
    require(Validate(partial,first).Valid&&Validate(partial,second).Valid,"shared cache concurrent jobs invalid");
    require(first.Stats.PlacementAttempts<=options.MaxPlacementAttempts&&second.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"shared cache budget exceeded");
    const auto st=shared->Statistics();
    if(!st.RegionMisses){std::cerr<<"placed="<<Validate(partial,first).PlacedCount<<" attempts="<<first.Stats.PlacementAttempts<<'\n';for(const auto& msg:first.Diagnostics)std::cerr<<msg<<'\n';}
    require(st.RegionMisses>0,"shared cache was not exercised");
    auto stopped=options;stopped.Cancel=[] {return true;};result=solver->Run(p,stopped);check=Validate(p,result);
    require(check.Valid&&!check.Complete&&result.State==Status::Cancelled,"cancelled result invalid");
    auto zero=options;zero.TimeLimitSeconds=0;result=solver->Run(p,zero);check=Validate(p,result);
    require(check.Valid&&!check.Complete&&check.PlacedCount==0,"zero time result invalid");
    // Directly check that generated multi-member patterns use real geometry,
    // respect inventory and angles, and can be interrupted.
    Problem combined;combined.Tolerance=1e-7;combined.Stocks.push_back({"s",12,12,1,1,"m"});
    combined.Parts.push_back(rectangle("tile",2,2,4));combined.Parts[0].AllowedAnglesDegrees={0};
    NfpCache combCache;std::vector<NfpPreparedShape> prepared{combCache.Prepare(combined.Parts[0].Geometry)};
    Detail::NfpCombinationOptions cfg;cfg.MaxResults=90;cfg.MaxStates=96;cfg.MaxAttempts=12000;
    std::size_t attempts=0;auto patterns=Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts);
    bool triple=false,quad=false;
    for(const auto& pattern:patterns)
    {
        triple|=pattern.Members.size()==3;quad|=pattern.Members.size()==4;
        require(pattern.Members.size()<=4&&pattern.MaterialArea/(pattern.Width*pattern.Height)>=.85-1e-8,"invalid pattern density or inventory");
        for(std::size_t i=0;i<pattern.Members.size();++i)
        {
            require(Detail::AngleAllowed(combined.Parts[0],pattern.Members[i].Transform.AngleDegrees,combined.Tolerance),"forbidden angle");
            for(std::size_t j=0;j<i;++j)require(!Detail::ShapesConflict(combined.Parts[0].Geometry,pattern.Members[i].Transform,combined.Parts[0].Geometry,pattern.Members[j].Transform,0,combined.Tolerance),"overlapping pattern members");
        }
    }
    if(!triple||!quad){std::cerr<<"patterns="<<patterns.size()<<" attempts="<<attempts<<" triple="<<triple<<" quad="<<quad<<"\n";for(const auto& v:patterns)std::cerr<<v.Members.size()<<":"<<v.Width<<"x"<<v.Height<<" ";std::cerr<<"\n";}
    require(triple&&quad,"three and four member patterns missing");
    require(attempts<=cfg.MaxAttempts,"combination budget exceeded");
    Problem sloped;sloped.Tolerance=1e-7;sloped.Stocks.push_back({"s",10382,4900,1,1,"m"});
    Part angled;angled.ID="sloped";angled.Quantity=4;angled.Material="m";angled.FreeRotation=false;
    angled.AllowedAnglesDegrees={0,180};
    angled.Geometry.Outer={{-1091.5,-657},{669.5,-830},{1091.5,-180},{1091.5,180},{669.5,830},{-1091.5,657}};
    sloped.Parts.push_back(angled);NfpCache angledCache;
    std::vector<NfpPreparedShape> angledPrepared{angledCache.Prepare(angled.Geometry)};
    attempts=0;patterns=Detail::BuildNfpCombinationPatterns(sloped,angledCache,angledPrepared,cfg,attempts);
    bool complementary=false,slopedTriple=false,slopedFour=false;
    for(const auto& pattern:patterns)
    {
        const auto fill=pattern.MaterialArea/(pattern.Width*pattern.Height);
        require(pattern.Width<=sloped.Stocks[0].Width+sloped.Tolerance&&pattern.Height<=sloped.Stocks[0].Height+sloped.Tolerance,"sloped assembly exceeds stock");
        for(std::size_t i=0;i<pattern.Members.size();++i)
        {
            require(Detail::AngleAllowed(angled,pattern.Members[i].Transform.AngleDegrees,sloped.Tolerance),"sloped forbidden angle");
            for(std::size_t j=0;j<i;++j)require(!Detail::ShapesConflict(angled.Geometry,pattern.Members[i].Transform,angled.Geometry,pattern.Members[j].Transform,0,sloped.Tolerance),"sloped assembly overlaps");
        }
        if(pattern.Members.size()==2&&fill>.87&&
            pattern.Members[0].Transform.AngleDegrees!=pattern.Members[1].Transform.AngleDegrees&&
            !Detail::ShapesConflict(angled.Geometry,pattern.Members[0].Transform,angled.Geometry,pattern.Members[1].Transform,0,sloped.Tolerance))complementary=true;
        if(pattern.Members.size()==3&&fill>.887)slopedTriple=true;
        if(pattern.Members.size()==4&&fill>.87)slopedFour=true;
    }
    require(complementary,"sloped edge complementary pair missing");
    require(slopedTriple,"dense sloped triple missing");
    require(slopedFour,"dense sloped four member assembly missing");
    combined.Parts[0].Quantity=1;attempts=0;patterns=Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts);
    require(patterns.empty(),"insufficient inventory generated pattern");
    combined.Parts[0].Quantity=4;cfg.Stop=[] {return true;};attempts=0;
    require(Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts).empty()&&attempts==0,"combination cancellation ignored");
    cfg.Stop={};cfg.MaxAttempts=0;attempts=0;
    require(Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts).empty()&&attempts==0,"zero combination budget ignored");
    std::cout<<"bounded contact controls passed\n";
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
