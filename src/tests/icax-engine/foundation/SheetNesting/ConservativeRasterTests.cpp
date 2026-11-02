#include "SheetNestingRaster.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    Shape Box(double w,double h){Shape s;s.Outer={{0,0},{w,0},{w,h},{0,h}};return s;}
    Part PartOf(const char* id,Shape s,std::size_t quantity)
    {Part p;p.ID=id;p.Geometry=std::move(s);p.Quantity=quantity;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};return p;}
    JobResult Solve(const Problem& p,RasterStrategySettings settings,Options o={})
    {StrategyRegistry r;r.Register("test.raster",MakeConservativeRasterFactory(std::move(settings)));return RunJob({"raster","test.raster",p,o},r);}
}
int main()
{
    try
    {
        Problem p;p.Parts={PartOf("A",Box(2,2),4)};p.Stocks={{"sheet",4,4,1,1,"m"}};
        RasterStrategySettings s;s.Step=1;s.Priority={0};Options o;o.TimeLimitSeconds=5;
        const auto tiled=Solve(p,s,o);Require(tiled.Checked.Valid&&tiled.Checked.Complete&&tiled.Output.Layouts[0].Placements.size()==4,"aligned edge-sharing tiling failed");
        const auto defaults=DefaultStrategies().IDs();Require(std::find(defaults.begin(),defaults.end(),"test.raster")==defaults.end(),"raster must remain opt-in");
        Shape tri;tri.Outer={{.13,.17},{2.27,.17},{.13,2.28}};
        Problem off;off.Parts={PartOf("offgrid",tri,3),PartOf("box",Box(1,1),2)};off.Stocks={{"s",12,12,1,1,"m"}};
        s.Priority={1,0};s.Interleave=true;const auto offgrid=Solve(off,s,o);
        Require(offgrid.Checked.Valid&&offgrid.Checked.Complete,"non-grid geometry must stay usable and valid");
        Require(offgrid.Output.Layouts[0].Placements.front().PartID=="box","explicit priority not honored");
        auto invalid=s;invalid.Priority={1,1};const auto wrong=Solve(off,invalid,o);
        Require(wrong.Output.State==Status::Failed&&wrong.Output.Remaining.size()==2,"invalid priority was not rejected");
        Problem boundary;boundary.Parts={PartOf("small",Box(1,1),130)};boundary.Stocks={{"wide",130,1,1,1,"m"}};
        s.Priority={0};s.Interleave=false;const auto wide=Solve(boundary,s,o);
        Require(wide.Checked.Valid&&wide.Checked.Complete,"bit masks across 64-bit words failed");
        auto limited=o;limited.MaxPlacementAttempts=1;const auto part=Solve(p,s,limited);
        Require(part.Checked.Valid&&!part.Checked.Complete&&part.Output.Stats.PlacementAttempts<=1,"attempt budget lost demand");
        auto cancel=o;cancel.Cancel=[]{return true;};const auto stopped=Solve(p,s,cancel);
        Require(stopped.Output.State==Status::Cancelled&&stopped.Output.Remaining[0].Quantity==4,"cancellation lost original demand");
        auto gap=p;gap.PartGap=1;const auto unsupported=Solve(gap,s,o);
        Require(unsupported.Checked.Valid&&!unsupported.Checked.Complete&&unsupported.Output.Layouts.empty(),"unsupported gap must preserve all demand");
        auto rotation=p;rotation.Parts[0].AllowedAnglesDegrees={0,90};const auto angle=Solve(rotation,s,o);
        Require(angle.Checked.Valid&&angle.Checked.Complete,"extra permitted directions must not suppress legal zero-degree packing");
        auto freeRotation=p;freeRotation.Parts[0].FreeRotation=true;const auto free=Solve(freeRotation,s,o);
        Require(free.Checked.Valid&&free.Checked.Complete,"free rotation must permit the zero-degree candidate");
        auto noZero=p;noZero.Parts[0].AllowedAnglesDegrees={90};const auto noZeroResult=Solve(noZero,s,o);
        Require(noZeroResult.Checked.Valid&&!noZeroResult.Checked.Complete&&noZeroResult.Output.Layouts.empty(),"unpermitted zero direction must preserve all demand");
        Problem stock;stock.Parts={PartOf("plate",Box(100,100),1)};stock.Stocks={{"large",2240,6000,1,1,"m"}};
        auto coarse=s;coarse.Step=5;const auto large=Solve(stock,coarse,o);
        Require(large.Checked.Valid&&large.Checked.Complete,"supported large physical stock was unexpectedly rejected");
        auto fine=coarse;fine.Step=1;const auto over=Solve(stock,fine,o);
        Require(over.Checked.Valid&&!over.Checked.Complete&&over.Output.Layouts.empty(),"oversize grid must preserve demand");
        auto empty=p;empty.Stocks[0].Quantity=0;const auto unavailable=Solve(empty,s,o);
        Require(unavailable.Checked.Valid&&!unavailable.Checked.Complete&&unavailable.Output.Layouts.empty()&&
            unavailable.Output.Remaining.size()==1&&unavailable.Output.Remaining[0].Quantity==4,"zero inventory fabricated a sheet");
        std::cout<<"ConservativeRasterTests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
