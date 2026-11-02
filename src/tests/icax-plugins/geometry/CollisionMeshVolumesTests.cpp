#include <GeometryServices/CollisionMeshVolumes.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace GS=iCAX::GeometryServices;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Cube(GS::CollisionMesh& mesh,double x,double y,double z){
    const auto base=static_cast<unsigned>(mesh.Vertices.size());
    for(int i=0;i<8;++i)mesh.Vertices.push_back({x+(i&1?1:0),y+(i&2?1:0),z+(i&4?1:0)});
    const std::array<std::array<unsigned,3>,12> faces{{{0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,4,6},{0,6,2},{1,3,7},{1,7,5},{0,1,5},{0,5,4},{2,6,7},{2,7,3}}};
    for(auto t:faces){for(auto& i:t)i+=base;mesh.Triangles.push_back(t);}
}
template<class F>void Reject(F fn,const char* message){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}Check(rejected,message);}
void Exhaustive(const GS::CollisionMesh& mesh,const std::vector<std::vector<unsigned>>& volumes){std::set<unsigned> faces;for(const auto& group:volumes)for(auto i:group)Check(i<mesh.Triangles.size()&&faces.insert(i).second,"Faces may neither duplicate nor escape the source mesh");Check(faces.size()==mesh.Triangles.size(),"Every original triangle must be retained");}
GS::CollisionMesh STL(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);Check(bool(f),"Read actual authored STL");f.seekg(80);unsigned count=0;f.read(reinterpret_cast<char*>(&count),4);GS::CollisionMesh mesh;
    for(unsigned i=0;i<count;++i){std::array<float,12> data{};unsigned short attribute;f.read(reinterpret_cast<char*>(data.data()),48);f.read(reinterpret_cast<char*>(&attribute),2);Check(bool(f),"Complete binary STL record");std::array<unsigned,3> triangle;
        for(int corner=0;corner<3;++corner){triangle[corner]=static_cast<unsigned>(mesh.Vertices.size());mesh.Vertices.push_back({data[3+corner*3]*1000.,data[4+corner*3]*1000.,data[5+corner*3]*1000.});}mesh.Triangles.push_back(triangle);}
    return mesh;
}
}
int main(int argc,char** argv){try{
    GS::CollisionMesh touching;Cube(touching,0,0,0);Cube(touching,1,1,0);auto groups=GS::ProveClosedMeshVolumes(touching);Check(groups.size()==2&&groups[0].size()==12&&groups[1].size()==12,"Four-face shared edge must partition into two independently closed solids");Exhaustive(touching,groups);
    auto translated=touching;for(auto& p:translated.Vertices)for(auto& v:p)v+=1e9;Check(GS::ProveClosedMeshVolumes(translated)==groups,"Volume proof must preserve a large translated mesh");
    auto open=touching;open.Triangles.pop_back();Reject([&]{GS::ProveClosedMeshVolumes(open);},"Open source may not be declared closed");
    auto degenerate=touching;degenerate.Triangles.push_back({0,0,1});Reject([&]{GS::ProveClosedMeshVolumes(degenerate);},"Degenerate source face must be rejected");
    auto invalid=touching;invalid.Triangles[0][0]=static_cast<unsigned>(invalid.Vertices.size());Reject([&]{GS::ProveClosedMeshVolumes(invalid);},"Out-of-range triangle index must be rejected before access");
    auto nonfinite=touching;nonfinite.Vertices[0][0]=std::numeric_limits<double>::infinity();Reject([&]{GS::ProveClosedMeshVolumes(nonfinite);},"Nonfinite coordinates must be rejected");
    Reject([&]{GS::ProveClosedMeshVolumes(touching,[]{return true;});},"Cancellation must stop volume preparation");
    if(argc==2){const auto directory=std::filesystem::path(argv[1])/"src/apps/five-axis-cam/resources/machines/industrial-ac/meshes";GS::CollisionMesh head;
        for(const auto* name:{"laser_head_dark.stl","laser_head_shell.stl","laser_head_steel.stl","laser_head_copper.stl","laser_head_black.stl","laser_head_teal.stl"}){const auto mesh=STL(directory/name);const auto base=static_cast<unsigned>(head.Vertices.size());head.Vertices.insert(head.Vertices.end(),mesh.Vertices.begin(),mesh.Vertices.end());for(auto triangle:mesh.Triangles){for(auto& index:triangle)index+=base;head.Triangles.push_back(triangle);}}
        const auto closed=GS::ProveClosedMeshVolumes(head);Exhaustive(head,closed);Check(closed.size()==1,"Material patches assemble into one closed physical head exterior");
        auto missing=head;missing.Triangles.pop_back();Reject([&]{GS::ProveClosedMeshVolumes(missing);},"Missing material surface must not pass closure proof");
        std::cout<<"PASS actual head 6 material sources, "<<head.Triangles.size()<<" triangles, 1 proven closed volume\n";}
    std::cout<<"PASS neutral closed-volume proof: edge-touching solids, complete face ownership, translation, open/degenerate/index/nonfinite rejection, cancellation\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
