#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace iCAX::SheetNesting;
namespace {
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
Options Budget(){Options o;o.TimeLimitSeconds=2;o.MaxPlacementAttempts=40000;return o;}
JobResult Solve(const Problem& p,Options o=Budget())
{return RunJob({"seam-test","rules.seam-assembly",p,std::move(o)},DefaultStrategies());}
std::size_t Missing(const Result& r){std::size_t n=0;for(const auto& a:r.Remaining)n+=a.Quantity;return n;}
std::size_t Demand(const Problem& p){std::size_t n=0;for(const auto& a:p.Parts)n+=a.Quantity;return n;}
void RequireUnplaced(const Problem& p,const JobResult& r,Status expected=Status::Partial)
{
 Require(r.Checked.Valid&&!r.Checked.Complete,"unsupported/cancelled original inventory must validate");
 Require(r.Output.State==expected,"unsupported/cancelled state");
 Require(r.Output.Layouts.empty()&&Missing(r.Output)==Demand(p),"unsupported/cancelled output must retain every original instance");
}
void Complete(const Problem& p,const JobResult& r)
{
 if(!r.Checked.Valid)for(const auto& e:r.Checked.Errors)std::cerr<<e<<'\n';
 Require(r.Checked.Valid&&r.Checked.Complete,"original geometry and complete demand must validate");
 Require(r.Output.Layouts.size()==1&&r.Checked.PlacedCount==Demand(p),"assembly must fit all original members on one stock");
}
Problem Two(double scale=1,double origin=0,bool reversed=false)
{
 Problem p;Part a,b;a.ID="left";b.ID="right";
 a.Geometry.Outer={{0,0},{40,0},{40,30},{60,50},{40,70},{40,100},{0,100}};
 b.Geometry.Outer={{40,0},{100,0},{100,100},{40,100},{40,70},{60,50},{40,30}};
 p.Parts={a,b};p.Stocks={{"stock",100*scale,100*scale,1,1,""}};
 if(scale<.001)p.Tolerance=1e-7;
 for(std::size_t i=0;i<p.Parts.size();++i){const double ox=(i?1:-1)*origin,oy=(i?-1:1)*origin;
  for(auto& v:p.Parts[i].Geometry.Outer){v.X=v.X*scale+ox;v.Y=v.Y*scale+oy;}
  if(reversed)std::reverse(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end());}
 return p;
}
void Subdivide(std::vector<Point>& ring,std::size_t divisions,std::size_t start=0,bool reverse=false)
{
 auto old=ring;ring.clear();
 for(std::size_t i=0;i<old.size();++i){const auto a=old[i],b=old[(i+1)%old.size()];
  for(std::size_t j=0;j<divisions;++j){const double t=static_cast<double>(j)/divisions;ring.push_back({a.X+(b.X-a.X)*t,a.Y+(b.Y-a.Y)*t});}}
 start%=ring.size();std::rotate(ring.begin(),ring.begin()+static_cast<std::ptrdiff_t>(start),ring.end());
 if(reverse)std::reverse(ring.begin(),ring.end());
}
Problem Twelve()
{
 Problem p;p.Stocks={{"stock",120,120,1,1,""}};
 const auto cut=[](std::size_t row){const double y=10.0*row;if(row==0||row==12)return std::vector<Point>{{0,y},{120,y}};const double offset=.25*row;return std::vector<Point>{{0,y},{24,y},{40,y+offset},{72,y+offset},{88,y},{120,y}};};
 for(std::size_t row=0;row<12;++row){Part part;part.ID="strip-"+std::to_string(row);part.FreeRotation=false;part.AllowedAnglesDegrees={0};part.Geometry.Outer=cut(row);const auto top=cut(row+1);part.Geometry.Outer.insert(part.Geometry.Outer.end(),top.rbegin(),top.rend());const double ox=17.0*row-70,oy=-11.0*row-13;for(auto& v:part.Geometry.Outer){v.X+=ox;v.Y+=oy;}p.Parts.push_back(std::move(part));}
 std::reverse(p.Parts.begin(),p.Parts.end());return p;
}
Problem Subdivided19() { Problem p;p.Tolerance=1e-6;
p.Stocks={{"stock",222.97863160315495,169.78834405783675,1,1,"m"}};
{Part part;part.ID="part-5";part.Material="m";part.Geometry.Outer={{-388.99846364988815,-862.17989773164402},{-418.317566299085,-878.43174171056671},{-500.63258890401926,-729.93150995501856},{-475.5036511598812,-716.00231231004966},{-455.19118815791353,-755.21626061332017},{-437.90503341087737,-786.40130928198528},{-412.04666997926978,-820.59983284009058}};Subdivide(part.Geometry.Outer,11,61,true);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-2";part.Material="m";part.Geometry.Outer={{-59.305594647116379,-473.31460555538962},{-25.445418137807394,-447.79913240960434},{41.226561482636797,-536.27583771209629},{7.3608987017033201,-561.79544506008415},{-11.30725559202105,-537.02196757538638},{-27.969055516858077,-514.8981229972876},{-41.970171237151334,-496.31801488376425}};Subdivide(part.Geometry.Outer,11,39,true);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-1";part.Material="m";part.Geometry.Outer={{491.57239498987144,-192.60788162648225},{516.98962434979171,-238.46177719846537},{491.7974579872897,-252.42703765058525},{471.44959151700749,-263.70604421281496},{447.22218031039915,-277.12664859104922},{420.0916916833562,-292.16532400735559},{417.60154833221935,-287.67298648443307},{406.74636120647813,-245.15806649944443},{400.99338903874991,-234.77942997411543},{418.40030914030058,-225.13061660391071},{446.472168871858,-218.64558095736743},{466.82003534214027,-207.36657439513769}};Subdivide(part.Geometry.Outer,11,19,false);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-4";part.Material="m";part.Geometry.Outer={{-851.74401203440061,-578.53753112599986},{-867.67336221358028,-609.80064113352807},{-910.03246845384831,-588.21759848627312},{-950.59359498630397,-574.32555085701756},{-982.36292466650502,-558.13826887157643},{-1021.130881294528,-536.98701752264662},{-1003.7319009957296,-502.83959600314768},{-964.5818405366058,-523.24092635896511},{-932.81251085640463,-539.42820834440636},{-894.10311827466876,-556.95448847874491}};Subdivide(part.Geometry.Outer,11,1,true);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-0";part.Material="m";part.Geometry.Outer={{11.822092971317801,583.60285036619757},{100.99957733338221,633.03473713387757},{137.3426380271477,567.47012006959687},{112.59027837941653,552.71142730094141},{92.24241190913429,541.4324207387117},{64.170552177576852,534.94738509216836},{46.76363207602617,525.29857172196375},{35.230449496718158,546.1049838652998}};Subdivide(part.Geometry.Outer,11,65,false);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-3";part.Material="m";part.Geometry.Outer={{152.6896266979692,746.20066339492269},{114.56996517707202,725.07058997557363},{91.521758847690421,766.65065486712706},{69.230789462010463,802.8266172349887},{51.944634714974271,834.01166590365381},{30.896042782894583,872.81757127792696},{78.091721970409793,898.97856344102775},{101.50007849581016,861.48069694012997},{118.78623324284635,830.29564827146487},{129.6414203685876,787.78072828647623}};Subdivide(part.Geometry.Outer,11,33,true);p.Parts.push_back(std::move(part));}
return p;}
Problem Subdivided21() { Problem p;p.Tolerance=1e-6;
p.Stocks={{"stock",18.402033495085643,22.892188955836598,1,1,"m"}};
{Part part;part.ID="part-1";part.Material="m";part.Geometry.Outer={{-27.092154196434759,116.59534568886374},{-25.986213090778342,114.60017511974188},{-24.412440229548025,108.92906232035745},{-21.304901267947798,103.32291363226406},{-26.465118827643177,100.46255833145941},{-29.572657789243408,106.06870701955279},{-31.289934897591479,111.66027411583792},{-31.55701836483756,112.14210544539333},{-30.833240359090844,112.54330214522116},{-30.168039122416289,115.09364374935969},{-28.832222180438585,115.83409917138188}};Subdivide(part.Geometry.Outer,11,78,true);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-2";part.Material="m";part.Geometry.Outer={{38.164372912737413,-43.700855518481575},{35.387644489518024,-40.016012443402076},{53.670159492361584,-26.23914921694421},{57.102654835776939,-30.794224387659725},{51.983550634980745,-34.651746091067928},{46.586655564822941,-36.999469457181014},{42.747327414225794,-39.892610734737168}};Subdivide(part.Geometry.Outer,11,3,true);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-0";part.Material="m";part.Geometry.Outer={{14.212542716693175,113.81147112439105},{19.48665253600582,116.73495793559198},{22.62001175376853,111.6665991965039},{23.844724869312284,109.4571582495557},{22.104656885308458,108.69591173207385},{20.768839943330754,107.95545631005166},{20.103638706656199,105.40511470591314},{19.379860700909482,105.0039180060853},{17.316289946955393,108.72669819259994}};Subdivide(part.Geometry.Outer,11,72,true);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-4";part.Material="m";part.Geometry.Outer={{90.681920372389385,29.626437637895013},{87.130229809544915,28.806465262213376},{81.980607767690486,51.111928898187728},{84.945825357700997,51.796503319487158},{86.13224411564633,45.961879305994458},{87.213664744435746,41.277731942439843},{89.240026200670144,35.871967455967834}};Subdivide(part.Geometry.Outer,11,56,true);p.Parts.push_back(std::move(part));}
{Part part;part.ID="part-3";part.Material="m";part.Geometry.Outer={{79.601700710683915,-58.490891897114516},{76.843682713801499,-60.019686236856074},{73.736143752201272,-54.413537548762683},{70.298249472976778,-49.775724124379408},{67.967595251776601,-45.571112608309363},{65.218896009155557,-40.28953336326866},{69.342483189273224,-38.003791664874839},{72.446230419535439,-43.088564596665947},{74.776884640735616,-47.293176112735992},{76.494161749083688,-52.884743209021131}};Subdivide(part.Geometry.Outer,11,25,true);p.Parts.push_back(std::move(part));}
return p;}

}
int main(){try{
 const auto basic=Two();Complete(basic,Solve(basic));
 const auto twelve=Twelve();auto upper=Budget();upper.MaxAssemblyMembers=11;RequireUnplaced(twelve,Solve(twelve,upper));upper.MaxAssemblyMembers=12;upper.MaxPatternMembers=1;Complete(twelve,Solve(twelve,upper));
 // Native-valid former winding counterexample: 0.01 mm stock, million-mm local origins.
 for(bool reverse:{false,true}){const auto tiny=Two(.0001,1e6,reverse);Complete(tiny,Solve(tiny));}
 // Distinct native-valid subdivision regressions, with changed ring start/winding and part order.
 for(const auto& p:{Subdivided19(),Subdivided21()}){auto o=Budget();o.MaxPatternMembers=1;Complete(p,Solve(p,o));}
 auto o=Budget();o.EnableCombinations=false;RequireUnplaced(basic,Solve(basic,o));
 o=Budget();o.MaxAssemblyMembers=1;RequireUnplaced(basic,Solve(basic,o));
 o.MaxAssemblyMembers=2;Complete(basic,Solve(basic,o));
 const auto six=Subdivided19();o=Budget();o.MaxAssemblyMembers=six.Parts.size()-1;RequireUnplaced(six,Solve(six,o));
 o.MaxAssemblyMembers=six.Parts.size();o.MaxPatternMembers=1;Complete(six,Solve(six,o));
 for(std::size_t invalid:{0u,13u}){o=Budget();o.MaxAssemblyMembers=invalid;const auto r=Solve(basic,o);Require(r.Output.State==Status::InvalidInput&&Missing(r.Output)==Demand(basic),"invalid assembly cap must be rejected");}
 auto gap=basic;gap.PartGap=.1;RequireUnplaced(gap,Solve(gap));
 auto material=basic;material.Parts[1].Material="other";RequireUnplaced(material,Solve(material));
 auto repeated=basic;repeated.Parts[0].Quantity=2;RequireUnplaced(repeated,Solve(repeated));
 auto one=basic;one.Parts.resize(1);RequireUnplaced(one,Solve(one));
 auto circle=basic;circle.Parts[0].Geometry={};circle.Parts[0].Geometry.Radius=10;RequireUnplaced(circle,Solve(circle));
 auto wrongStock=basic;wrongStock.Stocks[0].Material="other";RequireUnplaced(wrongStock,Solve(wrongStock));
 for(const auto& p:{basic,one,repeated,material}){o=Budget();o.Cancel=[]{return true;};RequireUnplaced(p,Solve(p,o),Status::Cancelled);}
 for(std::size_t pulse:{1u,2u,3u,4u,8u,16u,32u}){std::size_t calls=0;o=Budget();o.Cancel=[&]{return ++calls==pulse;};const auto r=Solve(six,o);Require(calls==pulse,"triggered single pulse must stay latched");RequireUnplaced(six,r,Status::Cancelled);}
 for(std::size_t cap=1;cap<=20;++cap){o=Budget();o.MaxPlacementAttempts=cap;const auto r=Solve(basic,o);Require(r.Checked.Valid,"attempt-limited original inventory must validate");Require(r.Output.Stats.PlacementAttempts<=cap,"assembly attempt cap must not be exceeded");}
 // Bounded preprocessing cancellation, without relying on a wall-clock threshold.
 auto dense=basic;for(auto& p:dense.Parts)Subdivide(p.Geometry.Outer,5000);o=Budget();std::size_t calls=0;o.Cancel=[&]{++calls;return true;};
 const auto stopped=Detail::RunSeamAssembly(dense,o);Require(calls==1&&stopped.State==Status::Cancelled&&stopped.Stats.PlacementAttempts==0,"immediate cancellation precedes dense preprocessing");Require(stopped.Layouts.empty()&&Missing(stopped)==Demand(dense),"dense cancellation keeps all demand");
 // Allowed source-frame rotation is preserved; mirror poses are never introduced.
 auto oriented=basic;for(auto& v:oriented.Parts[0].Geometry.Outer){const auto old=v;v={-old.Y+123,old.X-456};}
 oriented.Parts[0].FreeRotation=false;oriented.Parts[0].AllowedAnglesDegrees={270};oriented.Parts[1].FreeRotation=false;oriented.Parts[1].AllowedAnglesDegrees={0};Complete(oriented,Solve(oriented));
 std::cout<<"SeamAssemblyTests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
