#include "NestingInternal.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail { std::vector<Pattern> BuildClosurePatterns(const Problem&, const Options&, const std::function<bool()>&); }
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Problem FailedJigsaw15(){Problem p;p.Tolerance=1e-6;p.Stocks={{"S0",600,600,1,1,""}};
{ Part q; q.ID="jigsaw-1"; q.FreeRotation=false; q.AllowedAnglesDegrees={0}; q.Geometry.Outer={{-14,260},{36,260},{86,260},{136,260},{186,260},{236,260},{286,260},{286,310},{236,310},{236,360},{186,360},{186,410},{236,410},{286,410},{336,410},{386,410},{386,460},{336,460},{336,510},{286,510},{286,560},{236,560},{236,610},{186,610},{136,610},{86,610},{36,610},{-14,610},{-14,560},{-14,510},{-14,460},{-14,410},{-14,360},{-14,310}}; p.Parts.push_back(q); }
{ Part q; q.ID="jigsaw-2"; q.FreeRotation=false; q.AllowedAnglesDegrees={0}; q.Geometry.Outer={{200,100},{250,100},{250,50},{300,50},{300,0},{350,0},{400,0},{450,0},{500,0},{550,0},{600,0},{600,50},{600,100},{600,150},{600,200},{600,250},{600,300},{550,300},{550,350},{500,350},{500,300},{500,250},{450,250},{450,300},{400,300},{400,350},{350,350},{350,300},{350,250},{350,200},{400,200},{400,150},{350,150},{300,150},{250,150},{200,150}}; p.Parts.push_back(q); }
{ Part q; q.ID="jigsaw-3"; q.FreeRotation=false; q.AllowedAnglesDegrees={0}; q.Geometry.Outer={{200,350},{250,350},{250,300},{300,300},{300,250},{350,250},{350,300},{350,350},{400,350},{400,300},{450,300},{450,250},{500,250},{500,300},{500,350},{550,350},{550,300},{600,300},{600,350},{600,400},{600,450},{600,500},{600,550},{600,600},{550,600},{500,600},{450,600},{400,600},{350,600},{300,600},{250,600},{250,550},{300,550},{300,500},{350,500},{350,450},{400,450},{400,400},{350,400},{300,400},{250,400},{200,400}}; p.Parts.push_back(q); }
{ Part q; q.ID="jigsaw-0"; q.FreeRotation=false; q.AllowedAnglesDegrees={0}; q.Geometry.Outer={{0,0},{50,0},{100,0},{150,0},{200,0},{250,0},{300,0},{300,50},{250,50},{250,100},{200,100},{200,150},{250,150},{300,150},{350,150},{400,150},{400,200},{350,200},{350,250},{300,250},{250,250},{200,250},{150,250},{100,250},{50,250},{0,250},{0,200},{0,150},{0,100},{0,50}}; p.Parts.push_back(q); }
return p;}
void VerifyPatterns(const Problem& original,const std::vector<Detail::Pattern>& patterns,std::size_t maxMembers=4){
 for(const auto& pattern:patterns){Require(!pattern.Members.empty()&&pattern.Members.size()<=maxMembers,"pattern member cap");
  std::map<std::size_t,std::size_t> need;for(auto&m:pattern.Members){Require(m.PartIndex<original.Parts.size(),"original member index");++need[m.PartIndex];Require(Detail::AngleAllowed(original.Parts[m.PartIndex],m.Transform.AngleDegrees,original.Tolerance),"original angle");}
  Problem p=original;p.Parts.clear();p.EdgeMargin=0;p.Stocks={{"pattern",pattern.Width,pattern.Height,1,1,original.Parts[pattern.Members[0].PartIndex].Material}};
  for(auto [index,quantity]:need){auto q=original.Parts[index];Require(quantity<=q.Quantity,"inventory");q.Quantity=quantity;p.Parts.push_back(q);}
  Result result;result.State=Status::Complete;Layout layout;layout.StockID="pattern";layout.Width=pattern.Width;layout.Height=pattern.Height;std::map<std::size_t,std::size_t> seen;
  for(auto&m:pattern.Members)layout.Placements.push_back({original.Parts[m.PartIndex].ID,seen[m.PartIndex]++,m.Transform});
  result.Layouts.push_back(layout);auto check=Validate(p,result);Require(check.Valid&&check.Complete,"original geometry/inventory validation");
 }
}
bool HasClosedRectangle(const std::vector<Detail::Pattern>& patterns,std::size_t members){return std::any_of(patterns.begin(),patterns.end(),[&](const auto&p){return p.Members.size()==members&&p.Kind==ShapeKind::Rectangle;});}
}
int main(){try{
 Options options;options.MaxPatterns=128;options.MaxPlacementAttempts=60000;
 auto original=FailedJigsaw15();auto patterns=Detail::BuildClosurePatterns(original,options,{});VerifyPatterns(original,patterns);Require(HasClosedRectangle(patterns,4),"real former failure must close");
 auto shifted=original;
 for(std::size_t i=0;i<shifted.Parts.size();++i){auto& ring=shifted.Parts[i].Geometry.Outer;for(auto&v:ring){v.X+=1200.25*(i+1);v.Y-=750.5*(i+2);}std::reverse(ring.begin(),ring.end());std::rotate(ring.begin(),ring.begin()+3,ring.end());auto old=ring;ring.clear();for(std::size_t k=0;k<old.size();++k){auto a=old[k],b=old[(k+1)%old.size()];ring.push_back(a);ring.push_back({(a.X+b.X)/2,(a.Y+b.Y)/2});}}
 std::reverse(shifted.Parts.begin(),shifted.Parts.end());patterns=Detail::BuildClosurePatterns(shifted,options,{});VerifyPatterns(shifted,patterns);Require(HasClosedRectangle(patterns,4),"mixed origins/subdivisions/order must close");
 auto small=options;small.MaxPatterns=1;patterns=Detail::BuildClosurePatterns(original,small,{});VerifyPatterns(original,patterns);Require(patterns.size()<=original.Parts.size()+1,"small additional pattern cap");
 small=options;small.MaxPatternMembers=2;patterns=Detail::BuildClosurePatterns(original,small,{});VerifyPatterns(original,patterns,2);Require(patterns.size()==original.Parts.size(),"four-piece problem unsupported at member cap two retains singleton inventory");
 small.MaxPatternMembers=1;patterns=Detail::BuildClosurePatterns(original,small,{});VerifyPatterns(original,patterns,1);Require(patterns.size()==original.Parts.size(),"member cap one");
 small.MaxPatternMembers=0;Require(Detail::BuildClosurePatterns(original,small,{}).empty(),"member cap zero");small=options;small.MaxPatterns=0;Require(Detail::BuildClosurePatterns(original,small,{}).empty(),"pattern cap zero");
 auto gap=original;gap.PartGap=.4;patterns=Detail::BuildClosurePatterns(gap,options,{});VerifyPatterns(gap,patterns);Require(patterns.size()==gap.Parts.size()&&std::all_of(patterns.begin(),patterns.end(),[](const auto&p){return p.Members.size()==1;}),"positive gap keeps true singleton geometry");
 auto five=original;Part fifth;fifth.ID="fifth";fifth.FreeRotation=false;fifth.AllowedAnglesDegrees={0};fifth.Geometry.Outer={{0,0},{10,0},{10,20},{0,20}};five.Parts.push_back(fifth);patterns=Detail::BuildClosurePatterns(five,options,{});VerifyPatterns(five,patterns);Require(patterns.size()==5&&std::all_of(patterns.begin(),patterns.end(),[](const auto&p){return p.Members.size()==1;}),"five-piece inventory is outside small-closure scope");
 auto two=Problem{};two.Stocks={{"two",140,60,1,1,""}};for(int i=0;i<2;++i){auto q=fifth;q.ID="two-"+std::to_string(i);double w=i?40:100;q.Geometry.Outer={{0,0},{w,0},{w,60},{0,60}};two.Parts.push_back(q);}small=options;small.MaxPatternMembers=2;patterns=Detail::BuildClosurePatterns(two,small,{});VerifyPatterns(two,patterns,2);Require(HasClosedRectangle(patterns,2),"two-member closure remains supported");
 int calls=0;patterns=Detail::BuildClosurePatterns(original,options,[&](){return ++calls==12;});VerifyPatterns(original,patterns);Require(calls==12,"single-pulse cancellation must latch before the wider pass");
 calls=0;patterns=Detail::BuildClosurePatterns(original,options,[&](){return ++calls==1;});Require(patterns.empty()&&calls==1,"immediate cancellation");
 options.TimeLimitSeconds=1;
 auto job=RunJob({"closure-test","rules.composite-closure",original,options},DefaultStrategies());
 Require(job.Checked.Valid&&job.Checked.Complete&&job.Output.Layouts.size()==1,"registered closure job must complete original demand on one stock");
 bool pulse=true;options.Cancel=[&]{const bool answer=pulse;pulse=false;return answer;};
 job=RunJob({"closure-cancel","rules.composite-closure",original,options},DefaultStrategies());
 Require(job.Checked.Valid&&job.Output.State==Status::Cancelled,"registered closure cancellation stays latched");
 std::cout<<"ClosurePatternTests passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
