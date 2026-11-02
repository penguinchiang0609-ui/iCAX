#include "OpenCascadeResourceImport/TubeCadDocumentService.h"
#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include "TubeDesigner/FinalGeometryMeasurement.h"
#include "GeometryData/BRepPersistence.h"
#include "OpenCascadeResourceImport/CadBRepImportService.h"
#include <STEPControl_Writer.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <set>
#include "Data/VariantSerializer.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourcePersistenceRegistrationCatalog.h"
#include "ProjectFile/ProjectFile.h"
#include "Database/IRepository.h"
#include "Database/IMetaRegistry.h"
#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <iostream>
#include <filesystem>
#include <cmath>
namespace G=iCAX::GeometryData;namespace T=G::Tube;
void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class F>void Reject(F&& action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Require(rejected,"Expected rejected edit");}
TopoDS_Shape Native(const G::BRepModel& model){const auto r=iCAX::OpenCascade::BuildOpenCascadeShape(model);Require(r.bOK&&!r.Shape.IsNull()&&BRepCheck_Analyzer(r.Shape).IsValid(),"Cannot rebuild valid neutral output");return r.Shape;}
double Volume(const TopoDS_Shape& shape){GProp_GProps props;BRepGProp::VolumeProperties(shape,props,1e-10);return std::abs(props.Mass());}
void Equivalent(const TopoDS_Shape& a,const TopoDS_Shape& b){
    const auto difference=[](const TopoDS_Shape& left,const TopoDS_Shape& right){
        BRepAlgoAPI_Cut cut;
        NCollection_List<TopoDS_Shape> arguments,tools;
        arguments.Append(BRepBuilderAPI_Copy(left,true,false).Shape());
        tools.Append(BRepBuilderAPI_Copy(right,true,false).Shape());
        cut.SetArguments(arguments);cut.SetTools(tools);cut.SetNonDestructive(true);cut.SetToFillHistory(false);cut.Build();
        Require(cut.IsDone()&&!cut.Shape().IsNull()&&BRepCheck_Analyzer(cut.Shape()).IsValid(),"Invalid independent document comparison");
        return Volume(cut.Shape());
    };
    const auto allowed=std::max(1e-9,std::max(Volume(a),Volume(b))*1e-8);
    Require(difference(a,b)+difference(b,a)<=allowed,"Directed geometry difference after neutral roundtrip");
}
T::SRegionLoop2 Rectangle(const std::string& id,double width,double height,bool inner=false){
    T::SRegionLoop2 loop;loop.ID=id;std::vector<G::Point2> points{{-width/2,-height/2},{width/2,-height/2},{width/2,height/2},{-width/2,height/2}};
    if(inner)std::reverse(points.begin(),points.end());
    for(std::size_t i=0;i<points.size();++i){G::CurveSegment2 segment;segment.Curve=G::Segment2{points[i],points[(i+1)%4]};loop.Curves.push_back({id+"/"+std::to_string(i),segment});}return loop;
}
T::CTubeNeutralGeometry Fixture(){
    T::CTubeNeutralGeometry model;model.BaseNodeID="stock";model.RootNodeID="part";model.RecognitionStatus=T::ERecognitionStatus::Exact;
    T::SExtrudedRegionNode base;base.Last=200;base.Section.Boundaries={Rectangle("outer",40,20),Rectangle("inner",36,16,true)};
    base.Frame.XDirection={0,1,0};base.Frame.YDirection={0,0,1};base.Frame.ZDirection={1,0,0};
    base.SideAtlases.push_back({"outer-atlas","outer",T::ESideAtlasSeamRule::MinimumXThenY,120,0,200});
    model.SolidNodes.push_back({"stock","管坯",base});
    T::SExtrudedRegionNode cut;cut.Frame.Location={60,0,0};cut.First=8;cut.Last=12;
    G::CurveSegment2 circle;G::Circle2 c;c.Radius=4;circle.Curve=c;circle.Range={0,2*std::acos(-1.0),false};
    cut.Section.Boundaries.push_back({"hole-contour",{{"circle",circle}}});
    model.SolidNodes.push_back({"hole","共享孔",cut});
    T::SBooleanNode body;body.Operation=T::EBooleanOperation::Difference;body.Children={"stock","hole"};
    model.SolidNodes.push_back({"part","管材零件",body});return model;
}
int main(int argc,char** argv){
try{
    iCAX::Services::CServiceProvider services;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
    const auto service=services.Resolve<iCAX::CAD::ITubeCadDocumentService>();Require(bool(service),"Service registration missing");
    auto created=service->Create(Fixture());
    {
        // Imported face collections retain their exact records and arbitrary
        // identities while acquiring display data; ordinals are not face IDs.
        auto boundary=created.Evaluation.Shape;
        boundary.Triangulations3.clear();boundary.Shells.clear();boundary.Solids.clear();
        boundary.CompSolids.clear();boundary.Compounds.clear();boundary.RootShapes.clear();
        boundary.RootFaceIds.clear();boundary.RootShellIds.clear();boundary.RootSolidIds.clear();
        std::set<std::uint64_t> faceIDs;
        for(auto& face:boundary.Faces) {
            face.Id=1000+17*face.Id;face.Triangulation3Id=0;
            boundary.RootFaceIds.push_back(face.Id);faceIDs.insert(face.Id);
            boundary.RootShapes.push_back({G::EBRepShapeKind::Face,face.Id,face.Orientation,{}});
        }
        const auto exactBytes=G::Persistence::Serialize(boundary);
        T::CTubeNeutralGeometry graph;graph.BaseNodeID=graph.RootNodeID="boundary";
        graph.SolidNodes.push_back({"boundary","Boundary",T::SResidualBRepNode{"body"}});
        graph.RecognitionStatus=T::ERecognitionStatus::Partial;
        // A transported full-body residual is no longer a tube geometry
        // authority. A fixed-section definition is required to compile it.
        Reject([&]{service->Create(graph,{{"body",boundary}});});
        Require(G::Persistence::Serialize(boundary)==exactBytes,"Rejected construction changed its source");
        auto coreOnly=created.Document;coreOnly.Construction={};coreOnly.Bodies.clear();
        const auto reopened=service->Evaluate(T::Persistence::DeserializeDocument(T::Persistence::Serialize(coreOnly)));
        Require(reopened.Shape.Solids.size()==created.Evaluation.Shape.Solids.size(),"Core-only reopen lost the recovered solid");
        Require(std::abs(Volume(Native(reopened.Shape))-Volume(Native(created.Evaluation.Shape)))<1e-7,"Core-only reopen changed volume");
        const auto shape=Native(created.Evaluation.Shape);
        const auto generic=iCAX::TubeDesigner::MeasureFinalPartGeometry(shape,"fixture",1);
        const auto aligned=iCAX::TubeDesigner::MeasureFinalPartGeometry(shape,"fixture",1,true);
        Require(std::abs(generic.at("length").To<double>()-aligned.at("length").To<double>())<1e-7,"Known-axis measurement changed axial extent");
        Require(aligned.at("available").To<bool>(),"Known-axis final geometry measurement unavailable");
        std::cout<<"PASS residual authority rejection, core-only restore and reopen, known-axis measurement\n";
    }
    const auto expected=44800-std::acos(-1.0)*16*2;
    Require(std::abs(Volume(Native(created.Evaluation.Shape))-expected)<1e-3,"Neutral construction volume incorrect");
    // Authored tools may preserve a full circle as several exact trimmed
    // Circle2 segments. Parameter interpretation must respect the whole loop
    // without replacing its construction curves or ranges.
    auto quartered=Fixture();
    auto& quarterLoop=std::get<T::SExtrudedRegionNode>(quartered.SolidNodes[1].Data).Section.Boundaries[0];
    quarterLoop.Curves.clear();
    for(int index=0;index<4;++index) {
        const double angle=index*std::acos(-1.0)/2;
        G::Circle2 circle;circle.Radius=4;
        circle.Placement.XDirection={std::cos(angle),std::sin(angle)};
        circle.Placement.YDirection={-std::sin(angle),std::cos(angle)};
        G::CurveSegment2 segment;segment.Curve=circle;segment.Range={0,std::acos(-1.0)/2,false};
        quarterLoop.Curves.push_back({"quarter/"+std::to_string(index),segment});
    }
    const auto quarteredDocument=service->Create(quartered);
    const auto& quarteredPrimitive=std::get<T::SExtrudedRegionNode>(quarteredDocument.Document.Construction.SolidNodes[1].Data);
    Require(quarteredPrimitive.SectionPrimitives.size()==1
        &&quarteredPrimitive.SectionPrimitives[0].Kind==T::ESectionPrimitiveKind::Circle,
        "A complete exact four-arc circle lost its circle interpretation");
    const auto quarteredResize=service->SetSectionDimension(quarteredDocument.Document,1,"hole","hole-contour",
        iCAX::CAD::ETubeSectionDimension::Radius,7);
    const auto& resizedQuarters=std::get<T::SExtrudedRegionNode>(quarteredResize.Document.Construction.SolidNodes[1].Data).Section.Boundaries[0].Curves;
    Require(resizedQuarters.size()==4,"Circle dimension replaced exact authoring curves");
    for(std::size_t index=0;index<resizedQuarters.size();++index) {
        const auto& original=quarterLoop.Curves[index];const auto& changed=resizedQuarters[index];
        Require(changed.ID==original.ID&&changed.Segment.Range.First==original.Segment.Range.First
            &&changed.Segment.Range.Last==original.Segment.Range.Last&&changed.Segment.Range.Reversed==original.Segment.Range.Reversed
            &&std::get<G::Circle2>(changed.Segment.Curve).Radius==7,"Circle dimension dropped exact segment identity or range");
    }
    Require(std::abs(Volume(Native(quarteredResize.Evaluation.Shape))-(44800-std::acos(-1.0)*49*2))<1e-3,
        "Exact multi-arc radius edit did not affect the spatial body");
    auto semicircle=Fixture();
    auto& halfLoop=std::get<T::SExtrudedRegionNode>(semicircle.SolidNodes[1].Data).Section.Boundaries[0];
    halfLoop.Curves[0].Segment.Range.Last=std::acos(-1.0);
    G::CurveSegment2 chord;chord.Curve=G::Segment2{{-4,0},{4,0}};
    halfLoop.Curves.push_back({"chord",chord});
    const auto halfDocument=service->Create(semicircle);
    Require(std::get<T::SExtrudedRegionNode>(halfDocument.Document.Construction.SolidNodes[1].Data).SectionPrimitives[0].Kind
        ==T::ESectionPrimitiveKind::CurveLoop,"An incomplete arc loop was falsely promoted to a complete circle");
    Reject([&]{service->SetSectionDimension(halfDocument.Document,1,"hole","hole-contour",
        iCAX::CAD::ETubeSectionDimension::Radius,7);});
    auto withNewHole=created.Document.Construction;
    auto newHole=withNewHole.SolidNodes[1];newHole.ID="new-through";
    auto& newExtrusion=std::get<T::SExtrudedRegionNode>(newHole.Data);
    newExtrusion.Frame.Location.X=140;newExtrusion.First=-100;newExtrusion.Last=100;
    std::get<G::Circle2>(newExtrusion.Section.Boundaries[0].Curves[0].Segment.Curve).Radius=3;
    newHole.Relations.emplace();newHole.Relations->Extent.emplace();
    auto& newExtent=*newHole.Relations->Extent;
    newExtent.HostNodeID="stock";newExtent.Rule=T::EFeatureExtentRule::ThroughAllHostMaterial;
    newExtent.RecomputeAgainstHost=true;
    withNewHole.SolidNodes.push_back(newHole);
    std::get<T::SBooleanNode>(withNewHole.SolidNodes[2].Data).Children.push_back("new-through");
    const auto inserted=service->Edit(created.Document,1,withNewHole);
    const auto& insertedExtrusion=std::get<T::SExtrudedRegionNode>(inserted.Document.Construction.SolidNodes.back().Data);
    Require(insertedExtrusion.First<-10&&insertedExtrusion.First>-10.02&&insertedExtrusion.Last>10&&insertedExtrusion.Last<10.02,
        "New through feature did not derive its first extent from the actual host");
    Require(std::abs(Volume(Native(inserted.Evaluation.Shape))-(44800-std::acos(-1.0)*16*2-std::acos(-1.0)*9*4))<1e-3,
        "New through feature removed the wrong host material");
    withNewHole.SolidNodes.back().Relations->Extent->Rule=T::EFeatureExtentRule::ThroughSelectedHostBoundaries;
    Reject([&]{service->Edit(created.Document,1,withNewHole);});
    auto& selectedSeed=std::get<T::SExtrudedRegionNode>(withNewHole.SolidNodes.back().Data);
    selectedSeed.First=8;selectedSeed.Last=12;
    withNewHole.SolidNodes.back().Relations->Extent->PrimitiveFirst.Value=8;
    withNewHole.SolidNodes.back().Relations->Extent->PrimitiveLast.Value=12;
    const auto insertedSelected=service->Edit(created.Document,1,withNewHole);
    Require(std::abs(Volume(Native(insertedSelected.Evaluation.Shape))-(44800-std::acos(-1.0)*16*2-std::acos(-1.0)*9*2))<1e-3,
        "A new exact selected cutter did not retain its single observed host wall");
    auto& newBlindExtent=*withNewHole.SolidNodes.back().Relations->Extent;
    newBlindExtent.Rule=T::EFeatureExtentRule::BlindFromHostBoundary;
    newBlindExtent.Direction=T::EExtentDirection::Negative;newBlindExtent.Depth=T::SScalarParameter{0.75};
    const auto insertedBlind=service->Edit(created.Document,1,withNewHole);
    Require(std::abs(Volume(Native(insertedBlind.Evaluation.Shape))-(44800-std::acos(-1.0)*16*2-std::acos(-1.0)*9*0.75))<1e-3,
        "A new blind cutter did not retain its explicit depth from the observed opening");
    auto geometry=created.Document.Construction;
    auto& cut=std::get<T::SExtrudedRegionNode>(geometry.SolidNodes[1].Data);cut.Frame.Location.X=75;
    std::get<G::Circle2>(cut.Section.Boundaries[0].Curves[0].Segment.Curve).Radius=5;
    auto edited=service->Edit(created.Document,1,geometry);
    const auto views=service->ReadSketches(edited.Document,{"hole","stock"});
    Require(views.size()==2&&views[0].NodeID=="hole"&&views[1].NodeID=="stock"
        &&views[0].Revision==edited.Document.Revision,"Batch projection lost order, identity or revision");
    const auto singleView=service->ReadSketch(edited.Document,"hole");
    Require(iCAX::Data::VariantSerializer::Serialize(iCAX::CAD::TubeCadJson::Encode(views[0].Geometry))
        ==iCAX::Data::VariantSerializer::Serialize(iCAX::CAD::TubeCadJson::Encode(singleView.Geometry)),
        "Batch projection changed the exact single-feature curves");
    Require(views[0].ConstructionFrame&&views[0].ConstructionFrame->Location.X==75,
        "Batch projection lost the 3D placement");
    Reject([&]{service->ReadSketches(edited.Document,{"hole","missing"});});
    auto invalidRead=edited.Document;invalidRead.Unit="inch";
    Reject([&]{service->ReadSketches(invalidRead,{"hole"});});
    Require(edited.Document.Revision==2,"Revision not advanced");
    Require(std::abs(Volume(Native(edited.Evaluation.Shape))-(44800-std::acos(-1.0)*25*2))<1e-3,"Geometric node edit was ignored");
    Reject([&]{service->Edit(edited.Document,1,geometry);});
    const auto before=T::Persistence::Serialize(edited.Document);
    auto cyclic=geometry;std::get<T::SBooleanNode>(cyclic.SolidNodes[2].Data).Children.push_back("part");
    Reject([&]{service->Edit(edited.Document,2,cyclic);});
    Require(before==T::Persistence::Serialize(edited.Document),"Failed edit mutated source");
    auto constrained=Fixture();
    T::SFeatureRelations relations;T::SFeatureExtentConstraint extent;
    extent.HostNodeID="stock";extent.Rule=T::EFeatureExtentRule::ThroughSelectedHostBoundaries;
    extent.RecomputeAgainstHost=true;extent.PrimitiveFirst.Value=8;extent.PrimitiveLast.Value=12;
    relations.Extent=extent;constrained.SolidNodes[1].Relations=relations;
    const auto dependent=service->Create(constrained);
    auto largerSection=dependent.Document.Construction;
    std::get<T::SExtrudedRegionNode>(largerSection.SolidNodes[0].Data).Section.Boundaries={Rectangle("outer",40,24),Rectangle("inner",36,20,true)};
    const auto resized=service->Edit(dependent.Document,1,largerSection);
    const auto& resolved=std::get<T::SExtrudedRegionNode>(resized.Document.Construction.SolidNodes[1].Data);
    Require(resolved.First<10&&resolved.First>9.98&&resolved.Last>12&&resolved.Last<12.02,"Through extent did not follow changed host: "+std::to_string(resolved.First)+", "+std::to_string(resolved.Last));
    Require(std::abs(Volume(Native(resized.Evaluation.Shape))-(48000-std::acos(-1.0)*16*2))<1e-3,"Host change cut the wrong material span");
    auto blindGeometry=constrained;
    std::get<T::SExtrudedRegionNode>(blindGeometry.SolidNodes[1].Data).First=9;
    auto& blindExtent=*blindGeometry.SolidNodes[1].Relations->Extent;
    blindExtent.Rule=T::EFeatureExtentRule::BlindFromHostBoundary;blindExtent.Direction=T::EExtentDirection::Negative;
    blindExtent.Depth.emplace();blindExtent.Depth->Value=1;
    const auto blind=service->Create(blindGeometry);
    auto changedBlind=blind.Document.Construction;
    std::get<T::SExtrudedRegionNode>(changedBlind.SolidNodes[0].Data).Section.Boundaries={Rectangle("outer",40,24),Rectangle("inner",36,20,true)};
    changedBlind.SolidNodes[1].Relations->Extent->Depth->Value=0.75;
    const auto movedBlind=service->Edit(blind.Document,1,changedBlind);
    Require(std::abs(Volume(Native(movedBlind.Evaluation.Shape))-(48000-std::acos(-1.0)*16*0.75))<1e-3,"Explicit blind depth did not follow the new entry boundary");
    auto sketch=service->ReadSketch(edited.Document,"hole");
    auto& sketchRegion=std::get<T::CPlanarRegion2>(sketch.Geometry);
    std::get<G::Circle2>(sketchRegion.Boundaries[0].Curves[0].Segment.Curve).Radius=6;
    const auto from2D=service->EditSketch(edited.Document,sketch);
    const auto& hole3D=std::get<T::SExtrudedRegionNode>(from2D.Document.Construction.SolidNodes[1].Data);
    Require(hole3D.Frame.Location.X==75&&std::get<G::Circle2>(hole3D.Section.Boundaries[0].Curves[0].Segment.Curve).Radius==6,"2D edit did not update the same spatial feature");
    Require(hole3D.SectionPrimitives.size()==1&&hole3D.SectionPrimitives[0].Radius&&hole3D.SectionPrimitives[0].Radius->Value==6,"2D edit left parameter interpretation stale");
    Require(from2D.Document.Construction.SolidNodes[1].ID=="hole","2D edit replaced feature identity");
    Reject([&]{service->EditSketch(from2D.Document,sketch);});
    const auto fromParameters=service->SetSectionDimension(from2D.Document,from2D.Document.Revision,"hole","hole-contour",iCAX::CAD::ETubeSectionDimension::Radius,7);
    const auto parameterSketch=service->ReadSketch(fromParameters.Document,"hole");
    Require(std::get<G::Circle2>(std::get<T::CPlanarRegion2>(parameterSketch.Geometry).Boundaries[0].Curves[0].Segment.Curve).Radius==7,"Parameter edit did not reach the 2D source curve");
    Require(std::abs(Volume(Native(fromParameters.Evaluation.Shape))-(44800-std::acos(-1.0)*49*2))<1e-3,"Parameter edit did not reach the 3D body");
    const auto json=iCAX::Data::VariantSerializer::Serialize(iCAX::CAD::TubeCadJson::Encode(edited.Document));
    const auto decoded=iCAX::CAD::TubeCadJson::Decode<T::CTubeCadDocument>(iCAX::Data::VariantSerializer::Deserialize(json));
    Require(T::Persistence::Serialize(decoded)==before,"Editor transport dropped document information");
    auto beveledGeometry=edited.Document.Construction;
    T::SOpeningProfileSweepNode bevel;bevel.SourceCutNodeID="hole";bevel.ProfileField.Periodic=true;
    bevel.ProfileField.ContourLast=2*std::acos(-1.0)*5;
    T::SOpeningProfileKey key;G::CurveSegment2 line;line.Curve=G::Segment2{{8.5,0},{10,1.5}};key.Profile.Segments={line};
    bevel.ProfileField.Keys={key};beveledGeometry.SolidNodes.push_back({"bevel","开口剖面",bevel});
    std::get<T::SBooleanNode>(beveledGeometry.SolidNodes[2].Data).Children.push_back("bevel");
    auto beveled=service->Edit(edited.Document,2,beveledGeometry);
    Require(Volume(Native(beveled.Evaluation.Shape))<Volume(Native(edited.Evaluation.Shape))-1,"Authoritative bevel section had no effect");
    auto changedProfile=beveled.Document.Construction;
    auto& sweep=std::get<T::SOpeningProfileSweepNode>(changedProfile.SolidNodes.back().Data);
    std::get<G::Segment2>(sweep.ProfileField.Keys[0].Profile.Segments[0].Curve).End.Y=0.75;
    const auto smaller=service->Edit(beveled.Document,3,changedProfile);
    Require(Volume(Native(smaller.Evaluation.Shape))>Volume(Native(beveled.Evaluation.Shape))+1,"Bevel edit reused stale evaluated volume");
    const auto persisted=T::Persistence::Serialize(smaller.Document);
    Equivalent(Native(smaller.Evaluation.Shape),Native(service->Evaluate(T::Persistence::DeserializeDocument(persisted)).Shape));
    // Exercise the exact production module codec, separate from direct codec tests.
    Require(argc>2,"Source and plugin path arguments required");
    {
        const auto importer=services.Resolve<iCAX::CAD::ICadBRepImportService>();
        Require(bool(importer),"Source import service missing");
        const auto path=std::filesystem::absolute(std::filesystem::path(argv[1])/"output/tests/tube-neutral-model/import-snapshot.step");
        std::filesystem::create_directories(path.parent_path());
        const auto write=[&](const TopoDS_Shape& shape) {
            STEPControl_Writer writer;Require(writer.Transfer(shape,STEPControl_AsIs)==IFSelect_RetDone,"Snapshot fixture transfer failed");
            Require(writer.Write(path.string().c_str())==IFSelect_RetDone,"Snapshot fixture write failed");
        };
        const auto shape=Native(created.Evaluation.Shape);write(shape);
        iCAX::CAD::SCadBRepImportRequest request;request.SourcePath=path;request.IncludeDisplayMesh=false;
        const auto first=importer->Load(request),second=importer->Load(request);
        Require(G::Persistence::Serialize(first.Geometry)==G::Persistence::Serialize(second.Geometry),"Snapshot changed exact geometry");
        auto changedCopy=second;changedCopy.Geometry.Faces.front().Tolerance+=1;
        Require(G::Persistence::Serialize(importer->Load(request).Geometry)==G::Persistence::Serialize(first.Geometry),"Caller mutation contaminated source snapshot");
        const auto timestamp=std::filesystem::last_write_time(path);
        gp_Trsf scale;scale.SetScale(gp_Pnt(),2);write(BRepBuilderAPI_Transform(shape,scale,true).Shape());
        std::filesystem::last_write_time(path,timestamp);
        const auto changed=importer->Load(request);
        Require(changed.SourceSha256!=first.SourceSha256,"Changed source reused a stale byte snapshot");
        Require(std::abs((changed.Bounds.Max.X-changed.Bounds.Min.X)-2*(first.Bounds.Max.X-first.Bounds.Min.X))<1e-6,"Same-timestamp source change was not imported");
        std::cout<<"PASS immutable source snapshot, identical geometry, same-timestamp content invalidation\n";
    }
    const std::string pluginPath=argv[2];
    iCAX::Resource::CResourceLibrary pool;
    iCAX::Resource::CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(pool,{pluginPath});
    iCAX::Resource::CResourceInfo info;info.Key=iCAX::Resource::CResourceKey{"memory://tube-cad"};
    info.ResourceTypeID=T::CTubeCadDocument::kResourceTypeName;info.Persistence=iCAX::Resource::EResourcePersistenceMode::Embedded;info.nVersion=1;info.nSchemaVersion=1;
    pool.Set<T::CTubeCadDocument>(info.Key.Source,std::make_shared<T::CTubeCadDocument>(smaller.Document),info);
    const auto payload=pool.SerializePersistentVersion({info.Key.Source,pool.GetVersion(info.Key.Source)});
    iCAX::Resource::CResourceLibrary reopened;iCAX::Resource::CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(reopened,{pluginPath});
    reopened.RestorePersistentVersion(payload);const auto restored=reopened.Get<T::CTubeCadDocument>(info.Key.Source,1);
    Require(restored&&T::Persistence::Serialize(*restored)==persisted,"Registered codec lost tube model");
    const auto sceneID=iCAX::Data::GenerateNewUUID(),projectID=iCAX::Data::GenerateNewUUID();
    const auto path=std::filesystem::path(argv[1])/"output/tests/tube-neutral-model/document-roundtrip.ictd";
    iCAX::ProjectFile::CProjectFile projectFile({.Magic="ICAX-TUBE-CAD-TEST",.ProductID="tube-cad-test",.CurrentFormatVersion="1.0",.nCurrentFormatRevision=1});
    {
        auto database=iCAX::Database::GenerateRepository(sceneID,iCAX::Database::CreateMetaRegistry());
        iCAX::ProjectFile::CProjectDocumentInfo project;
        project.ProjectID=projectID;project.MainSceneID=sceneID;project.ProjectName="管材中性文档";
        projectFile.Save(path,project,*database,pool);
    }
    {
        // A fresh repository and library exercise disk persistence, not a
        // process-local resource cache or just the direct serializer.
        iCAX::Resource::CResourceLibrary resources;
        iCAX::Resource::CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(resources,{pluginPath});
        auto database=iCAX::Database::GenerateRepository(sceneID,iCAX::Database::CreateMetaRegistry());
        const auto opened=projectFile.Open(path,*database,resources);
        const auto document=resources.Get<T::CTubeCadDocument>(info.Key.Source,1);
        Require(opened.Info.ProjectID==projectID&&document&&T::Persistence::Serialize(*document)==persisted,"Project reopen lost the authoritative document");
        Equivalent(Native(smaller.Evaluation.Shape),Native(service->Evaluate(*document).Shape));
        auto next=document->Construction;
        std::get<T::SExtrudedRegionNode>(next.SolidNodes[1].Data).Frame.Location.X=95;
        const auto continued=service->Edit(*document,document->Revision,next);
        Require(continued.Document.Revision==document->Revision+1,"Reopened document cannot be edited");
    }
    std::cout<<"PASS authored graph, 3D/2D/shared parameter edits, dependent wall extent, exact transport, bevel re-evaluation, revisions, failed-edit atomicity, project save/reopen/continue\n";
    if(argc>1&&(argc<4||std::string(argv[3])!="--no-step"))for(const auto* file:{"samples/tube-geometry/03_round_tube_through_hole.step",
        "samples/tube-geometry/05_double_cavity_blind_hole.step", "samples/tube-geometry/08_double_cavity_beveled_hole.step",
        "samples/manual-acceptance/machined-step/04_REV-21-3558.step"}){
        const auto path=std::filesystem::path(argv[1])/file;STEPControl_Reader reader;
        Require(reader.ReadFile(path.string().c_str())==IFSelect_RetDone,"STEP read failed");reader.TransferRoots();
        const auto source=reader.OneShape();const auto neutral=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(source,"","",0.025);
        const auto imported=service->Import(neutral);const auto bytes=T::Persistence::Serialize(imported.Document);
        const auto replay=service->Evaluate(T::Persistence::DeserializeDocument(bytes));Equivalent(source,Native(replay.Shape));
        std::cout<<"PASS STEP "<<file<<" nodes="<<imported.Document.Construction.SolidNodes.size()<<"\n";
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
