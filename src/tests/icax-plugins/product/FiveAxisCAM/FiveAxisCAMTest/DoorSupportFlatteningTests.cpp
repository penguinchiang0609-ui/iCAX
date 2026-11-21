#include "pch.h"
#include <OpenCascadeResourceImport/CadBRepImportService.h>
#include <OpenCascadeResourceImport/CadBRepTransformService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/SectionPlateService.h>
#include <OpenCascadeResourceImport/TubeCadJson.h>
#include <GeometryData/BRepPersistence.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRepTopAdaptor_Tool.hxx>
#include <BRepTopAdaptor_TopolTool.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <GeomAPI_IntCS.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Contap_Contour.hxx>
#include <Contap_Line.hxx>
#include <IntSurf_PntOn2S.hxx>
#include <NCollection_List.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Type.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopAbs.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Lin2d.hxx>
#include <gp_Pln.hxx>
#include <bcrypt.h>
#include <cstdlib>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
#include <typeinfo>

// The direct contour replay calls TKHLR in addition to the normal test imports.
#pragma comment(lib,"TKHLR.lib")

namespace door_support_tests {
using Map=iCAX::Data::ObjectMap;
using Array=iCAX::Data::VariantArray;
using UInt=unsigned long long;
using ShapeMap=NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher>;
using Plate=iCAX::Geometry::SectionPlate;
using Input=iCAX::Geometry::SectionPlateInput;

std::string Environment(const char* name){
    char* value=nullptr;size_t length=0;_dupenv_s(&value,&length,name);
    const std::string result=value?value:"";std::free(value);return result;
}
bool Enabled(const char* name){const auto value=Environment(name);return !value.empty()&&value!="0";}
std::string Utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::filesystem::path Utf8Path(const std::string& value){return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(value.data()),value.size()));}
template<class Bytes> std::string Digest(const Bytes& bytes){
    if(bytes.size()>std::numeric_limits<ULONG>::max())throw std::runtime_error("Door audit data exceeds SHA-256 input capacity");
    unsigned char digest[32]{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<typename Bytes::value_type*>(bytes.data())),static_cast<ULONG>(bytes.size()),digest,sizeof(digest))<0)
        throw std::runtime_error("Door audit SHA-256 failed");
    constexpr char hex[]="0123456789abcdef";std::string result;
    for(const auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
template<class Values> Array Numbers(const Values& values){Array result;for(const auto value:values)result.emplace_back(value);return result;}
void Require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
std::array<double,16> Matrix(const gp_Trsf& transform){
    std::array<double,16> result{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    for(int row=0;row<3;++row)for(int column=0;column<4;++column)result[row*4+column]=transform.Value(row+1,column+1);
    return result;
}
std::array<double,6> Bounds(const TopoDS_Shape& shape){
    Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);Require(!bounds.IsVoid()&&!bounds.IsOpen(),"Door audit has unbounded geometry");
    std::array<double,6> result;bounds.Get(result[0],result[1],result[2],result[3],result[4],result[5]);return result;
}
TopoDS_Compound BoundaryFaces(const TopoDS_Shape& shape){
    // Keep the original trimmed analytic faces, but omit the solid wrapper:
    // DistShapeShape otherwise uses a fixed .001 mm vertex-in-solid classifier
    // and may replace the boundary distance with an assumed interior zero.
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);size_t count=0;
    for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next()){builder.Add(result,face.Current());++count;}
    Require(count>0,"Door independent distance has no boundary faces");return result;
}
std::string SurfaceName(GeomAbs_SurfaceType type){
    switch(type){case GeomAbs_Plane:return "Plane";case GeomAbs_Cylinder:return "Cylinder";case GeomAbs_Cone:return "Cone";
    case GeomAbs_Sphere:return "Sphere";case GeomAbs_Torus:return "Torus";case GeomAbs_BezierSurface:return "Bezier";
    case GeomAbs_BSplineSurface:return "BSpline";case GeomAbs_SurfaceOfRevolution:return "Revolution";
    case GeomAbs_SurfaceOfExtrusion:return "Extrusion";default:return "Other";}
}
struct FaceAudit {
    std::uint64_t ID=0;
    double Area=0;
    std::array<double,3> Center{},MidpointMaterialNormal{};
    std::array<double,6> Box{};
    std::string Surface;
    std::string NormalError;
    int OccurrenceOrientation=0;
    bool NormalDefined=false;
};
Map FaceValue(const FaceAudit& face){
    return {{"faceId",std::to_string(face.ID)},{"areaMm2",face.Area},{"centerMm",Numbers(face.Center)},
        {"boundsMm",Numbers(face.Box)},{"surfaceType",face.Surface},{"midpointNormalDefined",face.NormalDefined},
        {"midpointMaterialNormal",Numbers(face.MidpointMaterialNormal)},{"midpointNormalError",face.NormalError},
        {"occurrenceOrientation",face.OccurrenceOrientation}};
}
struct Source {
    std::filesystem::path Path;
    std::string FileDigest;
    std::string SourceFrame="original";
    std::array<double,16> BakedTransform=Matrix(gp_Trsf{});
    std::shared_ptr<const iCAX::GeometryData::BRepModel> Geometry;
    iCAX::OpenCascade::SOpenCascadeBRepBuildResult Native;
    std::vector<std::uint8_t> Serialized;
    std::vector<FaceAudit> Faces;
};
const Source& ImportedDoor(){
    // One import per process, including matrix runs. Source bytes are released
    // after the neutral BRep is frozen; no display triangulation is requested.
    static const Source source=[] {
        Source result;
        if(const auto frame=Environment("ICAX_DOOR_SUPPORT_SOURCE_FRAME");!frame.empty())result.SourceFrame=frame;
        Require(result.SourceFrame=="original"||result.SourceFrame=="centered","ICAX_DOOR_SUPPORT_SOURCE_FRAME must be original or centered");
        const auto relative=std::filesystem::path(u8"TestData/FA/汽车/05_Door_Inner_Panel_1240x1040_t1p0.step");
        auto root=std::filesystem::current_path();
        while(!std::filesystem::is_regular_file(root/relative)){
            const auto parent=root.parent_path();if(parent==root||parent.empty())throw std::runtime_error("Door STEP not found under workspace");root=parent;
        }
        result.Path=root/relative;
        iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        const auto importer=provider.Resolve<iCAX::CAD::ICadBRepImportService>();Require(!!importer,"Door CAD importer unavailable");
        auto loaded=importer->Load({.SourcePath=result.Path,.IncludeDisplayMesh=false});
        Require(loaded.IsValidBRep,"Door import did not produce a valid BRep");
        result.FileDigest=loaded.SourceSha256;
        if(result.SourceFrame=="centered"){
            Require(!loaded.Bounds.Empty,"Door import bounds are empty for source-frame reset");
            const auto& bounds=loaded.Bounds;
            result.BakedTransform[3]=-(bounds.Min.X+bounds.Max.X)*.5;
            result.BakedTransform[7]=-(bounds.Min.Y+bounds.Max.Y)*.5;
            result.BakedTransform[11]=-(bounds.Min.Z+bounds.Max.Z)*.5;
            const auto transform=provider.Resolve<iCAX::Geometry::ICadBRepTransformService>();Require(!!transform,"Door CAD rigid transform service unavailable");
            loaded.Geometry=transform->Transform(loaded.Geometry,result.BakedTransform);
        }
        result.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(std::move(loaded.Geometry));
        for(const auto& record:result.Geometry->Triangulations3)
            Require(record.Geometry.Vertices.empty()&&record.Geometry.Triangles.empty(),"Door source unexpectedly contains display mesh geometry");
        result.Serialized=iCAX::GeometryData::Persistence::Serialize(*result.Geometry);
        result.Native=iCAX::OpenCascade::ReconstructOpenCascadeShape(*result.Geometry);
        Require(result.Native.bOK,"Door neutral BRep reconstruction failed");
        for(const auto& [id,canonical]:result.Native.Faces){
            // The ID map deliberately stores canonical-forward faces. Resolve
            // the actual owning shell/root occurrence before material normals.
            TopoDS_Face face;
            for(TopExp_Explorer occurrence(result.Native.Shape,TopAbs_FACE);occurrence.More();occurrence.Next()){
                if(!occurrence.Current().IsSame(canonical))continue;
                Require(face.IsNull()||face.Orientation()==occurrence.Current().Orientation(),"Door face has ambiguous material-side occurrences");
                face=TopoDS::Face(occurrence.Current());
            }
            Require(!face.IsNull(),"Door face canonical ID has no source occurrence");
            FaceAudit audit;audit.ID=id;audit.OccurrenceOrientation=static_cast<int>(face.Orientation());GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties);
            audit.Area=std::abs(properties.Mass());const auto center=properties.CentreOfMass();audit.Center={center.X(),center.Y(),center.Z()};audit.Box=Bounds(face);
            BRepAdaptor_Surface surface(face,true);audit.Surface=SurfaceName(surface.GetType());
            double u0=0,u1=0,v0=0,v1=0;BRepTools::UVBounds(face,u0,u1,v0,v1);
            if(std::isfinite(u0)&&std::isfinite(u1)&&std::isfinite(v0)&&std::isfinite(v1)){
                try{
                    BRepLProp_SLProps normal(surface,(u0+u1)*.5,(v0+v1)*.5,1,1e-9);
                    if(normal.IsNormalDefined()){
                        auto direction=normal.Normal();if(face.Orientation()==TopAbs_REVERSED)direction.Reverse();
                        audit.NormalDefined=true;audit.MidpointMaterialNormal={direction.X(),direction.Y(),direction.Z()};
                    }
                }catch(const Standard_Failure& failure){const char* message=failure.what();audit.NormalError=message?message:"OCCT midpoint normal unavailable";}
            }
            result.Faces.push_back(audit);
        }
        std::sort(result.Faces.begin(),result.Faces.end(),[](const auto& a,const auto& b){return a.Area>b.Area;});
        return result;
    }();
    return source;
}
void WriteReport(const Map& report){
    const auto target=Environment("ICAX_DOOR_SUPPORT_REPORT");if(target.empty())return;
    const auto path=Utf8Path(target);if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);
    Require(file.good(),"Cannot write Door support audit report");
}
struct Pose {std::string Name;gp_Trsf Transform;std::array<double,3> Direction{0,0,1};};
std::vector<Pose> Poses(const Source& source){
    auto mode=Environment("ICAX_DOOR_SUPPORT_TRANSFORM_MODE");if(mode.empty())mode="identity";
    Pose identity;identity.Name="identity";
    Pose translation;translation.Name="translation";translation.Transform.SetTranslation(gp_Vec(137,-83,250));
    Pose tilt;tilt.Name="sync-tilt";gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),.43);
    tilt.Transform=translation.Transform*rotation;const auto direction=gp_Dir(0,0,1).Transformed(rotation);tilt.Direction={direction.X(),direction.Y(),direction.Z()};
    if(mode=="center-placement"){Pose centered;centered.Name=mode;const auto bounds=Bounds(source.Native.Shape);centered.Transform.SetTranslation(gp_Vec(-(bounds[0]+bounds[3])/2,-(bounds[1]+bounds[4])/2,-(bounds[2]+bounds[5])/2));return {centered};}
    if(mode=="identity")return {identity};if(mode=="translation")return {translation};if(mode=="sync-tilt")return {tilt};
    if(mode=="matrix"||mode=="all")return {identity,translation,tilt};
    throw std::invalid_argument("ICAX_DOOR_SUPPORT_TRANSFORM_MODE must be identity, translation, sync-tilt, center-placement or matrix");
}
Bnd_Box ConservativeBounds(const TopoDS_Shape& shape){
    Bnd_Box result;BRepBndLib::AddOptimal(shape,result,false,true);return result;
}
bool FiniteBounds(const Bnd_Box& box){
    if(box.IsVoid()||box.IsOpen())return false;double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
    return std::isfinite(x0)&&std::isfinite(y0)&&std::isfinite(z0)&&std::isfinite(x1)&&std::isfinite(y1)&&std::isfinite(z1);
}
double BoundaryDistance(const TopoDS_Shape& a,const TopoDS_Shape& b){
    BRepExtrema_DistShapeShape distance;distance.LoadS1(a);distance.LoadS2(b);distance.SetMultiThread(false);
    Require(distance.Perform(),"Door independent boundary distance calculation failed");const double result=distance.Value();
    Require(std::isfinite(result)&&result>=0,"Door independent boundary distance is not finite");return result;
}
TopoDS_Shape ValidCommon(const TopoDS_Shape& a,const TopoDS_Shape& b){
    BRepAlgoAPI_Common operation;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(a);tools.Append(b);
    operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
    Require(operation.IsDone(),"Door independent BRep intersection failed");
    Require(BRepCheck_Analyzer(operation.Shape()).IsValid(),"Door independent BRep intersection produced an invalid result");return operation.Shape();
}
double IntersectionVolume(const TopoDS_Shape& a,const TopoDS_Shape& b,const Bnd_Box& boundsA,const Bnd_Box& boundsB,std::string& proof){
    if(FiniteBounds(boundsA)&&FiniteBounds(boundsB)&&boundsA.Distance(boundsB)>0){proof="strictly disjoint conservative BRep bounds";return 0;}
    TopoDS_Shape intersection;
    if(FiniteBounds(boundsA)){
        double x0,y0,z0,x1,y1,z1;boundsA.Get(x0,y0,z0,x1,y1,z1);
        const auto queryBox=BRepPrimAPI_MakeBox(gp_Pnt(x0-1,y0-1,z0-1),gp_Pnt(x1+1,y1+1,z1+1)).Shape();
        // a lies strictly inside this independently computed enclosure. Hence
        // a INTERSECT b == a INTERSECT (b INTERSECT queryBox), retaining every
        // original solid, cavity and hole. Artificial caps cannot touch a.
        const auto clipped=ValidCommon(b,queryBox);
        if(clipped.ShapeType()==TopAbs_COMPOUND&&!TopoDS_Iterator(clipped).More()){
            proof="independent complete-solid BRep box clipping produced a valid empty compound";return 0;
        }
        intersection=ValidCommon(a,clipped);
        proof="independent complete-solid BRep clipping by conservative bounds plus 1mm, then valid BRep Common";
    }else{intersection=ValidCommon(a,b);proof="complete BRep Common with valid result";}
    double result=0;for(TopExp_Explorer solid(intersection,TopAbs_SOLID);solid.More();solid.Next()){GProp_GProps properties;BRepGProp::VolumeProperties(solid.Current(),properties);result+=std::abs(properties.Mass());}
    Require(std::isfinite(result)&&result>=0,"Door independent intersection volume is not finite");
    return result;
}
struct DistanceEvidence {Map Data;bool Verified=false;};
double MaximumExactProfileJunctionGap(const Plate& part){
    Require(!part.ExactProfile.Boundaries.empty(),"Door plate has no exact manufacturing profile");
    double maximum=0;
    for(const auto& loop:part.ExactProfile.Boundaries){
        Require(!loop.Curves.empty(),"Door exact manufacturing profile contains an empty loop");
        std::vector<std::pair<gp_Pnt2d,gp_Pnt2d>> endpoints;endpoints.reserve(loop.Curves.size());
        for(const auto& edge:loop.Curves){
            const auto& segment=edge.Segment;const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(segment.Curve);
            Require(!curve.IsNull(),"Door exact manufacturing profile curve is invalid");
            double first=segment.Range.First,last=segment.Range.Last;
            // Segment2 stores normalized parameters; its native support uses
            // distance along the line, as in FixtureJoineryTests JointExactArea.
            if(const auto* line=std::get_if<iCAX::GeometryData::Segment2>(&segment.Curve)){
                const double length=std::hypot(line->End.X-line->Start.X,line->End.Y-line->Start.Y);first*=length;last*=length;
            }
            Require(std::isfinite(first)&&std::isfinite(last),"Door exact manufacturing profile has nonfinite trim parameters");
            const auto start=curve->Value(segment.Range.Reversed?last:first),end=curve->Value(segment.Range.Reversed?first:last);
            Require(std::isfinite(start.X())&&std::isfinite(start.Y())&&std::isfinite(end.X())&&std::isfinite(end.Y()),
                "Door exact manufacturing profile has a nonfinite endpoint");
            endpoints.emplace_back(start,end);
        }
        // Audit the curve coordinates themselves, including the closing seam.
        // No BRep vertex or edge tolerance can excuse an open manufacturing loop.
        for(size_t index=0;index<endpoints.size();++index){
            const double gap=endpoints[index].second.Distance(endpoints[(index+1)%endpoints.size()].first);
            Require(std::isfinite(gap),"Door exact manufacturing profile has a nonfinite junction gap");maximum=std::max(maximum,gap);
        }
    }
    return maximum;
}
DistanceEvidence AuditDistance(const Plate& part,const TopoDS_Shape& plateBoundary,const TopoDS_Shape& workBoundary,
    const Bnd_Box& plateBounds,const Bnd_Box& workBounds,const Input& input){
    DistanceEvidence result;
    if(part.Base||part.Connection){
        const double required=part.Base?input.BaseDrop:input.Gap;result.Data["requiredClearanceMm"]=required;
        if(FiniteBounds(plateBounds)&&FiniteBounds(workBounds)){
            const double lower=plateBounds.Distance(workBounds);result.Data["clearanceLowerBoundMm"]=lower;
            if(lower+1e-7>=required){result.Data["method"]=std::string("conservative BRep bounds clearance certificate");result.Verified=true;}
        }
        if(!result.Verified){
            const double measured=BoundaryDistance(plateBoundary,workBoundary);result.Data["method"]=std::string("complete trimmed BRep boundary distance");
            result.Data["measuredBoundaryDistanceMm"]=measured;result.Verified=measured+1e-7>=required;
        }
    }else{
        Array errors;size_t attempts=0;double bestUpper=std::numeric_limits<double>::max();
        for(const auto& contact:part.Contacts){
            ++attempts;
            try{
                const auto point=BRepBuilderAPI_MakeVertex(gp_Pnt(contact[0],contact[1],contact[2])).Vertex();
                const double toPlate=BoundaryDistance(point,plateBoundary);if(toPlate>1e-5)continue;
                const double toWork=BoundaryDistance(point,workBoundary),upper=toPlate+toWork;bestUpper=std::min(bestUpper,upper);
                // A shared witness bounds the true global minimum by the SUM
                // of its two exact distances; neither value is a global minimum.
                if(upper<=1e-5){
                    result.Data["method"]=std::string("shared exact point-to-boundary contact witness");result.Data["pointMm"]=Numbers(contact);
                    result.Data["pointToPartBoundaryMm"]=toPlate;result.Data["pointToWorkpieceBoundaryMm"]=toWork;
                    result.Data["contactDistanceUpperBoundMm"]=upper;result.Verified=true;break;
                }
            }catch(const Standard_Failure& failure){const char* message=failure.what();errors.emplace_back(std::string(message?message:"OCCT contact witness query failed"));}
            catch(const std::exception& failure){errors.emplace_back(std::string(failure.what()));}
        }
        result.Data["witnessAttempts"]=static_cast<UInt>(attempts);if(!errors.empty())result.Data["witnessQueryErrors"]=std::move(errors);
        if(!result.Verified){
            if(bestUpper<std::numeric_limits<double>::max())result.Data["contactDistanceUpperBoundMm"]=bestUpper;
            const double measured=BoundaryDistance(plateBoundary,workBoundary);result.Data["method"]=std::string("complete trimmed BRep boundary distance");
            result.Data["measuredBoundaryDistanceMm"]=measured;result.Verified=measured<=1e-5;
        }
    }
    result.Data["verified"]=result.Verified;return result;
}
void WriteBytes(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){
    Require(bytes.size()<=static_cast<size_t>(std::numeric_limits<std::streamsize>::max()),"Door neutral BRep dump exceeds stream capacity");
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    file.close();Require(file.good(),"Cannot write Door neutral BRep dump: "+Utf8(path));
}
std::string SaveReturnedParts(const Source& source,const Input& input,const Pose& pose,const std::vector<Plate>& parts){
    const auto directory=Environment("ICAX_DOOR_PARTS_DIR");if(directory.empty())return {};
    const auto target=Utf8Path(directory)/pose.Name;std::filesystem::create_directories(target);
    // Persist the raw result before any posterior validity, distance or Boolean
    // audit. The manifest explicitly carries no acceptance claim.
    WriteBytes(target/"source.icbrep",source.Serialized);Array records;
    for(size_t index=0;index<parts.size();++index){
        const auto& part=parts[index];const auto name="part-"+std::to_string(index)+".icbrep";
        const auto bytes=iCAX::GeometryData::Persistence::Serialize(part.Geometry);WriteBytes(target/name,bytes);Array contacts;
        for(const auto& point:part.Contacts)contacts.emplace_back(Numbers(point));
        records.emplace_back(Map{{"index",static_cast<UInt>(index)},{"file",name},{"sha256",Digest(bytes)},{"bytes",static_cast<UInt>(bytes.size())},
            {"station",static_cast<UInt>(part.StationIndex)},{"base",part.Base},{"connection",part.Connection},{"frame",Numbers(part.Frame)},
            {"projectionFrame",Numbers(part.ProjectionFrame)},{"thicknessMm",part.Thickness},{"contacts",std::move(contacts)},
            {"supported",part.Supported},{"reportedDistanceMm",part.Distance},{"exactProfile",iCAX::CAD::TubeCadJson::Encode(part.ExactProfile)}});
    }
    Array faceIDs;for(const auto face:input.FaceIDs)faceIDs.emplace_back(std::to_string(face));
    Array stations;for(const auto& station:input.Stations)stations.emplace_back(Map{{"coordinate",station.Coordinate},{"from",station.From},{"to",station.To},
        {"alongX",station.AlongX},{"connection",station.Connection},{"layoutIndex",static_cast<UInt>(station.LayoutIndex)}});
    const Map manifest{{"schema",std::string("icax.door-support-returned-parts.1")},{"auditStatus",std::string("not-started")},
        {"sourceNeutralFile",std::string("source.icbrep")},{"sourcePath",Utf8(source.Path)},{"sourceFileSha256",source.FileDigest},{"sourceCadSha256",Digest(source.Serialized)},
        {"sourceFrame",source.SourceFrame},{"bakedTransform",Numbers(source.BakedTransform)},{"mode",pose.Name},{"transform",Numbers(input.Transform)},
        {"supportDirection",Numbers(input.SupportDirection)},{"selectedFaceIds",std::move(faceIDs)},{"baseDropMm",input.BaseDrop},{"gapMm",input.Gap},
        {"supportCount",static_cast<UInt>(input.SupportCount)},{"connectionCount",static_cast<UInt>(input.ConnectionCount)},
        {"stations",std::move(stations)},{"parts",std::move(records)}};
    const auto path=target/"manifest.json";std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(manifest);
    file.close();Require(file.good(),"Cannot write Door returned-parts manifest: "+Utf8(path));return Utf8(path);
}
Input PlateInput(const Source& source,std::uint64_t face,const Pose& pose){
    Input input;input.Geometry=source.Geometry;input.Transform=Matrix(pose.Transform);input.SupportDirection=pose.Direction;input.FaceIDs={face};
    input.Automatic=true;input.Pattern="Skeleton";input.CountLayout=true;input.SupportCount=7;input.ConnectionCount=3;input.SolidBase=true;
    input.JointMode="TabSlot";input.JointClearance=.2;input.TenonWidth=8;input.TenonPitch=60;input.JointWeb=1.5;input.TenonRadius=1;
    input.WindowMode="Automatic";input.WindowWeb=10;input.BaseShape="Rectangle";
    input.Thickness=3;input.BaseThickness=5;input.BaseDrop=40;input.Margin=15;input.Gap=.2;input.MinimumWeb=5;
    input.AvoidOpeningBoundaries=true;input.OpeningBoundaryMargin=1;input.OpeningReliefDepth=5;input.MaximumSupportSpan=220;
    return input;
}
}

TEST(FiveAxisDoorSupport, DISABLED_RealDoorSevenByThree){
    using namespace door_support_tests;
    Map report{{"schema",std::string("icax.door-support-flattening.1")},{"passed",false},{"probeOnly",Enabled("ICAX_DOOR_SUPPORT_PROBE_ONLY")}};
    try{
        const auto& source=ImportedDoor();report["sourcePath"]=Utf8(source.Path);report["sourceFileSha256"]=source.FileDigest;
        report["sourceFrame"]=source.SourceFrame;report["bakedTransform"]=Numbers(source.BakedTransform);
        report["sourceBoundsMm"]=Numbers(Bounds(source.Native.Shape));
        const auto before=Digest(source.Serialized);report["sourceCadSha256Before"]=before;report["sourceDisplayRecords"]=static_cast<UInt>(source.Geometry->Triangulations3.size());report["sourceMeshTriangles"]=UInt{0};
        Array faces;for(size_t index=0;index<std::min(size_t(10),source.Faces.size());++index){
            faces.emplace_back(FaceValue(source.Faces[index]));
            std::cout<<"Door face "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(FaceValue(source.Faces[index]))<<std::endl;
        }
        report["topFacesByExactBRepArea"]=faces;report["faceCount"]=static_cast<UInt>(source.Faces.size());
        std::uint64_t selected=0;std::string basis;
        if(const auto overrideFace=Environment("ICAX_DOOR_SUPPORT_FACE");!overrideFace.empty()){
            size_t consumed=0;selected=std::stoull(overrideFace,&consumed);Require(consumed==overrideFace.size(),"Door face override is not an integer");
            Require(source.Native.Faces.contains(selected),"Door face override is absent from the imported CAD");basis="explicit ICAX_DOOR_SUPPORT_FACE";
        }else{
            const auto face=std::find_if(source.Faces.begin(),source.Faces.end(),[](const auto& value){return value.NormalDefined&&value.MidpointMaterialNormal[2]<-1e-9;});
            if(face!=source.Faces.end()){selected=face->ID;basis="largest exact BRep area with a downward material-side surface normal at its UV midpoint";}
            else basis="no downward-normal face found; inspect face audit and provide an explicit CAD face ID";
        }
        report["selectedFaceId"]=std::to_string(selected);report["selectionBasis"]=basis;
        if(selected){const auto face=std::find_if(source.Faces.begin(),source.Faces.end(),[&](const auto& value){return value.ID==selected;});report["selectedFace"]=FaceValue(*face);}
        std::cout<<"Door support selectedFaceId="<<selected<<" basis="<<basis<<std::endl;WriteReport(report);
        Array scenarios;
        if(!Enabled("ICAX_DOOR_SUPPORT_PROBE_ONLY")){
            Require(selected!=0,"Door support face must be selected explicitly after probing");
            iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
            const auto service=provider.Resolve<iCAX::Geometry::ISectionPlateService>();Require(!!service,"Door section plate service unavailable");
            for(const auto& pose:Poses(source)){
                Map scenario{{"mode",pose.Name},{"transform",Numbers(Matrix(pose.Transform))},{"supportDirection",Numbers(pose.Direction)},{"passed",false}};
                Array audit;const auto started=std::chrono::steady_clock::now();
                try{
                    auto input=PlateInput(source,selected,pose);input.Stations=service->SuggestStations(input,220);Array stations;
                    for(const auto& station:input.Stations)stations.emplace_back(Map{{"coordinate",station.Coordinate},{"from",station.From},{"to",station.To},{"alongX",station.AlongX},{"connection",station.Connection},{"layoutIndex",static_cast<UInt>(station.LayoutIndex)}});
                    scenario["stations"]=stations;report["activeScenario"]=scenario;WriteReport(report);
                    const auto parts=service->Build(input,{});scenario["buildMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                    if(const auto manifest=SaveReturnedParts(source,input,pose,parts);!manifest.empty())scenario["returnedPartsManifest"]=manifest;
                    report["activeScenario"]=scenario;WriteReport(report);
                    Require(parts.size()==11,"Door result must contain seven supports, three connections and one base");
                    const auto work=source.Native.Shape.Moved(TopLoc_Location(pose.Transform));const auto workBoundary=BoundaryFaces(work);const auto workBounds=ConservativeBounds(work);
                    std::vector<std::pair<TopoDS_Shape,Bnd_Box>> previous;size_t supports=0,connections=0,bases=0;
                    scenario["distanceDomain"]=std::string("conservative BRep lower bounds, exact shared contact upper bounds, or measured complete trimmed boundary distance");
                    const auto stationSetting=Environment("ICAX_DOOR_SUPPORT_AUDIT_STATION");const size_t detailedStation=stationSetting.empty()?0:std::stoull(stationSetting);
                    for(const auto& part:parts){
                        const auto auditStarted=std::chrono::steady_clock::now();
                        const double profileGap=MaximumExactProfileJunctionGap(part);
                        const auto native=iCAX::OpenCascade::ReconstructOpenCascadeShape(part.Geometry);Require(native.bOK,"Door plate BRep reconstruction failed");
                        const bool valid=BRepCheck_Analyzer(native.Shape).IsValid();Require(valid,"Door plate BRep is invalid");
                        Require(part.Geometry.Triangulations3.empty(),"Door intermediate plate contains display triangulation");
                        for(TopExp_Explorer face(native.Shape,TopAbs_FACE);face.More();face.Next()){TopLoc_Location location;Require(BRep_Tool::Triangulation(TopoDS::Face(face.Current()),location).IsNull(),"Door reconstructed plate contains a triangulation");}
                        const auto plateBoundary=BoundaryFaces(native.Shape);const auto plateBounds=ConservativeBounds(native.Shape);
                        const auto distance=AuditDistance(part,plateBoundary,workBoundary,plateBounds,workBounds,input);
                        std::string workProof;const auto overlap=IntersectionVolume(native.Shape,work,plateBounds,workBounds,workProof);double pairOverlap=0;Array pairAudits;
                        for(size_t index=0;index<previous.size();++index){
                            std::string proof;const double volume=IntersectionVolume(native.Shape,previous[index].first,plateBounds,previous[index].second,proof);pairOverlap=std::max(pairOverlap,volume);
                            pairAudits.emplace_back(Map{{"earlierPartIndex",static_cast<UInt>(index)},{"volumeMm3",volume},{"method",proof}});
                        }
                        previous.emplace_back(native.Shape,plateBounds);
                        size_t solidCount=0;for(TopExp_Explorer solid(native.Shape,TopAbs_SOLID);solid.More();solid.Next())++solidCount;
                        GProp_GProps mass;BRepGProp::VolumeProperties(native.Shape,mass);const double partVolume=std::abs(mass.Mass());
                        Require(solidCount==1&&std::isfinite(partVolume)&&partVolume>0,"Door plate must be one positive-volume solid");
                        Map item{{"station",static_cast<UInt>(part.StationIndex)},{"base",part.Base},{"connection",part.Connection},{"validBRep",valid},{"solidCount",static_cast<UInt>(solidCount)},{"volumeMm3",partVolume},
                            {"exactProfileMaximumJunctionGapMm",profileGap},
                            {"distanceEvidence",distance.Data},{"workpieceIntersectionVolumeMm3",overlap},{"workpieceIntersectionMethod",workProof},
                            {"maximumPlateIntersectionVolumeMm3",pairOverlap},{"plateIntersectionAudits",std::move(pairAudits)},
                            {"frame",Numbers(part.Frame)},{"geometrySha256",Digest(iCAX::GeometryData::Persistence::Serialize(part.Geometry))},{"geometryFaceCount",static_cast<UInt>(part.Geometry.Faces.size())},{"displayTriangulations",static_cast<UInt>(part.Geometry.Triangulations3.size())}};
                        if(!part.Base&&part.StationIndex==detailedStation)item["exactProfile"]=iCAX::CAD::TubeCadJson::Encode(part.ExactProfile);
                        item["posteriorAuditMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-auditStarted).count();
                        audit.emplace_back(item);scenario["parts"]=audit;report["activeScenario"]=scenario;WriteReport(report);
                        const auto label="Door station "+std::to_string(part.StationIndex);
                        Require(profileGap<=1e-7,label+" exact manufacturing profile is open");
                        Require(overlap<=1e-7,label+" penetrates the workpiece");Require(pairOverlap<=1e-7,label+" penetrates another plate");
                        if(part.Base){++bases;Require(distance.Verified,label+" violates base clearance");Require(part.Supported,"Door support check is not Supported");}
                        else if(part.Connection){++connections;Require(distance.Verified,label+" violates connection clearance");}
                        else {++supports;Require(distance.Verified,label+" has no exact bearing contact");}
                        std::cout<<"Door posterior audit part "<<(previous.size()-1)<<" station="<<part.StationIndex<<" base="<<part.Base<<" connection="<<part.Connection
                            <<" distanceMethod="<<distance.Data.at("method").To<std::string>()<<" complete in "
                            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-auditStarted).count()<<" ms"<<std::endl;
                    }
                    Require(supports==7&&connections==3&&bases==1,"Door role counts changed");scenario["supportCount"]=static_cast<UInt>(supports);scenario["connectionCount"]=static_cast<UInt>(connections);scenario["baseCount"]=static_cast<UInt>(bases);scenario["passed"]=true;
                }catch(const Standard_Failure& failure){const char* message=failure.what();const std::string error=message?message:"OCCT failure";scenario["error"]=error;ADD_FAILURE()<<pose.Name<<": "<<error;}
                catch(const std::exception& failure){scenario["error"]=std::string(failure.what());ADD_FAILURE()<<pose.Name<<": "<<failure.what();}
                scenario["parts"]=audit;scenario["elapsedMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();scenarios.emplace_back(scenario);report["scenarios"]=scenarios;
                const auto after=iCAX::GeometryData::Persistence::Serialize(*source.Geometry);report["sourceCadSha256After"]=Digest(after);report["sourceCadUnchanged"]=after==source.Serialized;EXPECT_EQ(source.Serialized,after);WriteReport(report);
            }
        }
        const auto after=iCAX::GeometryData::Persistence::Serialize(*source.Geometry);report["sourceCadSha256After"]=Digest(after);report["sourceCadUnchanged"]=after==source.Serialized;
        EXPECT_EQ(source.Serialized,after);report["passed"]=!HasFailure();WriteReport(report);
        std::cout<<"Door support source CAD SHA-256 "<<before<<" -> "<<Digest(after)<<std::endl;
    }catch(const Standard_Failure& failure){const char* message=failure.what();const std::string error=message?message:"OCCT failure";report["error"]=error;ADD_FAILURE()<<error;WriteReport(report);}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();WriteReport(report);}
}

namespace door_support_tests {
TopoDS_Shape ReadStockDump(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary);Require(file.is_open(),"Cannot open stock BRep dump: "+Utf8(path));
    BRep_Builder builder;TopoDS_Shape result;BRepTools::Read(result,file,builder);
    Require(!result.IsNull(),"Cannot read stock BRep dump: "+Utf8(path));return result;
}
Map StockDumpAudit(const TopoDS_Shape& shape){
    Map report{{"null",shape.IsNull()}};if(shape.IsNull())return report;
    report["validBRep"]=BRepCheck_Analyzer(shape).IsValid();Array solids;double volume=0;size_t index=0;
    for(TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next()){
        GProp_GProps properties;BRepGProp::VolumeProperties(solid.Current(),properties);const auto center=properties.CentreOfMass();
        const double signedVolume=properties.Mass(),absoluteVolume=std::abs(signedVolume);volume+=absoluteVolume;
        solids.emplace_back(Map{{"index",static_cast<UInt>(index++)},{"signedVolumeMm3",signedVolume},{"volumeMm3",absoluteVolume},
            {"boundsMm",Numbers(Bounds(solid.Current()))},{"centroidMm",Array{center.X(),center.Y(),center.Z()}},
            {"validBRep",BRepCheck_Analyzer(solid.Current()).IsValid()}});
    }
    report["solidCount"]=static_cast<UInt>(solids.size());report["sumAbsoluteSolidVolumeMm3"]=volume;report["solids"]=std::move(solids);return report;
}
}

TEST(FiveAxisDoorSupport, DISABLED_ReplayStockBooleanDump){
    using namespace door_support_tests;
    Map report{{"schema",std::string("icax.door-stock-boolean-replay.1")},
        {"diagnosticOnly",true},{"nonDestructive",true},{"runParallel",false}};
    try{
        const auto directory=Environment("ICAX_DOOR_STOCK_REPLAY_DIR"),stationText=Environment("ICAX_DOOR_SUPPORT_AUDIT_STATION");
        Require(!directory.empty(),"ICAX_DOOR_STOCK_REPLAY_DIR must name an existing BRep dump directory");
        Require(!stationText.empty()&&std::all_of(stationText.begin(),stationText.end(),[](char value){return value>='0'&&value<='9';}),
            "ICAX_DOOR_SUPPORT_AUDIT_STATION must name the dumped station");
        const auto station=std::stoull(stationText);const auto stem=Utf8Path(directory)/("stock-"+std::to_string(station));
        const auto path=[&](const char* suffix){auto value=stem;value+=suffix;return value;};
        report["station"]=station;report["directory"]=directory;
        report["solidPath"]=Utf8(path("-solid.brep"));report["remainderPath"]=Utf8(path("-remainder.brep"));report["outsidePath"]=Utf8(path("-outside.brep"));
        const auto solid=ReadStockDump(path("-solid.brep")),remainder=ReadStockDump(path("-remainder.brep")),outside=ReadStockDump(path("-outside.brep"));
        report["solid"]=StockDumpAudit(solid);report["remainder"]=StockDumpAudit(remainder);report["originalOutside"]=StockDumpAudit(outside);
        BRepAlgoAPI_Cut operation;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(solid);tools.Append(remainder);
        operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);
        const auto started=std::chrono::steady_clock::now();operation.Build();
        report["recomputeMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        report["isDone"]=operation.IsDone();report["hasErrors"]=operation.HasErrors();report["hasWarnings"]=operation.HasWarnings();
        if(operation.IsDone())report["recomputedOutside"]=StockDumpAudit(operation.Shape());
        // Positive residuals and invalid geometry are diagnostic results. Do
        // not repair shapes, change Boolean tolerances, or assert them away.
    }catch(const Standard_Failure& failure){const char* message=failure.what();const std::string error=message?message:"OCCT stock dump replay failure";report["error"]=error;ADD_FAILURE()<<error;}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    std::cout<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report)<<std::endl;
}

TEST(FiveAxisDoorSupport, DISABLED_ReplayProjectionContour){
    using namespace door_support_tests;
    const int previousStackLength=Standard_Failure::DefaultStackTraceLength();Standard_Failure::SetDefaultStackTraceLength(40);
    Map report{{"schema",std::string("icax.door-projection-contour-replay.1")},{"diagnosticOnly",true},{"stackTraceLength",40}};Array faces;
    const auto failureValue=[](const Standard_Failure& failure){
        const char* message=failure.what();const char* stack=failure.GetStackString();const char* type=failure.ExceptionType();
        std::cerr<<"Door Contap exception type="<<(type?type:"Standard_Failure")<<" cppType="<<typeid(failure).name()
            <<" message="<<(message?message:"")<<std::endl<<(stack?stack:"")<<std::endl;
        return Map{{"exceptionType",std::string(type?type:"Standard_Failure")},{"cppType",std::string(typeid(failure).name())},
            {"message",std::string(message?message:"")},{"stack",std::string(stack?stack:"")}};
    };
    try{
        const auto file=Environment("ICAX_DOOR_PROJECTION_REPLAY_FILE");Require(!file.empty(),"ICAX_DOOR_PROJECTION_REPLAY_FILE must name the original single-face BRep dump");
        report["sourcePath"]=file;const auto shape=ReadStockDump(Utf8Path(file));size_t index=0;
        for(TopExp_Explorer explorer(shape,TopAbs_FACE);explorer.More();explorer.Next()){
            Map item{{"index",static_cast<UInt>(index++)}};std::string phase="surface metadata";
            try{
                const auto face=TopoDS::Face(explorer.Current());double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
                item["faceUVBounds"]=Array{u0,u1,v0,v1};item["orientation"]=static_cast<int>(face.Orientation());
                BRepTopAdaptor_Tool tool(face,Precision::PConfusion());const auto surface=tool.GetSurface();
                item["surfaceType"]=SurfaceName(surface->GetType());item["surfaceTypeCode"]=static_cast<int>(surface->GetType());
                item["adaptorUVBounds"]=Array{surface->FirstUParameter(),surface->LastUParameter(),surface->FirstVParameter(),surface->LastVParameter()};
                if(surface->GetType()==GeomAbs_BSplineSurface||surface->GetType()==GeomAbs_BezierSurface){
                    const bool spline=surface->GetType()==GeomAbs_BSplineSurface;int uPoles=0,vPoles=0;double minimum=std::numeric_limits<double>::max(),maximum=std::numeric_limits<double>::lowest();bool positive=true;
                    if(spline){
                        const auto geometry=surface->BSpline();uPoles=geometry->NbUPoles();vPoles=geometry->NbVPoles();
                        item["uDegree"]=geometry->UDegree();item["vDegree"]=geometry->VDegree();item["uKnotCount"]=geometry->NbUKnots();item["vKnotCount"]=geometry->NbVKnots();
                        Array uKnots,vKnots;for(int i=1;i<=geometry->NbUKnots();++i)uKnots.emplace_back(Map{{"value",geometry->UKnot(i)},{"multiplicity",geometry->UMultiplicity(i)}});
                        for(int i=1;i<=geometry->NbVKnots();++i)vKnots.emplace_back(Map{{"value",geometry->VKnot(i)},{"multiplicity",geometry->VMultiplicity(i)}});
                        item["uKnots"]=std::move(uKnots);item["vKnots"]=std::move(vKnots);
                        for(int u=1;u<=uPoles;++u)for(int v=1;v<=vPoles;++v){const double weight=geometry->Weight(u,v);positive=positive&&std::isfinite(weight)&&weight>0;minimum=std::min(minimum,weight);maximum=std::max(maximum,weight);}
                    }else{
                        const auto geometry=surface->Bezier();uPoles=geometry->NbUPoles();vPoles=geometry->NbVPoles();
                        item["uDegree"]=geometry->UDegree();item["vDegree"]=geometry->VDegree();item["uKnotCount"]=0;item["vKnotCount"]=0;
                        for(int u=1;u<=uPoles;++u)for(int v=1;v<=vPoles;++v){const double weight=geometry->Weight(u,v);positive=positive&&std::isfinite(weight)&&weight>0;minimum=std::min(minimum,weight);maximum=std::max(maximum,weight);}
                    }
                    item["uPoleCount"]=uPoles;item["vPoleCount"]=vPoles;item["allWeightsPositiveAndFinite"]=positive;item["minimumWeight"]=minimum;item["maximumWeight"]=maximum;
                }
                std::cout<<"Door Contap face "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(item)<<std::endl;
                phase="Contap_Contour";const auto started=std::chrono::steady_clock::now();
                Contap_Contour contour(surface,tool.GetTopolTool(),gp_Vec(0,0,1));
                item["elapsedMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();item["isDone"]=contour.IsDone();
                if(contour.IsDone()){
                    item["lineCount"]=contour.NbLines();Array lines;
                    for(int lineIndex=1;lineIndex<=contour.NbLines();++lineIndex){
                        const auto& line=contour.Line(lineIndex);const bool walking=line.TypeContour()==Contap_Walking;
                        const char* type=line.TypeContour()==Contap_Lin?"Line":line.TypeContour()==Contap_Circle?"Circle":walking?"Walking":"Restriction";
                        Map value{{"index",lineIndex},{"type",std::string(type)},{"typeCode",static_cast<int>(line.TypeContour())},{"vertexCount",line.NbVertex()},
                            {"walkingPointCount",walking?line.NbPnts():0},{"nbPntsApplicable",walking}};Array vertices;
                        for(int vertexIndex=1;vertexIndex<=line.NbVertex();++vertexIndex){
                            const auto& vertex=line.Vertex(vertexIndex);double u,v;vertex.Parameters(u,v);const auto& point=vertex.Value();
                            vertices.emplace_back(Map{{"index",vertexIndex},{"lineParameter",vertex.ParameterOnLine()},{"uv",Array{u,v}},
                                {"pointMm",Array{point.X(),point.Y(),point.Z()}},{"onArc",vertex.IsOnArc()},{"internal",vertex.IsInternal()}});
                        }
                        value["vertices"]=std::move(vertices);
                        if(walking&&line.NbPnts()>0){
                            std::array<double,6> xyz{INFINITY,INFINITY,INFINITY,-INFINITY,-INFINITY,-INFINITY};std::array<double,4> uv{INFINITY,INFINITY,-INFINITY,-INFINITY};
                            for(int pointIndex=1;pointIndex<=line.NbPnts();++pointIndex){
                                const auto& point=line.Point(pointIndex);const auto& position=point.Value();double u,v;point.ParametersOnS2(u,v);
                                const std::array<double,3> coordinates{position.X(),position.Y(),position.Z()};for(size_t axis=0;axis<3;++axis){xyz[axis]=std::min(xyz[axis],coordinates[axis]);xyz[axis+3]=std::max(xyz[axis+3],coordinates[axis]);}
                                uv[0]=std::min(uv[0],u);uv[1]=std::min(uv[1],v);uv[2]=std::max(uv[2],u);uv[3]=std::max(uv[3],v);
                            }
                            value["walkingPointBoundsMm"]=Numbers(xyz);value["walkingUVBounds"]=Numbers(uv);
                        }
                        lines.emplace_back(value);
                    }
                    item["lines"]=std::move(lines);
                }else ADD_FAILURE()<<"Door Contap face "<<(index-1)<<" did not complete";
            }catch(const Standard_Failure& failure){item["phase"]=phase;item["failure"]=failureValue(failure);ADD_FAILURE()<<"Door Contap face "<<(index-1)<<": "<<failure.what();}
            catch(const std::exception& failure){item["phase"]=phase;item["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
            faces.emplace_back(item);
        }
        Require(index>0,"Door projection replay contains no face");
    }catch(const Standard_Failure& failure){report["failure"]=failureValue(failure);ADD_FAILURE()<<failure.what();}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    Standard_Failure::SetDefaultStackTraceLength(previousStackLength);report["faces"]=std::move(faces);report["passed"]=!HasFailure();
    std::cout<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report)<<std::endl;
}

namespace door_support_tests {
Array DumpCheckStatuses(const NCollection_List<BRepCheck_Status>& statuses){
    Array result;for(const auto status:statuses){std::ostringstream name;BRepCheck::Print(status,name);result.emplace_back(Map{{"code",static_cast<int>(status)},{"name",name.str()}});}return result;
}
bool DumpHasCheckError(const NCollection_List<BRepCheck_Status>& statuses){
    return std::any_of(statuses.begin(),statuses.end(),[](BRepCheck_Status status){return status!=BRepCheck_NoError;});
}
Map DumpShapeIdentity(const TopoDS_Shape& shape,const ShapeMap& topology){
    return {{"topologyIndex",topology.FindIndex(shape)},{"type",std::string(TopAbs::ShapeTypeToString(shape.ShapeType()))},
        {"orientation",std::string(TopAbs::ShapeOrientationToString(shape.Orientation()))}};
}
}

TEST(FiveAxisDoorSupport, DISABLED_ReadBRepDumpTopology){
    using namespace door_support_tests;
    Map report{{"schema",std::string("icax.door-brep-topology-diagnostic.1")},{"diagnosticOnly",true}};
    try{
        const auto file=Environment("ICAX_DOOR_BREP_DIAGNOSTIC_FILE");Require(!file.empty(),"ICAX_DOOR_BREP_DIAGNOSTIC_FILE must name one existing BRep dump");
        report["sourcePath"]=file;const auto shape=ReadStockDump(Utf8Path(file));ShapeMap topology;TopExp::MapShapes(shape,topology);
        report["root"]=DumpShapeIdentity(shape,topology);BRepCheck_Analyzer analyzer(shape);report["validBRep"]=analyzer.IsValid();
        Map counts;for(int type=static_cast<int>(TopAbs_COMPOUND);type<=static_cast<int>(TopAbs_VERTEX);++type)counts[TopAbs::ShapeTypeToString(static_cast<TopAbs_ShapeEnum>(type))]=0;
        Array errors,solids;double signedVolume=0,absoluteVolume=0;
        std::vector<std::pair<int,ShapeMap>> solidFaces;
        for(int index=1;index<=topology.Extent();++index){
            const auto& subshape=topology(index);const std::string type=TopAbs::ShapeTypeToString(subshape.ShapeType());counts[type]=counts.at(type).To<int>()+1;
            const auto check=analyzer.Result(subshape);
            if(!check.IsNull()){
                Array contexts;check->InitContextIterator();for(;check->MoreShapeInContext();check->NextShapeInContext())if(DumpHasCheckError(check->StatusOnShape())){
                    auto context=DumpShapeIdentity(check->ContextualShape(),topology);context["statuses"]=DumpCheckStatuses(check->StatusOnShape());contexts.emplace_back(context);
                }
                if(DumpHasCheckError(check->Status())||!contexts.empty()){
                    auto issue=DumpShapeIdentity(subshape,topology);issue["statuses"]=DumpCheckStatuses(check->Status());issue["contexts"]=std::move(contexts);errors.emplace_back(issue);
                }
            }
            if(subshape.ShapeType()==TopAbs_SOLID){
                GProp_GProps mass;BRepGProp::VolumeProperties(subshape,mass);const double volume=mass.Mass();signedVolume+=volume;absoluteVolume+=std::abs(volume);
                auto item=DumpShapeIdentity(subshape,topology);item["signedVolumeMm3"]=volume;item["absoluteVolumeMm3"]=std::abs(volume);item["validBRep"]=analyzer.IsValid(subshape);solids.emplace_back(item);
                ShapeMap faces;TopExp::MapShapes(subshape,TopAbs_FACE,faces);solidFaces.emplace_back(index,std::move(faces));
            }
        }
        report["uniqueTopologyCounts"]=counts;report["uniqueTopologyCount"]=topology.Extent();report["checkErrors"]=std::move(errors);
        report["solids"]=std::move(solids);report["sumSignedSolidVolumeMm3"]=signedVolume;report["sumAbsoluteSolidVolumeMm3"]=absoluteVolume;
        Array faces;size_t occurrenceIndex=0;
        for(TopExp_Explorer explorer(shape,TopAbs_FACE);explorer.More();explorer.Next()){
            const auto face=TopoDS::Face(explorer.Current());auto item=DumpShapeIdentity(face,topology);item["occurrenceIndex"]=static_cast<UInt>(occurrenceIndex++);
            try{
                GProp_GProps area;BRepGProp::SurfaceProperties(face,area);item["exactBRepAreaMm2"]=std::abs(area.Mass());item["boundsMm"]=Numbers(Bounds(face));item["validBRep"]=analyzer.IsValid(face);
                const auto check=analyzer.Result(face);if(!check.IsNull())item["statuses"]=DumpCheckStatuses(check->Status());
                Array owners;for(const auto& [solidIndex,ownedFaces]:solidFaces)if(ownedFaces.Contains(face))owners.emplace_back(solidIndex);item["owningSolidTopologyIndices"]=std::move(owners);
                const auto outer=BRepTools::OuterWire(face);Array wires;
                for(TopoDS_Iterator child(face,false,true);child.More();child.Next()){
                    if(child.Value().ShapeType()!=TopAbs_WIRE)continue;const auto wire=TopoDS::Wire(child.Value());auto value=DumpShapeIdentity(wire,topology);
                    value["role"]=std::string(outer.IsNull()?"unclassified":wire.IsSame(outer)?"outer":"inner");
                    value["orientationInFaceOccurrence"]=std::string(TopAbs::ShapeOrientationToString(TopAbs::Compose(face.Orientation(),wire.Orientation())));
                    value["validBRep"]=analyzer.IsValid(wire);const auto wireCheck=analyzer.Result(wire);if(!wireCheck.IsNull())value["statuses"]=DumpCheckStatuses(wireCheck->Status());wires.emplace_back(value);
                }
                item["wireCount"]=static_cast<UInt>(wires.size());item["wires"]=std::move(wires);
            }catch(const Standard_Failure& failure){const char* message=failure.what();item["auditError"]=std::string(message?message:"OCCT face diagnostic failure");}
            catch(const std::exception& failure){item["auditError"]=std::string(failure.what());}
            faces.emplace_back(item);
        }
        report["faceOccurrences"]=std::move(faces);
        // Invalid topology is retained verbatim as diagnostic evidence. This
        // test performs no Boolean, contour calculation, healing or meshing.
    }catch(const Standard_Failure& failure){const char* message=failure.what();report["error"]=std::string(message?message:"OCCT topology dump diagnostic failure");ADD_FAILURE()<<failure.what();}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    std::cout<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report)<<std::endl;
}

namespace door_support_tests {
gp_Trsf ReplayTransform(const std::array<double,16>& matrix){
    gp_Trsf result;result.SetValues(matrix[0],matrix[1],matrix[2],matrix[3],matrix[4],matrix[5],matrix[6],matrix[7],matrix[8],matrix[9],matrix[10],matrix[11]);return result;
}
Map StationShapeSummary(const TopoDS_Shape& shape,bool& valid){
    valid=false;Map report{{"null",shape.IsNull()}};if(shape.IsNull())return report;
    ShapeMap topology;TopExp::MapShapes(shape,topology);BRepCheck_Analyzer analyzer(shape);valid=analyzer.IsValid();
    report["root"]=DumpShapeIdentity(shape,topology);report["validBRep"]=valid;Map counts;Array errors;
    for(int type=static_cast<int>(TopAbs_COMPOUND);type<=static_cast<int>(TopAbs_VERTEX);++type)counts[TopAbs::ShapeTypeToString(static_cast<TopAbs_ShapeEnum>(type))]=0;
    double signedVolume=0,absoluteVolume=0;
    for(int index=1;index<=topology.Extent();++index){
        const auto& current=topology(index);const std::string type=TopAbs::ShapeTypeToString(current.ShapeType());counts[type]=counts.at(type).To<int>()+1;
        if(current.ShapeType()==TopAbs_SOLID){GProp_GProps mass;BRepGProp::VolumeProperties(current,mass);signedVolume+=mass.Mass();absoluteVolume+=std::abs(mass.Mass());}
        const auto check=analyzer.Result(current);if(check.IsNull())continue;Array contexts;
        check->InitContextIterator();for(;check->MoreShapeInContext();check->NextShapeInContext())if(DumpHasCheckError(check->StatusOnShape())){
            auto context=DumpShapeIdentity(check->ContextualShape(),topology);context["statuses"]=DumpCheckStatuses(check->StatusOnShape());contexts.emplace_back(context);
        }
        if(DumpHasCheckError(check->Status())||!contexts.empty()){
            auto issue=DumpShapeIdentity(current,topology);issue["statuses"]=DumpCheckStatuses(check->Status());issue["contexts"]=std::move(contexts);errors.emplace_back(issue);
        }
    }
    report["uniqueTopologyCounts"]=std::move(counts);report["checkErrors"]=std::move(errors);
    report["sumSignedSolidVolumeMm3"]=signedVolume;report["sumAbsoluteSolidVolumeMm3"]=absoluteVolume;return report;
}
struct StationBooleanResult {TopoDS_Shape Shape;Map Data;bool Usable=false;};
template<class Operation> StationBooleanResult ReplayStationBoolean(const std::string& name,const TopoDS_Shape& a,const TopoDS_Shape& b){
    StationBooleanResult result;result.Data["operation"]=name;std::cout<<"Door station replay starting "<<name<<std::endl;
    const auto started=std::chrono::steady_clock::now();
    try{
        Operation operation;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(a);tools.Append(b);
        operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
        result.Data["isDone"]=operation.IsDone();result.Data["hasErrors"]=operation.HasErrors();result.Data["hasWarnings"]=operation.HasWarnings();
        if(operation.IsDone()){
            result.Shape=operation.Shape();bool valid=false;result.Data["result"]=StationShapeSummary(result.Shape,valid);result.Usable=valid&&!operation.HasErrors();
        }
    }catch(const Standard_Failure& failure){const char* message=failure.what();result.Data["error"]=std::string(message?message:"OCCT station Boolean failure");result.Data["exceptionType"]=std::string(failure.ExceptionType());}
    catch(const std::exception& failure){result.Data["error"]=std::string(failure.what());}
    result.Data["usableAsOperand"]=result.Usable;result.Data["elapsedMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    std::cout<<"Door station replay finished "<<name<<' '<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(result.Data)<<std::endl;return result;
}
Map ReplayPCurveMetadata(const occ::handle<Geom2d_Curve>& curve,double first,double last){
    Geom2dAdaptor_Curve adaptor(curve,first,last);Map result{{"typeCode",static_cast<int>(adaptor.GetType())},{"class",std::string(curve->DynamicType()->Name())},
        {"parameterRange",Array{first,last}},{"controlPoleRangeAvailable",false}};
    const auto poles=[&](const auto& geometry){
        double u0=std::numeric_limits<double>::max(),v0=u0,u1=std::numeric_limits<double>::lowest(),v1=u1;
        double w0=std::numeric_limits<double>::max(),w1=std::numeric_limits<double>::lowest();bool positive=true;
        for(int index=1;index<=geometry->NbPoles();++index){const auto point=geometry->Pole(index);u0=std::min(u0,point.X());u1=std::max(u1,point.X());v0=std::min(v0,point.Y());v1=std::max(v1,point.Y());
            const double weight=geometry->Weight(index);w0=std::min(w0,weight);w1=std::max(w1,weight);positive=positive&&std::isfinite(weight)&&weight>0;}
        result["controlPoleRangeAvailable"]=true;result["poleCount"]=geometry->NbPoles();result["uPoleRange"]=Array{u0,u1};result["vPoleRange"]=Array{v0,v1};
        result["allPoleUExactlyEqual"]=u0==u1;result["allPoleVExactlyEqual"]=v0==v1;result["allWeightsPositiveAndFinite"]=positive;result["weightRange"]=Array{w0,w1};
    };
    if(adaptor.GetType()==GeomAbs_BSplineCurve)poles(adaptor.BSpline());
    else if(adaptor.GetType()==GeomAbs_BezierCurve)poles(adaptor.Bezier());
    else if(adaptor.GetType()==GeomAbs_Line){const auto line=adaptor.Line();result["lineOriginUV"]=Array{line.Location().X(),line.Location().Y()};result["lineDirectionUV"]=Array{line.Direction().X(),line.Direction().Y()};}
    return result;
}
TopoDS_Face ReplaySupportSurface(const TopoDS_Face& face){
    size_t wires=0;for(TopExp_Explorer wire(face,TopAbs_WIRE);wire.More();wire.Next())++wires;if(wires==1)return face;
    auto local=face;const auto location=face.Location();local.Location(TopLoc_Location());const auto outer=BRepTools::OuterWire(local);
    Require(!outer.IsNull(),"Door replay support surface has no outer boundary");auto ceiling=TopoDS::Face(local.EmptyCopied());BRep_Builder builder;builder.Add(ceiling,outer);ceiling.Location(location);return ceiling;
}
Map ReplaySplineEdgeAudit(const TopoDS_Shape& shape,const std::array<double,6>& overlapBounds,double neighborhood){
    const double sectionX=(overlapBounds[0]+overlapBounds[3])*.5;const occ::handle<Geom_Plane> plane=new Geom_Plane(gp_Pnt(sectionX,0,0),gp_Dir(1,0,0));Array edges;size_t faceIndex=0;
    for(TopExp_Explorer faces(shape,TopAbs_FACE);faces.More();faces.Next(),++faceIndex){
        const auto face=TopoDS::Face(faces.Current());BRepAdaptor_Surface surface(face,true);if(surface.GetType()!=GeomAbs_BSplineSurface)continue;size_t edgeIndex=0;
        for(TopExp_Explorer explorer(face,TopAbs_EDGE);explorer.More();explorer.Next(),++edgeIndex){
            const auto edge=TopoDS::Edge(explorer.Current());const auto bounds=Bounds(edge);double vertexTolerance=0;Array vertices;
            for(TopExp_Explorer vertex(edge,TopAbs_VERTEX);vertex.More();vertex.Next()){
                const double tolerance=BRep_Tool::Tolerance(TopoDS::Vertex(vertex.Current()));vertexTolerance=std::max(vertexTolerance,tolerance);vertices.emplace_back(tolerance);
            }
            const double edgeTolerance=BRep_Tool::Tolerance(edge),padding=std::max({neighborhood,edgeTolerance,vertexTolerance});bool relevant=true;
            for(size_t axis=0;axis<3;++axis)if(bounds[axis]>overlapBounds[axis+3]+padding||bounds[axis+3]<overlapBounds[axis]-padding)relevant=false;
            if(!relevant)continue;Map item{{"faceIndex",static_cast<UInt>(faceIndex)},{"edgeIndex",static_cast<UInt>(edgeIndex)},{"boundsMm",Numbers(bounds)},
                {"edgeToleranceMm",edgeTolerance},{"vertexTolerancesMm",std::move(vertices)},{"maximumVertexToleranceMm",vertexTolerance},
                {"sameParameter",BRep_Tool::SameParameter(edge)},{"sameRange",BRep_Tool::SameRange(edge)},{"neighborhoodPaddingMm",padding}};
            try{
                double first,last,pFirst,pLast;const auto spatial=BRep_Tool::Curve(edge,first,last);const auto pcurve=BRep_Tool::CurveOnSurface(edge,face,pFirst,pLast);
                item["has3DCurve"]=!spatial.IsNull();item["hasPCurve"]=!pcurve.IsNull();
                if(!spatial.IsNull()){
                    item["curve3DClass"]=std::string(spatial->DynamicType()->Name());item["curve3DTypeCode"]=static_cast<int>(GeomAdaptor_Curve(spatial,first,last).GetType());item["curve3DParameterRange"]=Array{first,last};
                }
                if(!pcurve.IsNull())item["pcurve"]=ReplayPCurveMetadata(pcurve,pFirst,pLast);
                if(!spatial.IsNull()&&!pcurve.IsNull()){
                    GeomAPI_IntCS intersection(new Geom_TrimmedCurve(spatial,first,last),plane);item["planeIntersectionDone"]=intersection.IsDone();Array points;
                    if(intersection.IsDone()){
                        item["planeIntersectionSegments"]=intersection.NbSegments();
                        for(int index=1;index<=intersection.NbPoints();++index){
                            double planeU,planeV,parameter;intersection.Parameters(index,planeU,planeV,parameter);const auto curvePoint=spatial->Value(parameter);const auto uv=pcurve->Value(parameter);const auto surfacePoint=surface.Value(uv.X(),uv.Y());
                            const gp_Vec difference(curvePoint,surfacePoint);const auto hit=intersection.Point(index);
                            points.emplace_back(Map{{"parameter",parameter},{"inPCurveRange",parameter>=pFirst&&parameter<=pLast},{"uv",Array{uv.X(),uv.Y()}},
                                {"curve3DPointMm",Array{curvePoint.X(),curvePoint.Y(),curvePoint.Z()}},{"surfacePCurvePointMm",Array{surfacePoint.X(),surfacePoint.Y(),surfacePoint.Z()}},
                                {"intersectionPointMm",Array{hit.X(),hit.Y(),hit.Z()}},{"surfaceMinusCurveMm",Array{difference.X(),difference.Y(),difference.Z()}},{"differenceMm",curvePoint.Distance(surfacePoint)}});
                        }
                    }
                    item["intersectionPoints"]=std::move(points);
                }
            }catch(const Standard_Failure& failure){const char* message=failure.what();item["auditError"]=std::string(message?message:"OCCT edge representation diagnostic failure");}
            catch(const std::exception& failure){item["auditError"]=std::string(failure.what());}
            std::cout<<"Door station edge audit "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(item)<<std::endl;edges.emplace_back(item);
        }
    }
    return {{"sectionPlaneXmm",sectionX},{"overlapBoundsMm",Numbers(overlapBounds)},{"edges",std::move(edges)}};
}
}

TEST(FiveAxisDoorSupport, DISABLED_ReplayDoorStationBoolean){
    using namespace door_support_tests;
    Map report{{"schema",std::string("icax.door-station-boolean-replay.1")},{"diagnosticOnly",true},{"nonDestructive",true},{"runParallel",false}};const Source* source=nullptr;
    try{
        const auto directory=Environment("ICAX_DOOR_STOCK_REPLAY_DIR"),stationText=Environment("ICAX_DOOR_SUPPORT_AUDIT_STATION");
        Require(!directory.empty(),"ICAX_DOOR_STOCK_REPLAY_DIR must name the original stock dump directory");
        Require(!stationText.empty()&&std::all_of(stationText.begin(),stationText.end(),[](char value){return value>='0'&&value<='9';}),"ICAX_DOOR_SUPPORT_AUDIT_STATION must name the dumped station");
        const auto stationIndex=std::stoull(stationText);double padding=0;
        if(const auto value=Environment("ICAX_DOOR_REPLAY_SLAB_PADDING");!value.empty()){
            size_t consumed=0;padding=std::stod(value,&consumed);Require(consumed==value.size()&&std::isfinite(padding)&&padding>=0,"ICAX_DOOR_REPLAY_SLAB_PADDING must be a finite nonnegative millimetre value");
        }
        const bool edgeAuditOnly=Enabled("ICAX_DOOR_REPLAY_EDGE_AUDIT_ONLY");Require(!edgeAuditOnly||padding==0,"Edge-only diagnostics require the original tight slab padding 0");
        report["station"]=stationIndex;report["slabPaddingMm"]=padding;report["edgeAuditOnly"]=edgeAuditOnly;
        const auto platePath=Utf8Path(directory)/("stock-"+std::to_string(stationIndex)+"-solid.brep");report["platePath"]=Utf8(platePath);
        const auto plate=ReadStockDump(platePath);bool plateValid=false;report["inputP"]=StationShapeSummary(plate,plateValid);
        std::cout<<"Door station replay importing source BRep"<<std::endl;source=&ImportedDoor();
        report["sourcePath"]=Utf8(source->Path);report["sourceFileSha256"]=source->FileDigest;report["sourceFrame"]=source->SourceFrame;
        report["bakedTransform"]=Numbers(source->BakedTransform);report["sourceCadSha256Before"]=Digest(source->Serialized);
        const auto poses=Poses(*source);Require(poses.size()==1,"Station replay requires one explicit pose, not a pose matrix");
        std::uint64_t selected=0;
        if(const auto faceSetting=Environment("ICAX_DOOR_SUPPORT_FACE");!faceSetting.empty()){
            size_t consumed=0;selected=std::stoull(faceSetting,&consumed);Require(consumed==faceSetting.size()&&source->Native.Faces.contains(selected),"Door station replay selected face ID is invalid");
        }else{
            const auto face=std::find_if(source->Faces.begin(),source->Faces.end(),[](const auto& value){return value.NormalDefined&&value.MidpointMaterialNormal[2]<-1e-9;});
            Require(face!=source->Faces.end(),"Door station replay requires an explicit supporting CAD face ID");selected=face->ID;
        }
        auto input=PlateInput(*source,selected,poses.front());iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        const auto service=provider.Resolve<iCAX::Geometry::ISectionPlateService>();Require(!!service,"Door section plate service unavailable");input.Stations=service->SuggestStations(input,220);
        Require(stationIndex<input.Stations.size(),"Door station index is absent from the derived station layout");const auto& station=input.Stations[static_cast<size_t>(stationIndex)];
        // Derive the same orthonormal support frame as PlateDirectionFrame,
        // then the station frame/floor used by Section. No failing coordinates
        // or source-file-specific station locations are embedded here.
        double scale=0;for(const double value:input.SupportDirection)scale=std::max(scale,std::abs(value));Require(scale>0,"Door support direction is zero");
        gp_Vec z(input.SupportDirection[0]/scale,input.SupportDirection[1]/scale,input.SupportDirection[2]/scale);z.Normalize();
        gp_Vec x(1,0,0);x-=z.Multiplied(x.Dot(z));if(x.SquareMagnitude()<1e-12){x=gp_Vec(0,1,0);x-=z.Multiplied(x.Dot(z));}x.Normalize();const auto y=z.Crossed(x);
        gp_Trsf supportToWorld;supportToWorld.SetValues(x.X(),y.X(),z.X(),0,x.Y(),y.Y(),z.Y(),0,x.Z(),y.Z(),z.Z(),0);
        const auto alignedTransform=supportToWorld.Inverted()*ReplayTransform(input.Transform);const auto aligned=source->Native.Shape.Moved(TopLoc_Location(alignedTransform));const auto alignedBounds=Bounds(aligned);const double floor=alignedBounds[2]-input.BaseDrop;
        const std::array<double,16> frame=station.AlongX?std::array<double,16>{0,0,1,station.Coordinate-input.Thickness/2,1,0,0,0,0,1,0,floor,0,0,0,1}:
            std::array<double,16>{1,0,0,0,0,0,-1,station.Coordinate+input.Thickness/2,0,1,0,floor,0,0,0,1};
        BRepBuilderAPI_Copy copy(aligned,true,false);Require(copy.IsDone(),"Door station local BRep copy failed");const auto work=copy.Shape().Moved(TopLoc_Location(ReplayTransform(frame).Inverted()));
        const auto bounds=Bounds(work);bool workValid=false;report["localWorkpiece"]=StationShapeSummary(work,workValid);
        report["selectedFaceId"]=std::to_string(selected);report["mode"]=poses.front().Name;report["placementTransform"]=Numbers(input.Transform);report["supportDirection"]=Numbers(input.SupportDirection);
        report["floorMm"]=floor;report["stationFrame"]=Numbers(frame);report["stationCoordinate"]=station.Coordinate;report["stationAlongX"]=station.AlongX;report["stationConnection"]=station.Connection;
        report["localWorkpieceBoundsMm"]=Numbers(bounds);report["inputPBoundsMm"]=Numbers(Bounds(plate));report["thicknessMm"]=input.Thickness;
        const auto slab=BRepPrimAPI_MakeBox(gp_Pnt(bounds[0]-1,bounds[1]-1,-padding),bounds[3]-bounds[0]+2,bounds[4]-bounds[1]+2,input.Thickness+2*padding).Shape();
        if(workValid){
            const auto clipped=ReplayStationBoolean<BRepAlgoAPI_Common>("Common(W,stationSlab)",work,slab);report["workpieceSlab"]=clipped.Data;
            if(edgeAuditOnly){
                const auto overlapPath=Utf8Path(directory)/("stock-"+std::to_string(stationIndex)+"-overlap.brep");report["overlapPath"]=Utf8(overlapPath);
                const auto overlapBounds=Bounds(ReadStockDump(overlapPath));if(clipped.Usable)report["tightWorkpieceEdgeAudit"]=ReplaySplineEdgeAudit(clipped.Shape,overlapBounds,input.Gap);
                else report["tightWorkpieceEdgeAudit"]=Map{{"skipped",std::string("tight workpiece clip is not a completed valid BRep")}};
                const auto selectedOccurrence=source->Native.Faces.at(selected).Moved(TopLoc_Location(alignedTransform));
                const auto copiedSelected=copy.ModifiedShape(selectedOccurrence);Require(!copiedSelected.IsNull(),"Door replay selected source face has no copy mapping");
                const auto localSelected=TopoDS::Face(copiedSelected.Moved(TopLoc_Location(ReplayTransform(frame).Inverted())));
                const auto ceiling=ReplaySupportSurface(localSelected);BRep_Builder builder;TopoDS_Compound ceilingSource;builder.MakeCompound(ceilingSource);builder.Add(ceilingSource,ceiling);
                const auto ceilingClip=ReplayStationBoolean<BRepAlgoAPI_Common>("Common(original outer-wire ceiling,tightSlab)",ceilingSource,slab);report["ceilingSlab"]=ceilingClip.Data;
                if(ceilingClip.Usable)report["ceilingEdgeAudit"]=ReplaySplineEdgeAudit(ceilingClip.Shape,overlapBounds,input.Gap);
                else report["ceilingEdgeAudit"]=Map{{"skipped",std::string("ceiling clip is not a completed valid BRep")}};
                report["plateOperationsSkipped"]=std::string("edge-audit-only mode; no Common(P,Wslab) or Cut operations");
            }else if(plateValid&&clipped.Usable){
                // These paths consume the same valid operands independently;
                // a failed Common result is never supplied to the Cut path.
                const auto direct=ReplayStationBoolean<BRepAlgoAPI_Common>("Common(P,Wslab)",plate,clipped.Shape);report["directOverlap"]=direct.Data;
                auto remainder=ReplayStationBoolean<BRepAlgoAPI_Cut>("Cut(P,Wslab)",plate,clipped.Shape);Array identities;ShapeMap inputSolids;TopExp::MapShapes(plate,TopAbs_SOLID,inputSolids);
                if(!remainder.Shape.IsNull())for(TopExp_Explorer solid(remainder.Shape,TopAbs_SOLID);solid.More();solid.Next()){
                    bool equalToInputSolid=false;for(int index=1;index<=inputSolids.Extent();++index)equalToInputSolid=equalToInputSolid||solid.Current().IsEqual(inputSolids(index));
                    identities.emplace_back(Map{{"isEqualInputP",solid.Current().IsEqual(plate)},{"isEqualAnInputSolid",equalToInputSolid}});
                }
                remainder.Data["resultSolidsInputIdentity"]=std::move(identities);report["remainder"]=remainder.Data;
                if(remainder.Usable)report["differenceIdentityOverlap"]=ReplayStationBoolean<BRepAlgoAPI_Cut>("Cut(P,Cut(P,Wslab))",plate,remainder.Shape).Data;
                else report["differenceIdentityOverlap"]=Map{{"skipped",std::string("remainder was not a completed valid Boolean result")}};
            }else report["plateOperationsSkipped"]=std::string("P or independently clipped Wslab is invalid");
        }else report["slabOperationSkipped"]=std::string("local workpiece is invalid");
    }catch(const Standard_Failure& failure){const char* message=failure.what();report["error"]=std::string(message?message:"OCCT Door station replay failure");ADD_FAILURE()<<failure.what();}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    if(source){const auto after=iCAX::GeometryData::Persistence::Serialize(*source->Geometry);report["sourceCadSha256After"]=Digest(after);report["sourceCadUnchanged"]=after==source->Serialized;EXPECT_EQ(source->Serialized,after);}
    std::cout<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report)<<std::endl;
}

TEST(FiveAxisDoorSupport, NonIsoSplinePublicServicePreservesExactFrontBearing){
    using namespace door_support_tests;
    // Tensor-product degree-2 Bernstein coefficients for
    // S(u,v)=(100u+2v^2,100v,40+10u^2+5uv), 0<=u,v<=1.
    // u has coefficients i/2, u^2 has [0,0,1], and uv has ij/4.
    NCollection_Array2<gp_Pnt> poles(1,3,1,3);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)
        poles.SetValue(i+1,j+1,gp_Pnt(50.*i+(j==2?2.:0.),50.*j,40.+(i==2?10.:0.)+1.25*i*j));
    NCollection_Array1<double> knots(1,2);knots.SetValue(1,0);knots.SetValue(2,1);
    NCollection_Array1<int> multiplicities(1,2);multiplicities.Init(3);
    const occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,knots,knots,multiplicities,multiplicities,2,2);
    const std::array<double,3> internalUKnots{.47,.475,.48},internalVKnots{.25,.5,.75};
    for(const bool insertKnots:{false,true}){
    SCOPED_TRACE(insertKnots?"same surface after exact knot insertion":"original Bernstein representation");
    const auto representation=occ::down_cast<Geom_BSplineSurface>(surface->Copy());
    if(insertKnots){
        for(const double knot:internalUKnots)representation->InsertUKnot(knot,1,0);
        for(const double knot:internalVKnots)representation->InsertVKnot(knot,1,0);
    }
    BRepBuilderAPI_MakeFace face(representation,0,1,0,1,1e-9);ASSERT_TRUE(face.IsDone());
    const auto originalSolid=BRepPrimAPI_MakePrism(face.Face(),gp_Vec(0,0,1)).Shape();ASSERT_TRUE(BRepCheck_Analyzer(originalSolid).IsValid());
    const auto geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepGeometry(originalSolid,"Non-isoparametric support regression","generated"));
    ASSERT_TRUE(geometry->Triangulations3.empty());const auto before=iCAX::GeometryData::Persistence::Serialize(*geometry);const auto beforeHash=Digest(before);
    const auto source=iCAX::OpenCascade::ReconstructOpenCascadeShape(*geometry);ASSERT_TRUE(source.bOK);ASSERT_TRUE(BRepCheck_Analyzer(source.Shape).IsValid());
    std::uint64_t bottomFaceID=0;
    for(const auto& [id,canonical]:source.Faces){
        for(TopExp_Explorer occurrence(source.Shape,TopAbs_FACE);occurrence.More();occurrence.Next()){
            if(!occurrence.Current().IsSame(canonical))continue;const auto actual=TopoDS::Face(occurrence.Current());BRepAdaptor_Surface adaptor(actual,true);
            if(adaptor.GetType()!=GeomAbs_BSplineSurface)continue;double u0,u1,v0,v1;BRepTools::UVBounds(actual,u0,u1,v0,v1);
            BRepLProp_SLProps properties(adaptor,(u0+u1)*.5,(v0+v1)*.5,1,1e-12);ASSERT_TRUE(properties.IsNormalDefined());auto normal=properties.Normal();
            if(actual.Orientation()==TopAbs_REVERSED)normal.Reverse();
            if(normal.Z()<-.5){ASSERT_EQ(std::uint64_t{0},bottomFaceID)<<"More than one downward spline cap";bottomFaceID=id;}
        }
    }
    ASSERT_NE(std::uint64_t{0},bottomFaceID);
    Input input;input.Geometry=geometry;input.Transform=Matrix(gp_Trsf{});input.FaceIDs={bottomFaceID};
    input.Stations={{.Coordinate=50,.From=0,.To=100,.AlongX=true}};input.Automatic=false;input.Thickness=3;input.BaseDrop=20;
    input.JointMode="Weld";input.WindowMode="None";input.SolidBase=true;input.AvoidOpeningBoundaries=false;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<iCAX::Geometry::ISectionPlateService>();ASSERT_TRUE(service);const auto parts=service->Build(input,{});ASSERT_EQ(size_t{2},parts.size());
    TopoDS_Shape support;size_t supportCount=0;
    for(const auto& part:parts){
        ASSERT_TRUE(part.Geometry.Triangulations3.empty());for(const auto& record:part.Geometry.Faces)EXPECT_EQ(std::uint64_t{0},record.Triangulation3Id);
        const auto native=iCAX::OpenCascade::ReconstructOpenCascadeShape(part.Geometry);ASSERT_TRUE(native.bOK);ASSERT_TRUE(BRepCheck_Analyzer(native.Shape).IsValid());
        ShapeMap solids;TopExp::MapShapes(native.Shape,TopAbs_SOLID,solids);ASSERT_EQ(1,solids.Extent());
        for(TopExp_Explorer occurrence(native.Shape,TopAbs_FACE);occurrence.More();occurrence.Next()){
            TopLoc_Location location;EXPECT_TRUE(BRep_Tool::Triangulation(TopoDS::Face(occurrence.Current()),location).IsNull());
        }
        const auto overlap=ValidCommon(native.Shape,source.Shape);double volume=0;
        for(TopExp_Explorer solid(overlap,TopAbs_SOLID);solid.More();solid.Next()){GProp_GProps mass;BRepGProp::VolumeProperties(solid.Current(),mass);volume+=std::abs(mass.Mass());}
        EXPECT_LE(volume,1e-7)<<"Generated plate penetrates the original analytic workpiece";
        if(!part.Base&&!part.Connection){support=native.Shape;++supportCount;}
    }
    ASSERT_EQ(size_t{1},supportCount);ASSERT_FALSE(support.IsNull());const auto boundary=BoundaryFaces(support);
    const double front=50-input.Thickness/2;
    std::vector<double> contactParameters{.25,.5,.75};
    if(insertKnots){
        constexpr double side=1e-6;
        for(const double knot:internalVKnots){contactParameters.push_back(knot-side);contactParameters.push_back(knot+side);}
        // The unchanged analytic equation gives each U-knot crossing exactly:
        // front=100u+2v^2. Check both sides independently of generated edges.
        for(const double knot:internalUKnots){
            const double crossing=std::sqrt((front-100*knot)/2);
            contactParameters.push_back(crossing-side);contactParameters.push_back(crossing);contactParameters.push_back(crossing+side);
        }
    }
    for(const double v:contactParameters){
        const double u=(front-2*v*v)/100,y=100*v,z=40+10*u*u+5*u*v;SCOPED_TRACE(v);
        const auto expected=BRepBuilderAPI_MakeVertex(gp_Pnt(front,y,z)).Vertex();
        EXPECT_LE(BoundaryDistance(expected,boundary),1e-7)<<"Public support BRep departed from the original surface equation";
    }
    const auto after=iCAX::GeometryData::Persistence::Serialize(*geometry);EXPECT_EQ(beforeHash,Digest(after));EXPECT_EQ(before,after);
    }
}

TEST(FiveAxisDoorSupport, NarrowLowAmplitudeInteriorMinimumBeatsBothThicknessCaps){
    using namespace door_support_tests;
    // X=100u, Y=100v. The single degree-2 basis N_3 has support
    // [.497,.503] and N_3(.5)=3/4. Thus
    // Z=40+.00001u-.00008*N_3(u) is monotone outside this narrow
    // trough. Its interior maximum/minimum pair cannot be replaced by
    // a global quadratic silhouette outside the 0.6 mm dip.
    constexpr double center=.5,halfKnotStep=.002,slope=.00001,amplitude=.00008;
    const std::array<double,10> expanded{0,0,0,.497,.499,.501,.503,1,1,1};
    NCollection_Array1<double> uk(1,6),vk(1,2);NCollection_Array1<int> um(1,6),vm(1,2);
    const std::array<double,6> unique{0,.497,.499,.501,.503,1};
    for(int i=1;i<=6;++i){uk(i)=unique[static_cast<size_t>(i-1)];um(i)=i==1||i==6?3:1;}vk(1)=0;vk(2)=1;vm.Init(2);
    NCollection_Array2<gp_Pnt> poles(1,7,1,2);
    for(int i=0;i<7;++i)for(int j=0;j<2;++j){
        const double a=expanded[static_cast<size_t>(i+1)],b=expanded[static_cast<size_t>(i+2)];
        // Greville coefficients reproduce u exactly;
        // changing the one Z coefficient adds the known basis N_3.
        poles(i+1,j+1)=gp_Pnt(50*(a+b),100.*j,40+slope*(a+b)*.5-(i==3?amplitude:0.));
    }
    const occ::handle<Geom_BSplineSurface> original=new Geom_BSplineSurface(poles,uk,vk,um,vm,2,1);
    // In the central knot cell N_3=.75-((u-center)/halfKnotStep)^2.
    // The exact stationary point follows from slope+2*A*(u-c)/h^2=0.
    constexpr double minimumU=center-slope*halfKnotStep*halfKnotStep/(2*amplitude);
    constexpr double displacement=(minimumU-center)/halfKnotStep;
    constexpr double minimum=40+slope*minimumU-amplitude*(.75-displacement*displacement);
    constexpr double capFront=40+slope*.485,capBack=40+slope*.515;
    for(const bool insertKnots:{false,true}){
        SCOPED_TRACE(insertKnots?"exact inserted knots inside trough and thickness":"original narrow trough representation");
        const auto surface=occ::down_cast<Geom_BSplineSurface>(original->Copy());
        if(insertKnots){for(const double knot:{.498,.5,.502})surface->InsertUKnot(knot,1,0);for(const double knot:{.25,.75})surface->InsertVKnot(knot,1,0);}
        EXPECT_NEAR(surface->Value(minimumU,.5).Z(),minimum,1e-12);EXPECT_NEAR(surface->Value(.485,.5).Z(),capFront,1e-12);EXPECT_NEAR(surface->Value(.515,.5).Z(),capBack,1e-12);
        const auto solid=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(surface,0,1,0,1,1e-9).Face(),gp_Vec(0,0,1)).Shape();ASSERT_TRUE(BRepCheck_Analyzer(solid).IsValid());
        const auto geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepGeometry(solid,"Low amplitude interior minimum","generated"));
        ASSERT_TRUE(geometry->Triangulations3.empty());const auto before=iCAX::GeometryData::Persistence::Serialize(*geometry);const auto beforeHash=Digest(before);
        const auto source=iCAX::OpenCascade::ReconstructOpenCascadeShape(*geometry);ASSERT_TRUE(source.bOK);ASSERT_TRUE(BRepCheck_Analyzer(source.Shape).IsValid());std::uint64_t bottomFaceID=0;
        for(const auto& [id,canonical]:source.Faces)for(TopExp_Explorer occurrence(source.Shape,TopAbs_FACE);occurrence.More();occurrence.Next()){
            if(!occurrence.Current().IsSame(canonical))continue;const auto face=TopoDS::Face(occurrence.Current());BRepAdaptor_Surface surfaceAdaptor(face,true);
            if(surfaceAdaptor.GetType()!=GeomAbs_BSplineSurface)continue;double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            BRepLProp_SLProps properties(surfaceAdaptor,(u0+u1)*.5,(v0+v1)*.5,1,1e-12);ASSERT_TRUE(properties.IsNormalDefined());auto normal=properties.Normal();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
            if(normal.Z()<-.5){ASSERT_EQ(std::uint64_t{0},bottomFaceID);bottomFaceID=id;}
        }
        ASSERT_NE(std::uint64_t{0},bottomFaceID);Input input;input.Geometry=geometry;input.Transform=Matrix(gp_Trsf{});input.FaceIDs={bottomFaceID};
        // AlongX means the extrusion thickness is physical X=[48.5,51.5],
        // while the manufacturing profile spans physical Y=[0,100].
        input.Stations={{.Coordinate=50,.From=0,.To=100,.AlongX=true}};input.Automatic=false;input.Thickness=3;input.BaseDrop=20;
        input.JointMode="Weld";input.WindowMode="None";input.SolidBase=true;input.AvoidOpeningBoundaries=false;
        iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        const auto service=provider.Resolve<iCAX::Geometry::ISectionPlateService>();ASSERT_TRUE(service);const auto parts=service->Build(input,{});TopoDS_Shape support;
        for(const auto& part:parts){
            ASSERT_TRUE(part.Geometry.Triangulations3.empty());const auto native=iCAX::OpenCascade::ReconstructOpenCascadeShape(part.Geometry);ASSERT_TRUE(native.bOK);ASSERT_TRUE(BRepCheck_Analyzer(native.Shape).IsValid());
            ShapeMap solids;TopExp::MapShapes(native.Shape,TopAbs_SOLID,solids);ASSERT_EQ(1,solids.Extent());
            for(TopExp_Explorer face(native.Shape,TopAbs_FACE);face.More();face.Next()){TopLoc_Location location;EXPECT_TRUE(BRep_Tool::Triangulation(TopoDS::Face(face.Current()),location).IsNull());}
            const auto overlap=ValidCommon(native.Shape,source.Shape);double volume=0;
            for(TopExp_Explorer component(overlap,TopAbs_SOLID);component.More();component.Next()){GProp_GProps mass;BRepGProp::VolumeProperties(component.Current(),mass);volume+=std::abs(mass.Mass());}
            EXPECT_LE(volume,1e-7)<<"A cap-only profile must not penetrate the interior trough";
            if(!part.Base&&!part.Connection){ASSERT_TRUE(support.IsNull());support=native.Shape;}
        }
        ASSERT_FALSE(support.IsNull());const auto boundary=BoundaryFaces(support);
        for(const double v:{.1,.25,.5,.75,.9}){SCOPED_TRACE(v);const auto expected=BRepBuilderAPI_MakeVertex(gp_Pnt(100*minimumU,100*v,minimum)).Vertex();EXPECT_LE(BoundaryDistance(expected,boundary),1e-7);}
        const auto after=iCAX::GeometryData::Persistence::Serialize(*geometry);EXPECT_EQ(beforeHash,Digest(after));EXPECT_EQ(before,after);
    }
}

namespace door_support_tests {
Map DumpSurfaceStationRoot(const TopoDS_Face& face,const gp_Pnt2d& seed,double sectionX,double depth,bool critical,double capLow,double capHigh){
    BRepAdaptor_Surface surface(face,false);const double u0=surface.FirstUParameter(),u1=surface.LastUParameter(),v0=surface.FirstVParameter(),v1=surface.LastVParameter();
    Map report{{"equations",std::string(critical?"S.X=x, (Su cross Sv).Z=0":"S.X=x, S.Z=cap")},{"seedUV",Array{seed.X(),seed.Y()}},
        {"naturalUVBounds",Array{u0,u1,v0,v1}},{"converged",false}};
    if(!critical)report["capDepthMm"]=depth;
    const auto inDomain=[&](const gp_Pnt2d& uv){return std::isfinite(uv.X())&&std::isfinite(uv.Y())&&uv.X()>=u0&&uv.X()<=u1&&uv.Y()>=v0&&uv.Y()<=v1;};
    struct Evaluation {gp_Pnt Point;double F1=0,F2=0,A=0,B=0,C=0,D=0,SecondScale=1,Merit=0;};
    const auto evaluate=[&](const gp_Pnt2d& uv){
        Evaluation value;gp_Vec du,dv,duu,dvv,duv;
        if(critical)surface.D2(uv.X(),uv.Y(),value.Point,du,dv,duu,dvv,duv);else surface.D1(uv.X(),uv.Y(),value.Point,du,dv);
        value.F1=value.Point.X()-sectionX;value.A=du.X();value.B=dv.X();
        if(critical){
            value.F2=du.X()*dv.Y()-du.Y()*dv.X();value.SecondScale=std::max(1.,du.Magnitude()*dv.Magnitude());
            value.C=duu.X()*dv.Y()+du.X()*duv.Y()-duu.Y()*dv.X()-du.Y()*duv.X();
            value.D=duv.X()*dv.Y()+du.X()*dvv.Y()-duv.Y()*dv.X()-du.Y()*dvv.X();
        }else{value.F2=value.Point.Z()-depth;value.C=du.Z();value.D=dv.Z();}
        value.Merit=std::max(std::abs(value.F1),std::abs(value.F2)/value.SecondScale);
        for(const double scalar:{value.Point.X(),value.Point.Y(),value.Point.Z(),value.F1,value.F2,value.A,value.B,value.C,value.D,value.SecondScale,value.Merit})
            Require(std::isfinite(scalar),"Dump surface Newton evaluation is nonfinite");return value;
    };
    gp_Pnt2d uv=seed;
    try{
        Require(inDomain(uv),"Dump surface Newton seed is outside the natural domain");
        for(int iteration=0;iteration<80;++iteration){
            const auto value=evaluate(uv);report["iterations"]=iteration;report["uv"]=Array{uv.X(),uv.Y()};report["insideNaturalDomain"]=inDomain(uv);
            report["pointMm"]=Array{value.Point.X(),value.Point.Y(),value.Point.Z()};report["insideThicknessSlab"]=value.Point.Z()>=capLow&&value.Point.Z()<=capHigh;
            report["residualXmm"]=value.F1;report["residualSecondRaw"]=value.F2;report["residualSecondNormalized"]=value.F2/value.SecondScale;
            if(std::abs(value.F1)<=1e-10&&std::abs(value.F2)/value.SecondScale<=(critical?1e-12:1e-10)){report["converged"]=true;break;}
            const double determinant=value.A*value.D-value.B*value.C,scale=std::abs(value.A*value.D)+std::abs(value.B*value.C);report["jacobianDeterminant"]=determinant;
            Require(std::isfinite(determinant)&&std::abs(determinant)>64*std::numeric_limits<double>::epsilon()*scale,"Dump surface Newton Jacobian is singular");
            const double deltaU=(-value.F1*value.D+value.B*value.F2)/determinant,deltaV=(value.C*value.F1-value.A*value.F2)/determinant;
            Require(std::isfinite(deltaU)&&std::isfinite(deltaV),"Dump surface Newton step is nonfinite");bool accepted=false;double factor=1;
            for(int damping=0;damping<40;++damping,factor*=.5){
                const gp_Pnt2d candidate(uv.X()+factor*deltaU,uv.Y()+factor*deltaV);
                if(!inDomain(candidate))continue;const auto next=evaluate(candidate);
                if(next.Merit<value.Merit){uv=candidate;accepted=true;break;}
            }
            Require(accepted,"Dump surface Newton could not reduce residual inside the natural domain");
            if(iteration==79)report["error"]=std::string("Dump surface Newton iteration limit reached");
        }
    }catch(const Standard_Failure& failure){const char* message=failure.what();report["error"]=std::string(message?message:"OCCT surface Newton failure");report["exceptionType"]=std::string(failure.ExceptionType());}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());}
    return report;
}
}

TEST(FiveAxisDoorSupport, DISABLED_ReplayDumpInteriorSilhouette){
    using namespace door_support_tests;
    Map report{{"schema",std::string("icax.door-dump-interior-silhouette.1")},{"diagnosticOnly",true}};Array samples;
    try{
        const auto directory=Environment("ICAX_DOOR_STOCK_REPLAY_DIR"),stationText=Environment("ICAX_DOOR_SUPPORT_AUDIT_STATION");
        Require(!directory.empty()&&!stationText.empty()&&std::all_of(stationText.begin(),stationText.end(),[](char value){return value>='0'&&value<='9';}),"Interior-silhouette dump diagnostic requires a directory and numeric station");
        const auto station=std::stoull(stationText);const auto prefix="stock-"+std::to_string(station);report["station"]=station;
        const auto overlapPath=Utf8Path(directory)/(prefix+"-overlap.brep"),platePath=Utf8Path(directory)/(prefix+"-solid.brep");
        report["overlapPath"]=Utf8(overlapPath);report["platePath"]=Utf8(platePath);const auto overlap=ReadStockDump(overlapPath),plate=ReadStockDump(platePath);
        const auto bounds=Bounds(overlap),plateBounds=Bounds(plate);report["overlapBoundsMm"]=Numbers(bounds);report["inputPBoundsMm"]=Numbers(plateBounds);
        // Read the exact prism cap planes rather than mistaking a bounding
        // box's numerical enlargement for the actual extrusion depth.
        double capLow=std::numeric_limits<double>::max(),capHigh=std::numeric_limits<double>::lowest();
        for(TopExp_Explorer explorer(plate,TopAbs_FACE);explorer.More();explorer.Next()){
            BRepAdaptor_Surface surface(TopoDS::Face(explorer.Current()),false);if(surface.GetType()!=GeomAbs_Plane)continue;const auto plane=surface.Plane();const auto normal=plane.Axis().Direction();
            if(normal.X()!=0||normal.Y()!=0)continue;capLow=std::min(capLow,plane.Location().Z());capHigh=std::max(capHigh,plane.Location().Z());
        }
        Require(std::isfinite(capLow)&&std::isfinite(capHigh)&&capLow<capHigh,"Dump plate does not expose two exact constant-Z prism caps");report["capDepthsMm"]=Array{capLow,capHigh};
        for(const double fraction:{.25,.5,.75}){
            const double sectionX=bounds[0]+(bounds[3]-bounds[0])*fraction;Map sample{{"fractionOfOverlapX",fraction},{"sectionXmm",sectionX}};Array faces;std::optional<double> plateTop;
            std::cout<<"Door dump interior-silhouette section x="<<std::setprecision(17)<<sectionX<<std::endl;
            try{
                BRepAlgoAPI_Section section(plate,gp_Pln(gp_Pnt(sectionX,0,0),gp_Dir(1,0,0)),false);section.SetNonDestructive(true);section.SetRunParallel(false);section.Build();
                sample["plateSectionDone"]=section.IsDone();sample["plateSectionHasErrors"]=section.HasErrors();
                if(section.IsDone()){
                    sample["plateSectionValid"]=BRepCheck_Analyzer(section.Shape()).IsValid();const auto sectionBounds=Bounds(section.Shape());sample["plateSectionBoundsMm"]=Numbers(sectionBounds);
                    plateTop=sectionBounds[4];sample["plateTopYmm"]=*plateTop;
                }
            }catch(const Standard_Failure& failure){const char* message=failure.what();sample["plateSectionError"]=std::string(message?message:"OCCT plate section failure");}
            catch(const std::exception& failure){sample["plateSectionError"]=std::string(failure.what());}
            size_t faceIndex=0;
            for(TopExp_Explorer explorer(overlap,TopAbs_FACE);explorer.More();explorer.Next(),++faceIndex){
                const auto face=TopoDS::Face(explorer.Current());if(BRepAdaptor_Surface(face,false).GetType()!=GeomAbs_BSplineSurface)continue;
                double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);const gp_Pnt2d seed((u0+u1)*.5,(v0+v1)*.5);
                Map item{{"faceIndex",static_cast<UInt>(faceIndex)},{"trimmedUVBounds",Array{u0,u1,v0,v1}}};
                const auto cap0=DumpSurfaceStationRoot(face,seed,sectionX,capLow,false,capLow,capHigh),cap3=DumpSurfaceStationRoot(face,seed,sectionX,capHigh,false,capLow,capHigh),critical=DumpSurfaceStationRoot(face,seed,sectionX,0,true,capLow,capHigh);
                item["cap0"]=cap0;item["cap3"]=cap3;item["critical"]=critical;
                const auto yValue=[](const Map& value)->std::optional<double>{if(!value.at("converged").To<bool>())return {};return value.at("pointMm").To<Array>()[1].To<double>();};
                const auto y0=yValue(cap0),y3=yValue(cap3),yc=yValue(critical);
                if(y0)item["yCap0Mm"]=*y0;if(y3)item["yCap3Mm"]=*y3;if(yc)item["yCriticalMm"]=*yc;
                if(y0&&y3){
                    const double capMinimum=std::min(*y0,*y3);item["yCapMinimumMm"]=capMinimum;if(plateTop)item["plateTopMinusCapMinimumMm"]=*plateTop-capMinimum;
                    if(yc&&critical.at("insideThicknessSlab").To<bool>()){
                        const double minimum=std::min(capMinimum,*yc);item["yMinimumCandidateMm"]=minimum;item["capMinimumMinusCriticalMm"]=capMinimum-*yc;
                        if(plateTop)item["plateTopMinusMinimumCandidateMm"]=*plateTop-minimum;
                    }
                }
                faces.emplace_back(item);
            }
            sample["splineFaces"]=std::move(faces);std::cout<<"Door dump interior-silhouette result "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(sample)<<std::endl;samples.emplace_back(sample);
        }
    }catch(const Standard_Failure& failure){const char* message=failure.what();report["error"]=std::string(message?message:"OCCT interior-silhouette dump diagnostic failure");ADD_FAILURE()<<failure.what();}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    report["samples"]=std::move(samples);std::cout<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report)<<std::endl;
}

TEST(FiveAxisDoorSupport, DISABLED_PublicServiceReplaySingleCeilingFace){
    using namespace door_support_tests;
    const auto file=Environment("ICAX_DOOR_PROJECTION_REPLAY_FILE");ASSERT_FALSE(file.empty());
    const auto shape=ReadStockDump(Utf8Path(file));ASSERT_TRUE(BRepCheck_Analyzer(shape).IsValid())<<"Replay input is not a valid original BRep";
    ShapeMap faces,solids;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_SOLID,solids);
    ASSERT_EQ(1,faces.Extent())<<"Replay requires the original single ceiling face";ASSERT_EQ(0,solids.Extent())<<"Replay source must remain a sheet";
    const auto face=TopoDS::Face(faces(1));const auto bounds=Bounds(face);
    const bool skipWitness=Enabled("ICAX_DOOR_PUBLIC_REPLAY_SKIP_WITNESS");std::optional<gp_Pnt> witness;
    std::cout<<"Door single-face public replay mode "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Map{{"diagnosticOnly",skipWitness},{"witnessSkipped",skipWitness}})<<std::endl;
    if(!skipWitness){
    // This fixture witness was independently located on the original surface:
    // S.X=x and (Su cross Sv).Z=0, before any public support construction.
    const auto root=DumpSurfaceStationRoot(face,gp_Pnt2d(-188.3519247605,-276.23697296135),-302.5428745515,0,true,0,3);
    std::cout<<"Door single-face original-equation witness "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(root)<<std::endl;
    ASSERT_TRUE(root.at("converged").To<bool>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(root);
    ASSERT_TRUE(root.at("insideNaturalDomain").To<bool>());ASSERT_TRUE(root.at("insideThicknessSlab").To<bool>())<<"Critical witness is outside the original depth [0,3]";
    const auto uv=root.at("uv").To<Array>(),point=root.at("pointMm").To<Array>();
    BRepClass_FaceClassifier inSource(face,gp_Pnt2d(uv[0].To<double>(),uv[1].To<double>()),Precision::PConfusion());
    ASSERT_TRUE(inSource.State()==TopAbs_IN||inSource.State()==TopAbs_ON)<<"Original-equation witness lies outside the supplied face trim; replay input does not cover the diagnosed branch";
    witness=gp_Pnt(point[2].To<double>(),point[0].To<double>(),point[1].To<double>());
    }
    const auto geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepGeometry(shape,"Single original ceiling face replay","generated"));
    ASSERT_TRUE(geometry->Triangulations3.empty());ASSERT_TRUE(geometry->Solids.empty());const auto before=iCAX::GeometryData::Persistence::Serialize(*geometry);const auto beforeHash=Digest(before);
    Input input;input.Geometry=geometry;
    // Original local (X,Y,Z) becomes world (Z,X,Y): thickness is world X,
    // manufacturing span is world Y, and original local Y is world height.
    input.Transform={0,0,1,0,1,0,0,0,0,1,0,0,0,0,0,1};
    for(const auto& record:geometry->Faces)input.FaceIDs.push_back(record.Id);
    input.Stations={{.Coordinate=1.5,.From=bounds[0],.To=bounds[3],.AlongX=true}};input.Thickness=3;input.Automatic=false;input.BaseDrop=20;
    input.JointMode="Weld";input.WindowMode="None";input.SolidBase=true;input.AvoidOpeningBoundaries=false;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<iCAX::Geometry::ISectionPlateService>();ASSERT_TRUE(service);const auto parts=service->Build(input,{});TopoDS_Shape support;
    for(const auto& part:parts){
        ASSERT_TRUE(part.Geometry.Triangulations3.empty());const auto native=iCAX::OpenCascade::ReconstructOpenCascadeShape(part.Geometry);ASSERT_TRUE(native.bOK);ASSERT_TRUE(BRepCheck_Analyzer(native.Shape).IsValid());
        ShapeMap resultSolids;TopExp::MapShapes(native.Shape,TopAbs_SOLID,resultSolids);ASSERT_EQ(1,resultSolids.Extent());
        for(TopExp_Explorer occurrence(native.Shape,TopAbs_FACE);occurrence.More();occurrence.Next()){TopLoc_Location location;EXPECT_TRUE(BRep_Tool::Triangulation(TopoDS::Face(occurrence.Current()),location).IsNull());}
        if(!part.Base&&!part.Connection){ASSERT_TRUE(support.IsNull());support=native.Shape;}
    }
    ASSERT_FALSE(support.IsNull());
    if(witness){
        const auto expected=BRepBuilderAPI_MakeVertex(*witness).Vertex();const double distance=BoundaryDistance(expected,BoundaryFaces(support));
        std::cout<<"Door single-face public support witness distance="<<std::setprecision(17)<<distance<<" mm"<<std::endl;EXPECT_LE(distance,1e-7);
    }
    const auto after=iCAX::GeometryData::Persistence::Serialize(*geometry);EXPECT_EQ(beforeHash,Digest(after));EXPECT_EQ(before,after);
}

namespace door_support_tests {
std::vector<std::uint8_t> ReturnedAuditBytes(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary);Require(file.is_open(),"Cannot read returned Door audit input: "+Utf8(path));
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    Require(!file.bad()&&!bytes.empty(),"Empty or unreadable returned Door audit input: "+Utf8(path));return bytes;
}
double ReturnedAuditReal(const iCAX::Data::Variant& value){
    double result;
    if(value.Is<double>())result=value.To<double>();
    else if(value.Is<long long>())result=static_cast<double>(value.To<long long>());
    else if(value.Is<UInt>())result=static_cast<double>(value.To<UInt>());
    else throw std::runtime_error("Returned Door audit expected a numeric value");
    Require(std::isfinite(result),"Returned Door audit input is nonfinite");return result;
}
size_t ReturnedAuditIndex(const iCAX::Data::Variant& value){
    UInt result=0;
    if(value.Is<UInt>())result=value.To<UInt>();
    else {Require(value.Is<long long>()&&value.To<long long>()>=0,"Returned Door audit expected a nonnegative integer");result=static_cast<UInt>(value.To<long long>());}
    Require(result<=std::numeric_limits<size_t>::max(),"Returned Door audit integer exceeds size_t");return static_cast<size_t>(result);
}
template<size_t N> std::array<double,N> ReturnedAuditVector(const iCAX::Data::Variant& value){
    const auto values=value.To<Array>();Require(values.size()==N,"Returned Door audit vector has the wrong length");
    std::array<double,N> result{};for(size_t index=0;index<N;++index)result[index]=ReturnedAuditReal(values[index]);return result;
}
std::filesystem::path ReturnedAuditFile(const std::filesystem::path& directory,const std::string& name){
    const auto relative=Utf8Path(name);Require(!name.empty()&&!relative.is_absolute()&&relative==relative.filename()&&name!="."&&name!="..",
        "Returned Door audit file must be a local filename");return directory/relative;
}
struct ReturnedAuditPart {Plate Metadata;std::vector<std::uint8_t> Bytes;std::string Sha256;std::filesystem::path Path;};
}

TEST(FiveAxisDoorSupport, DISABLED_AuditReturnedDoorParts){
    using namespace door_support_tests;
    const auto started=std::chrono::steady_clock::now();const Source* auditSource=nullptr;std::vector<std::uint8_t> auditSourceBefore;
    bool auditsPassed=false;Map report{{"schema",std::string("icax.door-support-returned-audit.1")},{"passed",false},{"complete",false},{"geometryGenerated",false}};
    try{
        const auto manifestSetting=Environment("ICAX_DOOR_PARTS_REPLAY_MANIFEST");Require(!manifestSetting.empty(),"ICAX_DOOR_PARTS_REPLAY_MANIFEST must name the saved returned-parts manifest");
        const auto manifestPath=std::filesystem::absolute(Utf8Path(manifestSetting)).lexically_normal(),directory=manifestPath.parent_path();
        const auto manifestBytes=ReturnedAuditBytes(manifestPath);const auto manifestDigest=Digest(manifestBytes);
        const auto manifest=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(std::string(manifestBytes.begin(),manifestBytes.end())).To<Map>();
        Require(manifest.at("schema").To<std::string>()=="icax.door-support-returned-parts.1","Unexpected returned Door manifest schema");
        report["savedManifest"]=Utf8(manifestPath);report["savedManifestDigest"]=manifestDigest;
        Require(manifest.at("sourceFrame").To<std::string>()=="original","Returned Door audit requires the complete original source frame");
        const auto baked=ReturnedAuditVector<16>(manifest.at("bakedTransform"));Require(baked==Matrix(gp_Trsf{}),"Returned Door source has a baked placement transform");
        const auto mode=manifest.at("mode").To<std::string>();Require(mode=="center-placement","Returned Door audit requires the saved center-placement scenario");
        const auto& source=ImportedDoor();auditSource=&source;auditSourceBefore=iCAX::GeometryData::Persistence::Serialize(*source.Geometry);
        Require(source.SourceFrame=="original"&&source.BakedTransform==baked,"Fresh Door import does not use the original unmodified frame");
        const auto sourcePath=ReturnedAuditFile(directory,manifest.at("sourceNeutralFile").To<std::string>());const auto sourceBytes=ReturnedAuditBytes(sourcePath);
        const auto producerFileHash=manifest.at("sourceFileSha256").To<std::string>(),producerNeutralHash=manifest.at("sourceCadSha256").To<std::string>();
        const auto sourceFileHash=Digest(ReturnedAuditBytes(source.Path)),sourceNeutralHash=Digest(sourceBytes);
        report["sourcePath"]=Utf8(source.Path);report["sourceFileSha256"]=sourceFileHash;report["sourceNeutralSha256"]=sourceNeutralHash;
        report["producerSourceFileSha256"]=producerFileHash;report["producerSourceNeutralSha256"]=producerNeutralHash;
        report["auditSourceCadSha256Before"]=Digest(auditSourceBefore);
        const bool fileMatch=sourceFileHash==producerFileHash&&source.FileDigest==producerFileHash;
        const bool neutralMatch=sourceNeutralHash==producerNeutralHash&&Digest(auditSourceBefore)==producerNeutralHash&&sourceBytes==auditSourceBefore;
        report["sourceFileBytesMatchProducer"]=fileMatch;report["sourceNeutralBytesMatchProducer"]=neutralMatch;
        Require(fileMatch&&neutralMatch,"Saved Door source bytes do not match the fresh original import and producer manifest");
        for(const auto& record:source.Geometry->Triangulations3)Require(record.Geometry.Vertices.empty()&&record.Geometry.Triangles.empty(),"Fresh Door audit input contains display mesh geometry");
        Input input;input.Transform=ReturnedAuditVector<16>(manifest.at("transform"));input.SupportDirection=ReturnedAuditVector<3>(manifest.at("supportDirection"));
        input.BaseDrop=ReturnedAuditReal(manifest.at("baseDropMm"));input.Gap=ReturnedAuditReal(manifest.at("gapMm"));
        Require(input.BaseDrop>=0&&input.Gap>=0,"Returned Door clearance requirements are negative");
        const auto sourceBounds=Bounds(source.Native.Shape);auto expectedTransform=Matrix(gp_Trsf{});
        for(size_t axis=0;axis<3;++axis)expectedTransform[axis*4+3]=-(sourceBounds[axis]+sourceBounds[axis+3])*.5;
        for(size_t index=0;index<16;++index)Require(std::abs(input.Transform[index]-expectedTransform[index])<=(index==3||index==7||index==11?1e-9:1e-12),
            "Saved Door transform is not the complete original-source center placement");
        Require(input.Transform[12]==0&&input.Transform[13]==0&&input.Transform[14]==0&&input.Transform[15]==1,"Saved Door transform is not affine");
        Require(input.SupportDirection==std::array<double,3>{0,0,1},"Saved centered Door support direction is inconsistent");
        gp_Trsf placement;const auto& m=input.Transform;placement.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);
        const auto actualTransform=Matrix(placement);for(size_t index=0;index<16;++index)Require(std::abs(actualTransform[index]-m[index])<=1e-12,"Native placement differs from the saved transform");
        const auto selectedFaces=manifest.at("selectedFaceIds").To<Array>();Require(selectedFaces.size()==1&&selectedFaces.front().To<std::string>()=="100"&&source.Native.Faces.contains(100),"Returned Door audit requires canonical source face 100");
        const auto stations=manifest.at("stations").To<Array>();Require(stations.size()==10,"Returned Door manifest must retain ten stations");
        Require(ReturnedAuditIndex(manifest.at("supportCount"))==7&&ReturnedAuditIndex(manifest.at("connectionCount"))==3,"Returned Door manifest role counts changed");
        report["sourceFrame"]=source.SourceFrame;report["bakedTransform"]=Numbers(baked);report["mode"]=mode;report["transform"]=Numbers(input.Transform);
        report["supportDirection"]=Numbers(input.SupportDirection);report["sourceBoundsMm"]=Numbers(sourceBounds);report["selectedFaceIds"]=selectedFaces;
        report["baseDropMm"]=input.BaseDrop;report["gapMm"]=input.Gap;report["stations"]=stations;
        const auto values=manifest.at("parts").To<Array>();Require(values.size()==11,"Returned Door audit requires all eleven saved parts");
        std::vector<ReturnedAuditPart> saved;std::set<size_t> stationIDs;size_t supports=0,connections=0,bases=0;
        for(size_t index=0;index<values.size();++index){
            const auto value=values[index].To<Map>();Require(ReturnedAuditIndex(value.at("index"))==index,"Returned Door part indices must be unique and complete in [0,10]");
            ReturnedAuditPart record;record.Path=ReturnedAuditFile(directory,value.at("file").To<std::string>());record.Bytes=ReturnedAuditBytes(record.Path);record.Sha256=Digest(record.Bytes);
            Require(record.Bytes.size()==ReturnedAuditIndex(value.at("bytes"))&&record.Sha256==value.at("sha256").To<std::string>(),"Returned Door part byte count or SHA-256 differs from its manifest");
            const auto geometry=iCAX::GeometryData::Persistence::Deserialize(record.Bytes);Require(iCAX::GeometryData::Persistence::Serialize(geometry)==record.Bytes,"Returned Door neutral part serialization changed on read");
            Require(geometry.Triangulations3.empty(),"Returned Door intermediate part contains display triangulations");
            auto& part=record.Metadata;part.Base=value.at("base").To<bool>();part.Connection=value.at("connection").To<bool>();part.Supported=value.at("supported").To<bool>();
            Require(!(part.Base&&part.Connection),"Returned Door part has conflicting roles");part.StationIndex=ReturnedAuditIndex(value.at("station"));
            part.Frame=ReturnedAuditVector<16>(value.at("frame"));part.ProjectionFrame=ReturnedAuditVector<16>(value.at("projectionFrame"));
            part.Thickness=ReturnedAuditReal(value.at("thicknessMm"));part.Distance=ReturnedAuditReal(value.at("reportedDistanceMm"));
            Require(part.Thickness>0&&part.Distance>=0,"Returned Door part dimensions or reported distance are invalid");
            part.ExactProfile=iCAX::CAD::TubeCadJson::Decode<iCAX::GeometryData::Tube::CPlanarRegion2>(value.at("exactProfile"));
            for(const auto& contact:value.at("contacts").To<Array>())part.Contacts.push_back(ReturnedAuditVector<3>(contact));
            if(part.Base)++bases;else{
                Require(part.StationIndex<stations.size()&&stationIDs.insert(part.StationIndex).second,"Returned Door station IDs are missing or duplicated");
                Require(stations[part.StationIndex].To<Map>().at("connection").To<bool>()==part.Connection,"Returned Door station and part roles disagree");
                if(part.Connection)++connections;else ++supports;
            }
            saved.push_back(std::move(record));
        }
        Require(supports==7&&connections==3&&bases==1&&stationIDs.size()==10,"Returned Door role counts must be seven supports, three connections and one base");
        report["supportCount"]=static_cast<UInt>(supports);report["connectionCount"]=static_cast<UInt>(connections);report["baseCount"]=static_cast<UInt>(bases);
        const size_t workerCount=std::min(size_t{4},saved.size());report["maximumConcurrency"]=static_cast<UInt>(workerCount);report["partCount"]=static_cast<UInt>(saved.size());
        std::mutex reportMutex;std::atomic_size_t next{0};size_t completed=0;Array workerErrors;std::vector<Map> results;
        for(size_t index=0;index<saved.size();++index){const auto& part=saved[index].Metadata;results.push_back(Map{{"index",static_cast<UInt>(index)},{"station",static_cast<UInt>(part.StationIndex)},
            {"base",part.Base},{"connection",part.Connection},{"savedFile",Utf8(saved[index].Path)},{"savedBytesSha256",saved[index].Sha256},{"byteCount",static_cast<UInt>(saved[index].Bytes.size())},{"passed",false},{"status",std::string("queued")}});}
        const auto publish=[&](size_t index,Map item,bool finished){
            std::lock_guard lock(reportMutex);results[index]=std::move(item);if(finished)++completed;Array parts;for(const auto& result:results)parts.emplace_back(result);
            report["parts"]=std::move(parts);report["completedParts"]=static_cast<UInt>(completed);report["elapsedMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();WriteReport(report);
        };
        Array initial;for(const auto& result:results)initial.emplace_back(result);report["parts"]=std::move(initial);report["completedParts"]=UInt{0};WriteReport(report);
        const auto worker=[&](size_t workerID){
            try{
                // Deserialize and reconstruct inside the worker. Only immutable
                // byte buffers/neutral metadata cross threads; no kernel shape,
                // geometry handle, pcurve or mutable topology is shared.
                const auto privateSource=iCAX::GeometryData::Persistence::Deserialize(sourceBytes);
                for(const auto& record:privateSource.Triangulations3)Require(record.Geometry.Vertices.empty()&&record.Geometry.Triangles.empty(),"Private returned-audit source contains display mesh geometry");
                const auto sourceNative=iCAX::OpenCascade::ReconstructOpenCascadeShape(privateSource);Require(sourceNative.bOK,"Private returned-audit full-source reconstruction failed");
                Require(BRepCheck_Analyzer(sourceNative.Shape).IsValid(),"Private returned-audit full source is invalid");
                const auto work=sourceNative.Shape.Moved(TopLoc_Location(placement));const auto workBoundary=BoundaryFaces(work);const auto workBounds=ConservativeBounds(work);
                std::vector<Plate> parts;for(const auto& record:saved)parts.push_back(record.Metadata);
                std::vector<iCAX::OpenCascade::SOpenCascadeBRepBuildResult> nativeParts(saved.size());std::vector<Bnd_Box> nativeBounds(saved.size());std::vector<bool> nativeReady(saved.size(),false);
                const auto nativePart=[&](size_t index)->const iCAX::OpenCascade::SOpenCascadeBRepBuildResult&{
                    auto& native=nativeParts[index];if(!nativeReady[index]){parts[index].Geometry=iCAX::GeometryData::Persistence::Deserialize(saved[index].Bytes);
                        native=iCAX::OpenCascade::ReconstructOpenCascadeShape(parts[index].Geometry);Require(native.bOK,"Returned Door plate BRep reconstruction failed");
                        Require(BRepCheck_Analyzer(native.Shape).IsValid(),"Returned Door plate BRep is invalid");nativeBounds[index]=ConservativeBounds(native.Shape);nativeReady[index]=true;
                    }return native;
                };
                while(true){
                    const size_t index=next.fetch_add(1);if(index>=saved.size())break;
                    Map item;{std::lock_guard lock(reportMutex);item=results[index];}item["worker"]=static_cast<UInt>(workerID);item["status"]=std::string("running");publish(index,item,false);
                    const auto partStarted=std::chrono::steady_clock::now();std::string phase="exact profile closure";
                    try{
                        auto& part=parts[index];const double profileGap=MaximumExactProfileJunctionGap(part);item["exactProfileMaximumJunctionGapMm"]=profileGap;
                        phase="private BRep reconstruction";const auto& native=nativePart(index);item["validBRep"]=true;
                        Require(part.Geometry.Triangulations3.empty(),"Returned Door intermediate part contains display triangulations");
                        for(TopExp_Explorer face(native.Shape,TopAbs_FACE);face.More();face.Next()){TopLoc_Location location;Require(BRep_Tool::Triangulation(TopoDS::Face(face.Current()),location).IsNull(),"Returned Door reconstructed part contains triangulation");}
                        size_t solidCount=0;for(TopExp_Explorer solid(native.Shape,TopAbs_SOLID);solid.More();solid.Next())++solidCount;
                        GProp_GProps mass;BRepGProp::VolumeProperties(native.Shape,mass);const double volume=std::abs(mass.Mass());
                        item["solidCount"]=static_cast<UInt>(solidCount);item["volumeMm3"]=volume;item["frame"]=Numbers(part.Frame);item["displayTriangulations"]=static_cast<UInt>(part.Geometry.Triangulations3.size());
                        Require(solidCount==1&&std::isfinite(volume)&&volume>0,"Returned Door plate must be one positive-volume solid");
                        phase="full-source distance/contact audit";const auto distance=AuditDistance(part,BoundaryFaces(native.Shape),workBoundary,nativeBounds[index],workBounds,input);item["distanceEvidence"]=distance.Data;
                        phase="full-source exact intersection";std::string workProof;const double overlap=IntersectionVolume(native.Shape,work,nativeBounds[index],workBounds,workProof);
                        item["workpieceIntersectionVolumeMm3"]=overlap;item["workpieceIntersectionMethod"]=workProof;
                        phase="exact plate-pair intersections";double maximumOverlap=0;Array pairAudits;
                        for(size_t earlier=0;earlier<index;++earlier){
                            const auto& other=nativePart(earlier);std::string proof;const double intersection=IntersectionVolume(native.Shape,other.Shape,nativeBounds[index],nativeBounds[earlier],proof);
                            maximumOverlap=std::max(maximumOverlap,intersection);pairAudits.emplace_back(Map{{"earlierPartIndex",static_cast<UInt>(earlier)},{"volumeMm3",intersection},{"method",proof}});
                            item["plateIntersectionAudits"]=pairAudits;item["auditedPlatePairCount"]=static_cast<UInt>(pairAudits.size());
                        }
                        item["plateIntersectionAudits"]=std::move(pairAudits);item["auditedPlatePairCount"]=static_cast<UInt>(index);item["maximumPlateIntersectionVolumeMm3"]=maximumOverlap;
                        phase="acceptance checks";Require(profileGap<=1e-7,"Returned Door exact manufacturing profile is open");
                        Require(overlap<=1e-7,"Returned Door plate penetrates the full original workpiece");Require(maximumOverlap<=1e-7,"Returned Door plate penetrates another plate");
                        Require(distance.Verified,"Returned Door plate violates its original distance/contact requirement");if(part.Base)Require(part.Supported,"Returned Door support check is not Supported");
                        Require(Digest(iCAX::GeometryData::Persistence::Serialize(part.Geometry))==saved[index].Sha256,"Returned Door neutral part changed during the independent audit");
                        item["passed"]=true;item["status"]=std::string("completed");
                    }catch(const Standard_Failure& failure){item["error"]=std::string(failure.what());item["status"]=std::string("failed");}
                    catch(const std::exception& failure){item["error"]=std::string(failure.what());item["status"]=std::string("failed");}
                    catch(...){item["error"]=std::string("Unknown returned Door part audit failure");item["status"]=std::string("failed");}
                    item["phase"]=phase;item["posteriorAuditMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-partStarted).count();publish(index,std::move(item),true);
                }
                Require(Digest(iCAX::GeometryData::Persistence::Serialize(privateSource))==sourceNeutralHash,"Private full-source neutral input changed during returned Door audit");
            }catch(const Standard_Failure& failure){std::lock_guard lock(reportMutex);workerErrors.emplace_back(Map{{"worker",static_cast<UInt>(workerID)},{"error",std::string(failure.what())}});}
            catch(const std::exception& failure){std::lock_guard lock(reportMutex);workerErrors.emplace_back(Map{{"worker",static_cast<UInt>(workerID)},{"error",std::string(failure.what())}});}
            catch(...){std::lock_guard lock(reportMutex);workerErrors.emplace_back(Map{{"worker",static_cast<UInt>(workerID)},{"error",std::string("Unknown returned Door worker failure")}});}
        };
        std::vector<std::jthread> workers;for(size_t index=0;index<workerCount;++index)workers.emplace_back(worker,index);for(auto& thread:workers)thread.join();
        Array finalParts;bool allPassed=completed==saved.size()&&workerErrors.empty();std::set<std::pair<size_t,size_t>> auditedPairs;
        for(size_t index=0;index<results.size();++index){const auto& item=results[index];finalParts.emplace_back(item);allPassed=allPassed&&item.at("passed").To<bool>();
            if(item.contains("plateIntersectionAudits"))for(const auto& pair:item.at("plateIntersectionAudits").To<Array>()){
                const size_t earlier=ReturnedAuditIndex(pair.To<Map>().at("earlierPartIndex"));Require(earlier<index&&auditedPairs.emplace(earlier,index).second,"Returned Door plate-pair audit is duplicate or misidentified");
            }
        }
        report["parts"]=std::move(finalParts);report["workerErrors"]=workerErrors;report["completedParts"]=static_cast<UInt>(completed);report["auditedPlatePairCount"]=static_cast<UInt>(auditedPairs.size());
        const auto manifestDigestAfter=Digest(ReturnedAuditBytes(manifestPath));const bool manifestUnchanged=manifestDigestAfter==manifestDigest;
        report["savedManifestDigestAfter"]=manifestDigestAfter;report["savedManifestUnchanged"]=manifestUnchanged;Require(manifestUnchanged,"Returned Door manifest changed during the independent audit");
        Require(Digest(ReturnedAuditBytes(sourcePath))==sourceNeutralHash,"Saved returned Door neutral source file changed during the independent audit");
        for(const auto& record:saved)Require(Digest(ReturnedAuditBytes(record.Path))==record.Sha256,"Saved returned Door part file changed during the independent audit");
        Require(Digest(ReturnedAuditBytes(source.Path))==sourceFileHash,"Original Door source file changed during the independent audit");
        auditsPassed=allPassed&&auditedPairs.size()==55;Require(auditsPassed,"Independent returned Door audit did not pass all eleven parts and all fifty-five plate pairs");
    }catch(const Standard_Failure& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    catch(...){report["error"]=std::string("Unknown returned Door audit failure");ADD_FAILURE()<<"Unknown returned Door audit failure";}
    if(auditSource){
        try{
            const auto after=iCAX::GeometryData::Persistence::Serialize(*auditSource->Geometry);report["auditSourceCadSha256After"]=Digest(after);
            report["auditInputUnchanged"]=after==auditSourceBefore;Require(after==auditSourceBefore,"Fresh Door audit input changed during returned-parts replay");
        }catch(const std::exception& failure){report["error"]=std::string(failure.what());ADD_FAILURE()<<failure.what();}
    }
    // This reports a fresh audit of saved construction output. It cannot
    // recover the terminated producer's unrecorded post-Build memory state.
    report["complete"]=true;report["passed"]=auditsPassed&&!HasFailure();report["elapsedMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    WriteReport(report);std::cout<<"Door returned-parts independent audit "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report)<<std::endl;
}
