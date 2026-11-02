// Analytic oracles accompany, and do not replace, the real Classify corpus.
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <filesystem>
#include <vector>
#include <array>
#include <cmath>

namespace fs=std::filesystem;
void make(const fs::path& out,const std::vector<std::array<double,2>>& polygon,int mode){
    BRepBuilderAPI_MakePolygon wire;for(auto p:polygon)wire.Add(gp_Pnt(p[0],-5,p[1]));wire.Close();
    auto solid=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),gp_Vec(0,10,0)).Shape();
    TopoDS_Compound shape;BRep_Builder b;b.MakeCompound(shape);
    for(TopExp_Explorer e(solid,TopAbs_FACE);e.More();e.Next()){
        auto f=TopoDS::Face(e.Current());GProp_GProps g;BRepGProp::SurfaceProperties(f,g);auto p=g.CentreOfMass();
        if(std::abs(std::abs(p.Y())-5)<1e-6)continue;
        bool take=false;
        if(mode==0)take=p.X()<10&&p.Z()>1e-6&&p.Z()<10-1e-6;
        if(mode==1||mode==2)take=(std::abs(p.X()-10)<1e-6||std::abs(p.X()-20)<1e-6)&&p.Z()>6;
        if(mode==1&&std::abs(p.Z()-6)<1e-6)take=true;
        if(mode==3)take=std::abs(p.X())<1e-6;
        if(take)b.Add(shape,f);
    }
    BRepBuilderAPI_MakePolygon section;for(auto p:std::vector<std::array<double,2>>{{-5,0},{5,0},{5,10},{-5,10}})section.Add(gp_Pnt(35,p[0],p[1]));section.Close();b.Add(shape,section.Wire());
    STEPControl_Writer writer;writer.Transfer(shape,STEPControl_AsIs);auto p=out.u8string();writer.Write(std::string(p.begin(),p.end()).c_str());
}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;fs::path out=argv[1];fs::create_directories(out);
    make(out/L"x-bevel.step",{{5,0},{30,0},{30,10},{5,10},{0,5}},0);
    make(out/L"k-bevel.step",{{3,0},{30,0},{30,10},{3,10},{0,7},{0,3}},0);
    make(out/L"blind-with-floor.step",{{0,0},{30,0},{30,10},{20,10},{20,6},{10,6},{10,10},{0,10}},1);
    make(out/L"blind-without-floor.step",{{0,0},{30,0},{30,10},{20,10},{20,6},{10,6},{10,10},{0,10}},2);
    make(out/L"bar-end.step",{{0,0},{30,0},{30,10},{0,10}},3);
    return 0;
}
