#include "../../../iCAX-Plugins/cam/Machine/SDFLoader.cpp"
#include <cassert>
#include <iostream>
using namespace iCAX::CAM;
static std::string path="output/tests/machine-simulation/frame-test.sdf";
static CMachineDescriptionResource load(const std::string& model){std::ofstream(path)<<"<sdf version='1.7'><model name='test'>"<<model<<"</model></sdf>";CMachineDescriptionResource d;std::string e;if(!LoadSDFMachineDescription(path,d,e))throw std::runtime_error(e);return d;}
static void reject(const std::string& model){bool rejected=false;try{load(model);}catch(const std::exception&){rejected=true;}assert(rejected);}
static const SMachineElementData& part(const CMachineDescriptionResource& d,const char* name){for(const auto& e:d.Elements)if(e.OriginalName==name)return e;throw std::runtime_error(name);}
static void expectNear(double a,double b){assert(std::abs(a-b)<1e-8);}
int main(){
 const auto d=load(R"(<pose>1 2 3 0 0 0</pose><link name='base'/><link name='arm'><pose>1 0 0 0 0 0</pose><visual name='v'><pose>.2 0 0 0 0 0</pose></visual><collision name='c'/><plugin name='icax_machine_tool' filename='icax.machine.tool'><tcp_position_mm>200 0 0</tcp_position_mm><beam_direction>1 0 0</beam_direction></plugin></link><frame name='axis_frame'><pose degrees='true'>0 0 0 0 0 90</pose></frame><joint name='j' type='revolute'><parent>base</parent><child>arm</child><pose>0 .5 0 0 0 0</pose><axis><xyz expressed_in='axis_frame'>1 0 0</xyz><limit><lower>-3</lower><upper>3</upper><velocity>1</velocity></limit></axis></joint>)");
 const auto& base=part(d,"base");expectNear(base.Pose[0],1);expectNear(base.Pose[1],2);expectNear(base.Pose[2],3);
 const auto& arm=part(d,"arm");expectNear(arm.JointToParent.Pose[0],1);expectNear(arm.JointToParent.Pose[1],.5);expectNear(arm.Visuals[0].Pose[0],.2);expectNear(arm.Visuals[0].Pose[1],-.5);
 expectNear(arm.JointToParent.Axis.XYZ[0].To<double>(),0);expectNear(arm.JointToParent.Axis.XYZ[1].To<double>(),1);expectNear(arm.Tool.TcpLocalPosition[1].To<double>(),-500);
 Motion::Node node;node.Type="revolute";node.Axis={0,1,0};const auto world=Motion::Multiply(SDFFrames::Matrix(base.Pose),Motion::Multiply(SDFFrames::Matrix(arm.Pose),Motion::Joint(node,1.5707963267948966)));
 const auto visual=Motion::Multiply(world,SDFFrames::Matrix(arm.Visuals[0].Pose));expectNear(visual[3],2);expectNear(visual[7],2);expectNear(visual[11],2.8);
 const auto rotated=load(R"(<frame name='mount'><pose>0 0 1 0 0 1.5707963267948966</pose></frame><link name='a'><pose relative_to='mount'>1 0 0 0 0 0</pose></link><joint name='j' type='fixed'><parent>world</parent><child>a</child></joint>)");expectNear(part(rotated,"a").Pose[0],0);expectNear(part(rotated,"a").Pose[1],1);expectNear(part(rotated,"a").Pose[2],1);
 const auto support=load(R"(<link name='declared'><icax_work_support/></link><link name='work_table'/>)");assert(part(support,"declared").bWorkSupport);assert(!part(support,"work_table").bWorkSupport);
 reject("<link name='a'/><link name='a'/>");
 reject("<link name='a'><pose relative_to='missing'>0 0 0 0 0 0</pose></link>");
 reject("<link name='a'><pose>0 0</pose></link>");
 reject("<link name='a'/><joint name='j' type='fixed'><parent>missing</parent><child>a</child></joint>");
 reject("<link name='a'/><link name='b'/><joint name='j' type='fixed'><parent>a</parent><child>b</child></joint><joint name='k' type='fixed'><parent>b</parent><child>a</child></joint>");
 reject("<link name='a'/><frame name='b' attached_to='c'/><frame name='c' attached_to='b'/>");
 reject("<link name='a'/><model name='nested'/>");
 reject("<link name='a'/><joint name='j' type='ball'><parent>world</parent><child>a</child></joint>");
 reject("<link name='a'/><joint name='j' type='revolute'><parent>world</parent><child>a</child><axis><xyz>0 0 1</xyz><limit><lower>2</lower><upper>-2</upper></limit></axis></joint>");
 std::cout<<"PASS: SDF model/link/joint frames, offset pivot, expressed_in, degrees, TCP rebase, relative_to and 9 malformed model cases\n";
}
