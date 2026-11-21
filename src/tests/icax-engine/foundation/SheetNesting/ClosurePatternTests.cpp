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
  Require(Detail::HasSinglePartType(pattern.Members),"closure combined distinct original indices");
  std::map<std::size_t,std::size_t> need;for(auto&m:pattern.Members){Require(m.PartIndex<original.Parts.size(),"original member index");++need[m.PartIndex];Require(Detail::PoseAllowed(original.Parts[m.PartIndex],m.Transform,original.Tolerance),"original pose permission");}
  Problem p=original;p.Parts.clear();p.EdgeMargin=0;p.Stocks={{"pattern",pattern.Width,pattern.Height,1,1,original.Parts[pattern.Members[0].PartIndex].Material}};
  for(auto [index,quantity]:need){auto q=original.Parts[index];Require(quantity<=q.Quantity,"inventory");q.Quantity=quantity;p.Parts.push_back(q);}
  Result result;result.State=Status::Complete;Layout layout;layout.StockID="pattern";layout.Width=pattern.Width;layout.Height=pattern.Height;std::map<std::size_t,std::size_t> seen;
  for(auto&m:pattern.Members)layout.Placements.push_back({original.Parts[m.PartIndex].ID,seen[m.PartIndex]++,m.Transform});
  result.Layouts.push_back(layout);auto check=Validate(p,result);Require(check.Valid&&check.Complete,"original geometry/inventory validation");
 }
}
bool HasClosedRectangle(const std::vector<Detail::Pattern>& patterns,std::size_t members){return std::any_of(patterns.begin(),patterns.end(),[&](const auto&p){return p.Members.size()==members&&p.Kind==ShapeKind::Rectangle;});}
bool OnlySingletons(const std::vector<Detail::Pattern>& patterns){return std::all_of(patterns.begin(),patterns.end(),[](const auto&p){return p.Members.size()==1;});}
Problem SameTypeClosure(){Problem p;p.Tolerance=1e-6;p.Stocks={{"repeat-sheet",120,80,1,1,""}};
 Part q;q.ID="repeat-rectangle";q.Quantity=4;q.FreeRotation=false;q.AllowedAnglesDegrees={0};q.Geometry.Outer={{0,0},{30,0},{30,20},{0,20}};p.Parts.push_back(q);return p;}
void ShiftAndSubdivide(Problem& p){
 for(std::size_t i=0;i<p.Parts.size();++i){auto& ring=p.Parts[i].Geometry.Outer;for(auto&v:ring){v.X+=1200.25*(i+1);v.Y-=750.5*(i+2);}std::reverse(ring.begin(),ring.end());std::rotate(ring.begin(),ring.begin()+3,ring.end());auto old=ring;ring.clear();for(std::size_t k=0;k<old.size();++k){auto a=old[k],b=old[(k+1)%old.size()];ring.push_back(a);ring.push_back({(a.X+b.X)/2,(a.Y+b.Y)/2});}}
 std::reverse(p.Parts.begin(),p.Parts.end());
}
}
int main(){try{
 Options options;options.MaxPatterns=128;options.MaxPlacementAttempts=60000;
 auto original=FailedJigsaw15();auto patterns=Detail::BuildClosurePatterns(original,options,{});VerifyPatterns(original,patterns);
 Require(patterns.size()==original.Parts.size()&&OnlySingletons(patterns),"distinct complementary jigsaw originals entered cross-type closure");
 auto shifted=original;ShiftAndSubdivide(shifted);patterns=Detail::BuildClosurePatterns(shifted,options,{});VerifyPatterns(shifted,patterns);
 Require(patterns.size()==shifted.Parts.size()&&OnlySingletons(patterns),"source origins, subdivisions or ordering bypassed cross-type closure restriction");
 auto self=SameTypeClosure();patterns=Detail::BuildClosurePatterns(self,options,{});VerifyPatterns(self,patterns);
 Require(HasClosedRectangle(patterns,4),"same-original four-member closure was disabled");
 auto shiftedSelf=self;ShiftAndSubdivide(shiftedSelf);patterns=Detail::BuildClosurePatterns(shiftedSelf,options,{});VerifyPatterns(shiftedSelf,patterns);
 Require(HasClosedRectangle(patterns,4),"same-original origins, winding and subdivisions changed closure reachability");
 auto small=options;small.MaxPatterns=1;patterns=Detail::BuildClosurePatterns(self,small,{});VerifyPatterns(self,patterns);
 Require(patterns.size()<=self.Parts.size()+1,"small additional pattern cap");
 small=options;small.MaxPatternMembers=2;patterns=Detail::BuildClosurePatterns(self,small,{});VerifyPatterns(self,patterns,2);
 Require(patterns.size()==self.Parts.size(),"four-instance inventory outside member-cap-two closure keeps singleton inventory");
 small.MaxPatternMembers=1;patterns=Detail::BuildClosurePatterns(self,small,{});VerifyPatterns(self,patterns,1);Require(patterns.size()==self.Parts.size(),"member cap one");
 small.MaxPatternMembers=0;Require(Detail::BuildClosurePatterns(self,small,{}).empty(),"member cap zero");small=options;small.MaxPatterns=0;Require(Detail::BuildClosurePatterns(self,small,{}).empty(),"pattern cap zero");
 auto gap=self;gap.PartGap=.4;patterns=Detail::BuildClosurePatterns(gap,options,{});VerifyPatterns(gap,patterns);Require(patterns.size()==gap.Parts.size()&&OnlySingletons(patterns),"positive gap keeps true singleton geometry");
 auto five=original;Part fifth;fifth.ID="fifth";fifth.FreeRotation=false;fifth.AllowedAnglesDegrees={0};fifth.Geometry.Outer={{0,0},{10,0},{10,20},{0,20}};five.Parts.push_back(fifth);patterns=Detail::BuildClosurePatterns(five,options,{});VerifyPatterns(five,patterns);Require(patterns.size()==5&&std::all_of(patterns.begin(),patterns.end(),[](const auto&p){return p.Members.size()==1;}),"five-piece inventory is outside small-closure scope");
 auto two=Problem{};two.Stocks={{"two",140,60,1,1,""}};for(int i=0;i<2;++i){auto q=fifth;q.ID="two-"+std::to_string(i);double w=i?40:100;q.Geometry.Outer={{0,0},{w,0},{w,60},{0,60}};two.Parts.push_back(q);}small=options;small.MaxPatternMembers=2;patterns=Detail::BuildClosurePatterns(two,small,{});VerifyPatterns(two,patterns,2);
 Require(patterns.size()==two.Parts.size()&&OnlySingletons(patterns),"different rectangle originals entered cross-type closure");
 auto sameTwo=two;sameTwo.Parts.resize(1);sameTwo.Parts[0].Quantity=2;sameTwo.Parts[0].Geometry.Outer={{0,0},{70,0},{70,60},{0,60}};
 patterns=Detail::BuildClosurePatterns(sameTwo,small,{});VerifyPatterns(sameTwo,patterns,2);Require(HasClosedRectangle(patterns,2),"same-original two-member closure remains supported");
 int calls=0;patterns=Detail::BuildClosurePatterns(self,options,[&](){return ++calls==12;});VerifyPatterns(self,patterns);Require(calls==12,"single-pulse cancellation must latch before the wider pass");
 calls=0;patterns=Detail::BuildClosurePatterns(self,options,[&](){return ++calls==1;});Require(patterns.empty()&&calls==1,"immediate cancellation");
 options.TimeLimitSeconds=1;
 auto job=RunJob({"closure-test","rules.composite-closure",self,options},DefaultStrategies());
 Require(job.Checked.Valid&&job.Checked.Complete&&job.Output.Layouts.size()==1,"registered self-closure job must complete original demand on one stock");
 job=RunJob({"separate-types-test","rules.composite-closure",two,options},DefaultStrategies());
 Require(job.Checked.Valid&&job.Checked.Complete&&job.Output.Layouts.size()==1,"forbidding cross-type closure blocked independent original rectangles on one stock");
 bool pulse=true;options.Cancel=[&]{const bool answer=pulse;pulse=false;return answer;};
 job=RunJob({"closure-cancel","rules.composite-closure",self,options},DefaultStrategies());
 Require(job.Checked.Valid&&job.Output.State==Status::Cancelled,"registered closure cancellation stays latched");
 std::cout<<"ClosurePatternTests passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
