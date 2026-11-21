#include "SheetNestingPipeline.h"
#include "NestingExecution.h"
#include "NestingAxisSafety.h"
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace{void Require(bool v,const char* s){if(!v)throw std::runtime_error(s);}}
int main()try
{
    Problem p;p.Stocks={{"stock",6,6,2,1,"m"}};Part l;l.ID="L";l.Material="m";l.FreeRotation=false;l.AllowedAnglesDegrees={0};l.Geometry.Outer={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
    Part tile;tile.ID="tile";tile.Material="m";tile.FreeRotation=false;tile.AllowedAnglesDegrees={0};tile.Geometry.Outer={{0,0},{4,0},{4,4},{0,4}};p.Parts={l,tile};
    JobResult before;before.Output.State=Status::Complete;before.Output.Layouts={{"stock",0,6,6,{{"L",0,{0,0,0},91,false}},20},{"stock",1,6,6,{{"tile",0,{0,0,0},92,false}},16}};Detail::AssessCandidate(p,before);Require(before.Checked.Valid&&before.Checked.Complete,"invalid original controls");
    Options o;o.TimeLimitSeconds=2;o.MaxPlacementAttempts=20000;const auto stage=MakeNfpRefillStage();auto result=stage.Factory()->Run(p,before.Output,o);const auto valid=Validate(p,result);
    Require(valid.Valid&&valid.Complete&&valid.PlacedCount==2&&result.Layouts.size()==1,"actual concave cavity must absorb the last-board tile");
    bool moved=false;for(const auto& x:result.Layouts[0].Placements)if(x.PartID=="tile"){moved=true;Require(x.Instance==0&&x.Transform.AngleDegrees==0&&x.Transform.X==2&&x.Transform.Y==2,"original part must retain identity and use a legal exact contact pose");}
    Require(moved&&before.Output.Layouts.size()==2&&before.Output.Layouts[1].Placements[0].Transform.X==0,"stage mutated its incumbent");
    std::cout<<"PASS real concave space absorbs a tail part despite its enclosing rectangle being full\n";
    auto cancelled=o;cancelled.Cancel=[]{return true;};result=stage.Factory()->Run(p,before.Output,cancelled);Require(result.State==Status::Cancelled&&Validate(p,result).Valid&&result.Layouts.size()==2&&result.Stats.PlacementAttempts==0,"cancellation must retain complete originals");
    auto bounded=o;bounded.MaxPlacementAttempts=1;result=stage.Factory()->Run(p,before.Output,bounded);Require(Validate(p,result).Valid&&Validate(p,result).Complete&&result.Stats.PlacementAttempts<=1,"bounded search lost originals or overran quota");
    auto spaced=p;spaced.PartGap=.1;result=stage.Factory()->Run(spaced,before.Output,o);
    Require(result.Layouts.size()==2&&Validate(spaced,result).Complete,"positive gap must keep the original boards when the exact cavity has no clearance");
    auto spacedFit=spaced;spacedFit.PartGap=.2;spacedFit.Parts[1].Geometry.Outer={{0,0},{3.5,0},{3.5,3.5},{0,3.5}};
    auto spacedSource=before;spacedSource.Output.Layouts[1].MaterialArea=12.25;Detail::AssessCandidate(spacedFit,spacedSource);
    Require(spacedSource.Checked.Valid&&spacedSource.Checked.Complete,"positive-gap controls must contain the complete original set");
    result=stage.Factory()->Run(spacedFit,spacedSource.Output,o);
    Require(Validate(spacedFit,result).Complete&&result.Layouts.size()==1,"positive-gap refill must eliminate a tail board using real concave free material");
    const auto& fixed=result.Layouts[0].Placements[0];const auto& added=result.Layouts[0].Placements[1];
    Require(AxisSafety::Pair(AxisSafety::Make(spacedFit.Parts[0].Geometry,fixed.Transform),AxisSafety::Make(spacedFit.Parts[1].Geometry,added.Transform),.2,false,spacedFit.Tolerance)==AxisSafety::Verdict::Safe,"forward refill must preserve the nominal positive distance");
    Require(spacedSource.Output.Layouts.size()==2&&added.PartID=="tile"&&added.Instance==0,"refill must preserve original identities and leave its input unchanged");
    std::cout<<"PASS cancellation, identities, finite budgets and positive-clearance forward refill\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
