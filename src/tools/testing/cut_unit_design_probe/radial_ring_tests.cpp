#define wmain batch_main
#include "batch_cut_units.cpp"
#undef wmain
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>

TopoDS_Wire circle(double x,double y,double radius){return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(x,y,0),gp_Dir(0,0,1)),radius))).Wire();}
TopoDS_Face withHole(TopoDS_Wire outer,TopoDS_Wire inner){BRepBuilderAPI_MakeFace f(outer);inner.Reverse();f.Add(inner);return f.Face();}
int main(){try{
    auto run=[](const char* name,const TopoDS_Face& f,bool expected){Piece p;bool actual=radialRing(f,p);if(actual!=expected)throw std::runtime_error(name);if(actual&&(!BRepCheck_Analyzer(p.face).IsValid()||std::abs(area(f)-area(p.face))>1e-8||p.lines.empty()))throw std::runtime_error("ring-region-changed");std::cout<<name<<" passed\n";};
    run("concentric-annulus",withHole(circle(0,0,10),circle(0,0,8)),true);
    run("eccentric-annulus",withHole(circle(0,0,10),circle(2,1,4)),true);
    run("thin-annulus",withHole(circle(0,0,10),circle(0,0,9.999)),true);
    auto outer=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),15,10))).Wire();
    run("ellipse-with-round-hole",withHole(outer,circle(0,0,7)),true);
    BRepBuilderAPI_MakeFace multi(circle(0,0,10));auto hole=circle(-3,0,1);hole.Reverse();multi.Add(hole);hole=circle(3,0,1);hole.Reverse();multi.Add(hole);run("two-holes-must-not-merge",multi.Face(),false);
    BRepBuilderAPI_MakePolygon poly;for(auto p:{gp_Pnt(-10,-10,0),gp_Pnt(10,-10,0),gp_Pnt(10,10,0),gp_Pnt(2,10,0),gp_Pnt(2,-3,0),gp_Pnt(-2,-3,0),gp_Pnt(-2,10,0),gp_Pnt(-10,10,0)})poly.Add(p);poly.Close();
    run("non-star-ring-must-not-merge",withHole(poly.Wire(),circle(-6,0,1)),false);
    return 0;
}catch(const Standard_Failure& e){std::cerr<<e.GetMessageString();return 1;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
