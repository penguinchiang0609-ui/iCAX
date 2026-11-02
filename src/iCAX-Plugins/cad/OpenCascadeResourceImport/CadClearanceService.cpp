#include "pch.h"
#include "CadClearanceService.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include "FixtureMeshGeometry.h"
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <GeometryServices/CollisionDistanceService.h>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

namespace
{
    class CBRepCollisionBoundsService final:public iCAX::GeometryServices::IBRepCollisionBoundsService {
        AUTO_REGIST_SERVICE(iCAX::GeometryServices::IBRepCollisionBoundsService, CBRepCollisionBoundsService)
    public:
        void OnLoad() override{} void OnUnload() override{}
        std::vector<iCAX::GeometryServices::CollisionBox> Boxes(const iCAX::GeometryData::BRepModel& model,
            const std::function<bool()>& cancelled) const override{
            if(cancelled&&cancelled())throw std::runtime_error("CAD_BOUNDS_CANCELLED");
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);
            if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("CAD_BOUNDS_INVALID");
            Bnd_Box box;BRepBndLib::Add(built.Shape,box,false);
            if(box.IsVoid()||box.IsWhole()||box.IsOpen())throw std::invalid_argument("CAD_BOUNDS_UNBOUNDED");
            double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
            iCAX::GeometryServices::CollisionBox result;result.Center={(x+X)*.5,(y+Y)*.5,(z+Z)*.5};result.HalfExtents={(X-x)*.5,(Y-y)*.5,(Z-z)*.5};return {result};
        }
    };
    class CCadClearanceService final : public iCAX::Geometry::ICadClearanceService
    {
        AUTO_REGIST_SERVICE(iCAX::Geometry::ICadClearanceService, CCadClearanceService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        std::vector<iCAX::Geometry::SClearancePair> Measure(
            const std::vector<iCAX::Geometry::SClearanceObject>& objects,
            const std::function<bool()>& isCancelled) const override
        {
            auto cancelled = [&] { if (isCancelled && isCancelled()) throw std::runtime_error("CAD_CLEARANCE_CANCELLED"); };
            std::vector<TopoDS_Shape> shapes;
            std::vector<iCAX::FixtureMesh::Mesh> meshes;
            for (const auto& object : objects)
            {
                cancelled();
                if (!object.Geometry) throw std::invalid_argument("CAD_CLEARANCE_GEOMETRY_REQUIRED");
                const auto& m = object.Transform;
                for (auto v : m) if (!std::isfinite(v)) throw std::invalid_argument("CAD_CLEARANCE_INVALID_TRANSFORM");
                if (std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1) > 1e-8)
                    throw std::invalid_argument("CAD_CLEARANCE_INVALID_TRANSFORM");
                for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
                    double dot=0;for(int k=0;k<3;++k)dot+=m[k*4+i]*m[k*4+j];
                    if(std::abs(dot-(i==j?1.:0.))>1e-8)throw std::invalid_argument("CAD_CLEARANCE_INVALID_TRANSFORM");
                }
                const double det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
                if(std::abs(det-1)>1e-8)throw std::invalid_argument("CAD_CLEARANCE_INVALID_TRANSFORM");
                const auto built = iCAX::OpenCascade::BuildOpenCascadeShape(*object.Geometry);
                if (!built.bOK || built.Shape.IsNull()) throw std::runtime_error("CAD_CLEARANCE_INVALID_BREP");
                gp_Trsf transform;
                transform.SetValues(m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7], m[8],m[9],m[10],m[11]);
                shapes.push_back(built.Shape.Moved(TopLoc_Location(transform)));
                meshes.push_back(iCAX::FixtureMesh::Tessellate(shapes.back(),.005,isCancelled));
            }
            std::vector<iCAX::Geometry::SClearancePair> result;
            for (size_t a=0;a<shapes.size();++a) for (size_t b=a+1;b<shapes.size();++b)
            {
                cancelled();
                auto distance=iCAX::FixtureMesh::Measure(meshes[a],meshes[b]);
                // Surface distance alone misses a component wholly contained
                // in a solid. Classification is independent of mesh distance.
                const auto contained=[&](const TopoDS_Shape& solidShape,const TopoDS_Shape& other){
                    for(TopExp_Explorer s(solidShape,TopAbs_SOLID);s.More();s.Next()){
                        BRepClass3d_SolidClassifier classify(s.Current());
                        for(TopExp_Explorer v(other,TopAbs_VERTEX);v.More();v.Next()){
                            cancelled();classify.Perform(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())),1e-7);
                            if(classify.State()==TopAbs_IN||classify.State()==TopAbs_ON)return true;
                        }
                    }return false;
                };
                if(distance.Distance>0&&(contained(shapes[a],shapes[b])||contained(shapes[b],shapes[a])))distance.Distance=0;
                result.push_back({objects[a].ID,objects[b].ID,distance.Distance,distance.First,distance.Second,meshes[a].Allowance+meshes[b].Allowance});
            }
            cancelled();
            return result;
        }
    };
}
