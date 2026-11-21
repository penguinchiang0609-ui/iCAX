// Small actual pinned-Rust-kernel smoke controls; quality benchmarks are separate.
#include "SheetNestingSparrow.h"
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Part Box(std::string id,double width,double height,std::size_t quantity=1)
    {Part p;p.ID=std::move(id);p.Material="m";p.Quantity=quantity;p.FreeRotation=false;
        p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return p;}
    void Complete(const Problem& p,const Result& r)
    {
        const auto check=Validate(p,r);
        if(!check.Valid||!check.Complete){for(const auto& d:r.Diagnostics)std::cerr<<d<<'\n';for(const auto& e:check.Errors)std::cerr<<e<<'\n';}
        Require(r.State==Status::Complete&&r.GeometryValidated&&check.Valid&&check.Complete,"actual kernel original-native validation failed");
    }
}
int main()try
{
    Problem p;p.Tolerance=1e-7;p.Parts={Box("left",4,2),Box("right",3,2)};
    p.Stocks.push_back({"stock",10,10,1,1,"m"});
    Options options;options.TimeLimitSeconds=1;options.Seed=17;
    SparrowSettings settings;settings.Threads=1;settings.PolygonSimplificationRatio=-1;settings.NarrowConcavityRatio=-1;
    Complete(p,MakeSparrowFactory(settings)()->Run(p,options));

    Problem rotated;rotated.Tolerance=1e-7;rotated.Parts={Box("only-90",4,2)};
    rotated.Parts[0].AllowedAnglesDegrees={90};rotated.Stocks.push_back({"stock",3,5,1,1,"m"});
    const auto r=MakeSparrowFactory(settings)()->Run(rotated,options);Complete(rotated,r);
    Require(r.Layouts[0].Placements[0].Transform.AngleDegrees==90,"actual kernel lost finite angle domain");

    Problem exactFit;exactFit.Tolerance=1e-7;exactFit.Parts={Box("exact-axis-fit",1,1)};
    exactFit.Stocks.push_back({"stock",2,1,1,1,"m"});
    const auto exact=MakeSparrowFactory(settings)()->Run(exactFit,options);
    if(exact.State==Status::Complete)Complete(exactFit,exact);
    else Require(exact.State==Status::Failed&&exact.Layouts.empty()&&exact.Remaining.size()==1,
        "exact-strip sampling limitation corrupted original demand");

    // The pinned f32 kernel can fail when a rotated bbox fills the fixed
    // strip height exactly. Preserve this real limitation as a safety control:
    // it may return a validated solution or a clean failure with all demand,
    // never an invalid "complete" result or a panic across the host boundary.
    auto tightRotated=rotated;tightRotated.Stocks[0].Width=2;tightRotated.Stocks[0].Height=4;
    const auto tight=MakeSparrowFactory(settings)()->Run(tightRotated,options);
    if(tight.State==Status::Complete)Complete(tightRotated,tight);
    else Require(tight.State==Status::Failed&&tight.Layouts.empty()&&tight.Remaining.size()==1,
        "rotated exact-fit kernel limitation corrupted original demand");

    auto bounded=p;bounded.Stocks[0].Width=1;
    const auto tooWide=MakeSparrowFactory(settings)()->Run(bounded,options);
    Require(tooWide.State==Status::Failed&&tooWide.Layouts.empty()&&tooWide.Remaining.size()==2,
        "actual wider-strip proposal exceeded original one-sheet cap");

    auto cancelled=options;cancelled.Cancel=[]{return true;};
    const auto stopped=MakeSparrowFactory(settings)()->Run(p,cancelled);
    Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==2,"actual-kernel cancellation contract failed");

    Result seed;seed.State=Status::Complete;seed.GeometryValidated=true;
    seed.Layouts.push_back({"stock",0,10,10,{{"left",0,{1,2,0},4,false},{"right",0,{6,2,0},5,true}},14});
    Complete(p,seed);
    const auto refined=MakeSparrowRefineStage(settings).Factory()->Run(p,seed,options);Complete(p,refined);
    bool ranCompress=false;
    for(const auto& message:refined.Diagnostics)
        ranCompress|=message.find("mode=warm_compress")!=std::string::npos||message.find("shared quality key")!=std::string::npos;
    if(!ranCompress)for(const auto& message:refined.Diagnostics)std::cerr<<message<<'\n';
    Require(ranCompress,"representable original seed did not reach actual warm compression");
    Require(refined.Layouts[0].Placements[0].Group==4&&refined.Layouts[0].Placements[1].Group==5&&refined.Layouts[0].Placements[1].GapFiller,
        "actual warm kernel lost original instance provenance");

    auto mirroredProblem=p;mirroredProblem.Parts[0].AllowMirror=true;
    auto mirroredSeed=seed;mirroredSeed.Layouts[0].Placements[0].Transform={1,4,0,true};
    Complete(mirroredProblem,mirroredSeed);
    const auto reflected=MakeSparrowRefineStage(settings).Factory()->Run(mirroredProblem,mirroredSeed,options);
    Complete(mirroredProblem,reflected);
    Require(reflected.Layouts.size()==1&&reflected.Layouts[0].Placements.size()==2&&
        reflected.Layouts[0].Placements[0].Transform.X==1&&reflected.Layouts[0].Placements[0].Transform.Y==4&&
        reflected.Layouts[0].Placements[0].Transform.AngleDegrees==0&&reflected.Layouts[0].Placements[0].Transform.Mirrored&&
        reflected.Layouts[0].Placements[0].PartID=="left"&&reflected.Layouts[0].Placements[0].Group==4&&
        reflected.Layouts[0].Placements[1].PartID=="right"&&reflected.Layouts[0].Placements[1].Group==5&&
        reflected.Layouts[0].Placements[1].GapFiller,
        "unsupported external mirror transport changed original poses, identities or provenance");
    bool reflectedNote=false;for(const auto& message:reflected.Diagnostics)
        reflectedNote|=message.find("does not support mirrored initial poses")!=std::string::npos;
    Require(reflectedNote,"unsupported external mirror transport did not record why the original was retained");

    // The native source is valid but its very large local origin destroys
    // the small edges when imported as f32. No widening or edited source
    // may be used to turn that representation failure into an accepted seed.
    Problem precision;precision.Tolerance=1e-7;precision.Parts={Box("large-origin",.01,.01)};
    for(auto& point:precision.Parts[0].Geometry.Outer){point.X+=1e6;point.Y+=1e6;}
    precision.Stocks.push_back({"stock",10,10,1,1,"m"});
    Result preciseSeed;preciseSeed.State=Status::Complete;preciseSeed.GeometryValidated=true;
    preciseSeed.Layouts.push_back({"stock",0,10,10,{{"large-origin",0,{-1e6+2,-1e6+2,0},13,true}},.0001});
    Complete(precision,preciseSeed);
    const auto retained=MakeSparrowRefineStage(settings).Factory()->Run(precision,preciseSeed,options);
    Require(retained.Layouts[0].Placements[0].Transform.X==preciseSeed.Layouts[0].Placements[0].Transform.X&&
        retained.Layouts[0].Placements[0].Transform.Y==preciseSeed.Layouts[0].Placements[0].Transform.Y&&
        retained.Layouts[0].Placements[0].Group==13&&retained.Layouts[0].Placements[0].GapFiller,
        "unrepresentable warm seed changed original binary64 pose or metadata");
    bool retainedNote=false;for(const auto& message:retained.Diagnostics)retainedNote|=message.find("retained immutable incumbent")!=std::string::npos;
    Require(retainedNote,"actual f32 import failure was not recorded");Complete(precision,retained);
    std::cout<<"PASS Sparrow actual pinned kernel original-native smoke controls\n";return 0;
}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
