#include "OpenCascadeResourceImport/TubeCadDocumentService.h"
#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace G=iCAX::GeometryData;namespace T=G::Tube;namespace O=iCAX::OpenCascade;
using iCAX::Data::ObjectMap;using iCAX::Data::VariantArray;
using Json=iCAX::TemplateRuntime::CStandardJsonCodec;

TopoDS_Shape Native(const G::BRepModel& model)
{
    const auto built=O::BuildOpenCascadeShape(model);
    if(!built.bOK||built.Shape.IsNull()||!BRepCheck_Analyzer(built.Shape).IsValid())throw std::runtime_error("Invalid neutral test output");
    return built.Shape;
}
double Volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return std::abs(properties.Mass());
}
bool Equivalent(const TopoDS_Shape& a,const TopoDS_Shape& b)
{
    return O::ValidateTubeCSGShapeReplay(a,b,1e-6).bEquivalent;
}
T::SRegionLoop2 Rectangle(const std::string& id,double width,double height,bool inner=false)
{
    T::SRegionLoop2 loop;loop.ID=id;
    std::vector<G::Point2> points{{-width/2,-height/2},{width/2,-height/2},{width/2,height/2},{-width/2,height/2}};
    if(inner)std::reverse(points.begin(),points.end());
    for(std::size_t i=0;i<points.size();++i){G::CurveSegment2 segment;segment.Curve=G::Segment2{points[i],points[(i+1)%4]};loop.Curves.push_back({id+"/"+std::to_string(i),segment});}
    return loop;
}
T::CTubeNeutralGeometry Fixture(double floor=9.)
{
    T::CTubeNeutralGeometry model;model.BaseNodeID="stock";model.RootNodeID="part";model.RecognitionStatus=T::ERecognitionStatus::Exact;
    T::SExtrudedRegionNode stock;stock.Last=200;stock.Section.Boundaries={Rectangle("outer",40,20),Rectangle("inner",36,16,true)};
    stock.Frame.XDirection={0,1,0};stock.Frame.YDirection={0,0,1};stock.Frame.ZDirection={1,0,0};
    model.SolidNodes.push_back({"stock","Stock",stock});
    T::SExtrudedRegionNode hole;hole.Frame.Location={60,0,0};hole.First=8;hole.Last=12;
    G::CurveSegment2 circle;G::Circle2 value;value.Radius=4;circle.Curve=value;circle.Range={0,2*std::acos(-1.),false};
    hole.Section.Boundaries.push_back({"hole-profile",{{"circle",circle}}});model.SolidNodes.push_back({"hole","Hole",hole});
    T::SExtrudedRegionNode slot;slot.Frame.Location={120,0,0};slot.First=floor;slot.Last=12;
    slot.Section.Boundaries={Rectangle("slot-profile",12,8)};model.SolidNodes.push_back({"slot","Blind slot",slot});
    model.SolidNodes.push_back({"part","Part",T::SBooleanNode{T::EBooleanOperation::Difference,{"stock","hole","slot"}}});
    return model;
}
int main(int argc,char** argv)
{
    const auto directory=argc>1?std::filesystem::path(argv[1]):std::filesystem::path("tube-boundary-document-acceptance");
    std::filesystem::create_directories(directory);VariantArray checks;ObjectMap evidence;
    const auto save=[&](const char* status,const std::string& error={}){
        std::ofstream stream(directory/"checks.json",std::ios::binary);
        stream<<Json::Serialize(ObjectMap{{"status",std::string(status)},{"error",error},{"checks",checks},{"evidence",evidence}});stream.flush();
    };
    const auto check=[&](bool passed,const std::string& name){checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});save("running");if(!passed)throw std::runtime_error(name);};
    const auto rejected=[&](auto&& action){try{action();return false;}catch(const Standard_Failure&){return true;}catch(const std::exception&){return true;}};
    try {
        iCAX::Services::CServiceProvider services;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
        const auto service=services.Resolve<iCAX::CAD::ITubeCadDocumentService>();check(bool(service),"real registered tube CAD service resolves");
        const auto created=service->Create(Fixture());const auto originalBytes=T::Persistence::Serialize(created.Document);
        const auto original=Native(created.Evaluation.Shape);const double originalVolume=Volume(original),pi=std::acos(-1.);
        evidence["originalVolumeMm3"]=originalVolume;
        check(std::abs(originalVolume-(44800-pi*32-96))<1e-5,"Create with wall penetration and blind slot restores the expected full solid");
        check(created.Document.Geometry.Section.Boundaries.size()==2&&!created.Document.Geometry.MachiningFaces.Faces.empty(),"Create publishes section plus explicit machining-face core");
        check(created.Document.Revision==1,"Create starts document revision one");
        const auto directCreated=service->CreateGeometry(created.Document.Geometry,created.Document.Placement);
        check(directCreated.Document.Revision==1&&directCreated.Document.Construction.SolidNodes.empty()&&directCreated.Document.Bodies.empty(),"CreateGeometry accepts section plus machining boundary without an authoring graph");
        check(Equivalent(original,Native(directCreated.Evaluation.Shape)),"direct core creation restores the complete material");
        const auto directReopened=T::Persistence::DeserializeDocument(T::Persistence::Serialize(directCreated.Document));
        check(Equivalent(original,Native(service->Evaluate(directReopened).Shape)),"directly created core persists and reopens without construction resources");
        const auto explicitFaces=O::ReconstructOpenCascadeShape(created.Document.Geometry.MachiningFaces);bool hasFloor=false;
        for(const auto& [id,face]:explicitFaces.Faces){BRepAdaptor_Surface surface(face);if(surface.GetType()!=GeomAbs_Plane)continue;
            Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
            if(std::abs(z-9.)<1e-6&&std::abs(Z-9.)<1e-6&&x>100&&X<140)hasFloor=true;}
        check(hasFloor,"blind-slot bottom is retained as explicit CAD machining boundary");
        auto coreOnly=created.Document;coreOnly.Construction={};coreOnly.Bodies.clear();const auto coreOnlyBytes=T::Persistence::Serialize(coreOnly);
        check(Equivalent(original,Native(service->Evaluate(coreOnly).Shape)),"core restores the same solid after all construction definitions and bodies are removed");
        const auto section=service->ReadSketch(coreOnly,"section");
        check(std::get<T::CPlanarRegion2>(section.Geometry).Boundaries.size()==2&&section.NodeID=="section","section sketch is available directly from a core-only document");
        check(rejected([&]{service->EditSketch(coreOnly,section);}),"section edit without construction rules refuses to guess geometric dependencies");
        check(T::Persistence::Serialize(coreOnly)==coreOnlyBytes,"rejected core-only section edit preserves its original document bytes");
        auto corrupt=created.Document;corrupt.Construction.RootNodeID="missing-authoring-root";corrupt.Bodies.emplace("invalid",G::BRepModel{});
        check(Equivalent(original,Native(service->Evaluate(corrupt).Shape)),"invalid authoring graph and invalid optional body do not affect authoritative core evaluation");
        check(service->ReadSketch(corrupt,"section").NodeID=="section","invalid optional authoring data do not block core section reading");
        auto changedGraph=created.Document;std::get<T::SExtrudedRegionNode>(changedGraph.Construction.SolidNodes[2].Data).First=9.5;
        check(Equivalent(original,Native(service->Evaluate(changedGraph).Shape)),"changing only a construction definition cannot silently replace the core shape");
        const auto constructionPreview=service->EvaluateConstruction(changedGraph);
        check(std::abs(Volume(Native(constructionPreview.Shape))-originalVolume-48)<1e-5,"explicit construction preview evaluates the edited slot without publishing a core");
        check(Equivalent(Native(constructionPreview.Shape),Native(iCAX::CAD::EvaluateTubeCadConstruction(changedGraph).Shape)),"product construction-preview entry and service preview share the same result");
        check(constructionPreview.ConstructionBodies.contains("stock")&&constructionPreview.ConstructionBodies.contains("slot"),"construction previews retain actual node identities");
        const auto persisted=directory/"core-only.ictubecad";{std::ofstream file(persisted,std::ios::binary);file.write(reinterpret_cast<const char*>(coreOnlyBytes.data()),coreOnlyBytes.size());}
        std::ifstream binary(persisted,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(binary)),{});
        const auto reopened=T::Persistence::DeserializeDocument(bytes);
        check(T::Persistence::Serialize(reopened)==coreOnlyBytes,"binary file reopen retains the complete core exactly");
        check(Equivalent(original,Native(service->Evaluate(reopened).Shape)),"binary core-only reopen restores the same complete material");
        const auto jsonText=Json::Serialize(iCAX::CAD::TubeCadJson::Encode(coreOnly));{std::ofstream file(directory/"core-only.json",std::ios::binary);file<<jsonText;}
        std::ifstream jsonFile(directory/"core-only.json",std::ios::binary);const std::string json((std::istreambuf_iterator<char>(jsonFile)),{});
        const auto jsonReopened=iCAX::CAD::TubeCadJson::Decode<T::CTubeCadDocument>(Json::Parse(json));
        check(T::Persistence::Serialize(jsonReopened)==coreOnlyBytes,"JSON file reopen retains complete exact core and placement");
        check(Equivalent(original,Native(service->Evaluate(jsonReopened).Shape)),"JSON core-only reopen restores the same complete material");
        const auto shallower=service->Create(Fixture(9.5));const auto direct=service->EditGeometry(created.Document,1,shallower.Document.Geometry);
        check(direct.Document.Revision==2&&direct.Document.Construction.SolidNodes.empty()&&direct.Document.Bodies.empty(),"direct core replacement increments revision and clears obsolete constructions");
        check(std::abs(Volume(Native(direct.Evaluation.Shape))-originalVolume-48)<1e-5,"direct machining-boundary replacement changes actual blind depth");
        check(T::Persistence::Serialize(created.Document)==originalBytes,"direct core replacement leaves the original document unchanged");
        check(rejected([&]{service->EditGeometry(created.Document,9,shallower.Document.Geometry);}),"direct core replacement rejects stale revision");
        auto malformed=[&](bool reverse){auto geometry=created.Document.Geometry;
            BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);bool modified=false;
            // Traverse actual accumulated occurrences. The neutral face map
            // alone does not include its compound's authoritative orientation.
            for(TopExp_Explorer faces(explicitFaces.Shape,TopAbs_FACE);faces.More();faces.Next()){
                auto face=TopoDS::Face(faces.Current());BRepAdaptor_Surface surface(face);
                Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
                if(!modified&&surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().X())>.999
                    &&std::abs(x)<1e-6&&std::abs(X)<1e-6){modified=true;if(reverse)face.Reverse();else continue;}
                builder.Add(compound,face);}
            if(!modified)throw std::runtime_error("fixture lacks an explicit end face");
            geometry.MachiningFaces=O::ConvertOpenCascadeShapeToBRep(compound,"","",0.025);return geometry;};
        const auto missingEnd=malformed(false),wrongOrientation=malformed(true);
        check(rejected([&]{service->EditGeometry(created.Document,1,missingEnd);}),"missing finite end boundary is rejected instead of filled with a stock cap");
        check(rejected([&]{service->EditGeometry(created.Document,1,wrongOrientation);}),"wrong material-side orientation is rejected");
        check(T::Persistence::Serialize(created.Document)==originalBytes,"invalid boundary replacements leave original core and authoring bytes unchanged");
        auto editedConstruction=created.Document.Construction;std::get<T::SExtrudedRegionNode>(editedConstruction.SolidNodes[2].Data).First=9.5;
        const auto graphEdit=service->Edit(created.Document,1,editedConstruction);
        check(std::abs(Volume(Native(graphEdit.Evaluation.Shape))-originalVolume-48)<1e-5,"construction Edit compiles a new actual boundary core");
        auto graphEditCore=graphEdit.Document;graphEditCore.Construction={};graphEditCore.Bodies.clear();
        check(Equivalent(Native(graphEdit.Evaluation.Shape),Native(service->Evaluate(graphEditCore).Shape)),"edited geometry remains complete after its authoring rules are removed");
        auto holeSketch=service->ReadSketch(created.Document,"hole");
        std::get<G::Circle2>(std::get<T::CPlanarRegion2>(holeSketch.Geometry).Boundaries[0].Curves[0].Segment.Curve).Radius=5;
        const auto sketchEdit=service->EditSketch(created.Document,holeSketch);
        check(std::abs(Volume(Native(sketchEdit.Evaluation.Shape))-(44800-pi*50-96))<1e-5,"feature sketch edit regenerates actual machining boundary geometry");
        const auto sectionEdit=service->SetSectionDimension(created.Document,1,"section","outer",iCAX::CAD::ETubeSectionDimension::Width,44);
        check(std::abs(Volume(Native(sectionEdit.Evaluation.Shape))-originalVolume-16000)<1e-5,"stable core section entry rebuilds material after a width edit");
        check(T::Persistence::Serialize(created.Document)==originalBytes,"all edit routes preserve original input document bytes");
        gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),.37);rotation.SetTranslationPart(gp_Vec(70,-31,20));
        const auto rotated=BRepBuilderAPI_Transform(original,rotation,true).Shape();
        const auto importedSource=O::ConvertOpenCascadeShapeToBRep(rotated,"","",0.025);const auto sourceBytes=G::Persistence::Serialize(importedSource);
        const auto imported=service->Import(importedSource);
        check(Equivalent(rotated,Native(imported.Evaluation.Shape)),"rotated Import restores original document placement from normalized X core");
        check(G::Persistence::Serialize(importedSource)==sourceBytes,"Import leaves supplied source CAD bytes unchanged");
        auto importedCore=imported.Document;importedCore.Construction={};importedCore.Bodies.clear();
        check(Equivalent(rotated,Native(service->Evaluate(importedCore).Shape)),"rotated imported core remains complete without recognized construction bodies");
        if(argc>2) {
            const auto stepPath=std::filesystem::absolute(std::filesystem::path(argv[2]));
            STEPControl_Reader reader;
            check(reader.ReadFile(stepPath.string().c_str())==IFSelect_RetDone,"real source STEP parses successfully");
            check(reader.TransferRoots()>0&&!reader.OneShape().IsNull()&&BRepCheck_Analyzer(reader.OneShape()).IsValid(),"real source STEP transfers valid geometry");
            const auto realShape=reader.OneShape();const auto realNeutral=O::ConvertOpenCascadeShapeToBRep(realShape,"","",0.025);
            const auto realBefore=G::Persistence::Serialize(realNeutral);const auto realImported=service->Import(realNeutral);
            check(Equivalent(realShape,Native(realImported.Evaluation.Shape)),"real STEP Import restores identical material from the boundary core");
            check(G::Persistence::Serialize(realNeutral)==realBefore,"real STEP Import leaves source neutral CAD bytes unchanged");
            auto realCore=realImported.Document;realCore.Construction={};realCore.Bodies.clear();
            check(Equivalent(realShape,Native(service->Evaluate(realCore).Shape)),"real STEP core remains complete after recognized construction resources are removed");
            const auto realBytes=T::Persistence::Serialize(realCore);const auto realFile=directory/"real-step-core.ictubecad";
            {std::ofstream file(realFile,std::ios::binary);file.write(reinterpret_cast<const char*>(realBytes.data()),realBytes.size());}
            std::ifstream file(realFile,std::ios::binary);const std::vector<std::uint8_t> realDisk((std::istreambuf_iterator<char>(file)),{});
            const auto realReopened=T::Persistence::DeserializeDocument(realDisk);
            check(T::Persistence::Serialize(realReopened)==realBytes,"real STEP core survives actual binary file reopen exactly");
            check(Equivalent(realShape,Native(service->Evaluate(realReopened).Shape)),"reopened real STEP core restores identical complete material");
            evidence["realStepPath"]=stepPath.string();evidence["realStepVolumeMm3"]=Volume(realShape);
            evidence["realStepCoreExplicitFaces"]=int(realCore.Geometry.MachiningFaces.Faces.size());
        }
        evidence["importedPlacement"]=iCAX::CAD::TubeCadJson::Encode(imported.Document.Placement);
        evidence["originalExplicitFaces"]=int(created.Document.Geometry.MachiningFaces.Faces.size());
        evidence["directEditVolumeMm3"]=Volume(Native(direct.Evaluation.Shape));
        evidence["allInputsUnchanged"]=true;save("passed");std::cout<<"PASS "<<checks.size()<<" focused tube boundary document checks\n";return 0;
    } catch(const Standard_Failure& error){save("failed",error.GetMessageString());std::cerr<<error.GetMessageString()<<'\n';return 1;}
      catch(const std::exception& error){save("failed",error.what());std::cerr<<error.what()<<'\n';return 1;}
}
