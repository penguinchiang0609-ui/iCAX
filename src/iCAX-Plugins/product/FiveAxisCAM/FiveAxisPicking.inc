// Geometry picking has no alignment constraints or placement mutations.
namespace {
    struct PickObject {
        Map GeometryRef;std::array<double,16> Transform;std::shared_ptr<const iCAX::GeometryData::BRepModel> Geometry;
        std::shared_ptr<CFiveAxisWorkpieceComponent> Workpiece;
    };
    PickObject ReadPickObject(Scene& scene,const ID& id,const ID& setup){
        const auto entity=scene.Database().GetEntity(id);PickObject object;
        if(const auto p=Component<CFiveAxisPlacementComponent>(entity)){
            if(p->GetSetupID()!=setup)throw std::invalid_argument("CAM_SETUP_MISMATCH");
            object.Workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
            object.GeometryRef=object.Workpiece->GetGeometryRef();object.Transform=RigidMatrix(p->GetTransform());
        }else if(const auto f=Component<CFiveAxisFixtureComponent>(entity)){
            if(f->GetSetupID()!=setup)throw std::invalid_argument("CAM_SETUP_MISMATCH");
            object.GeometryRef=f->GetGeometryRef();object.Transform=RigidMatrix(f->GetTransform());
        }else throw std::invalid_argument("CAM_PICK_OBJECT_REQUIRED");
        const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(object.GeometryRef);
        if(!ref)throw std::runtime_error("CAM_GEOMETRY_MISSING");
        object.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL,ref->nVersion);
        if(!object.Geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");return object;
    }
    Text SnapMode(iCAX::Geometry::ESnapFeatureKind kind){
        using K=iCAX::Geometry::ESnapFeatureKind;
        switch(kind){case K::Endpoint:return "endpoint";case K::Midpoint:return "midpoint";case K::CircleCenter:return "center";case K::SphereCenter:return "sphere-center";case K::Edge:return "edge";case K::Line:return "line";case K::Plane:return "plane";case K::Axis:return "axis";case K::Center:return "datum-point";case K::Direction:return "direction";}
        throw std::invalid_argument("CAD_SNAP_FEATURE_KIND");
    }
    std::shared_ptr<const iCAX::Geometry::SCadSnapFeatures> SnapCatalog(Scene& scene,const PickObject& object){
        auto runtime=Runtime(scene);const auto key=iCAX::Data::VariantSerializer::Serialize(object.GeometryRef);
        if(auto found=runtime->SnapFeatures.find(key);found!=runtime->SnapFeatures.end())return found->second;
        auto catalog=std::make_shared<const iCAX::Geometry::SCadSnapFeatures>(scene.Services().Resolve<iCAX::Geometry::ICadSnapFeatureService>()->Analyze(*object.Geometry));
        if(runtime->SnapFeatures.size()>=16)runtime->SnapFeatures.clear();
        runtime->SnapFeatures.emplace(key,catalog);return catalog;
    }
    Map SnapWire(const iCAX::Geometry::SCadSnapFeature& f){
        Array faces,edges,samples,corners;for(auto id:f.FaceIds)faces.emplace_back(std::to_string(id));for(auto id:f.EdgeIds)edges.emplace_back(std::to_string(id));for(const auto& p:f.EdgeSamples)samples.emplace_back(Array{p[0],p[1],p[2]});for(const auto& p:f.PlaneCorners)corners.emplace_back(Array{p[0],p[1],p[2]});
        return {{"featureId",f.Id},{"mode",SnapMode(f.Kind)},{"faceIds",faces},{"edgeId",std::to_string(f.EdgeId)},{"vertexId",std::to_string(f.VertexId)},
            {"point",Array{f.Point[0],f.Point[1],f.Point[2]}},{"direction",Array{f.Direction[0],f.Direction[1],f.Direction[2]}},{"xDirection",Array{f.XDirection[0],f.XDirection[1],f.XDirection[2]}},{"radius",f.Radius},{"edgeSamples",samples},{"edgeIds",edges},{"label",f.Label},{"derived",f.Derived},{"boxKind",f.BoxKind},{"planeCorners",corners}};
    }
    iCAX::Geometry::SCadSnapFeature PickFeature(Scene& scene,const PickObject& object,const Map& query,int depth=0){
        if(depth>3)throw std::invalid_argument("CAD_SNAP_CONSTRUCTION_DEPTH");
        if(!query.contains("featureId")&&!query.contains("construction")&&String(query,"mode")=="point"){
            auto exact=query;if(!exact.contains("faceId")&&object.Workpiece)exact=DisplayReferenceQuery(*object.Workpiece,*object.Geometry,exact);
            const auto point=ResolveReference(scene,*object.Geometry,exact);iCAX::Geometry::SCadSnapFeature f;
            f.Id="surface-point:"+iCAX::Data::VariantSerializer::Serialize(exact);f.Kind=iCAX::Geometry::ESnapFeatureKind::Center;f.Point=RegistrationPoint(point.at("point"));f.FaceIds={Integer(point,"faceId")};f.Label="面上点";return f;
        }
        if(query.contains("construction")){
            const auto construction=query.at("construction").To<Map>();const auto kind=String(construction,"kind");using K=iCAX::Geometry::ESnapConstruction;
            const std::map<Text,K> kinds{{"ThreePointPlane",K::ThreePointPlane},{"TwoPointDirection",K::TwoPointDirection},{"PlaneU",K::PlaneU},{"PlaneV",K::PlaneV},{"PlaneCenter",K::PlaneCenter},{"PlaneNormal",K::PlaneNormal}};
            const auto found=kinds.find(kind);if(found==kinds.end())throw std::invalid_argument("CAD_SNAP_CONSTRUCTION_KIND");
            const auto inputs=construction.at("sources").To<Array>();if(inputs.size()>3)throw std::invalid_argument("CAD_SNAP_CONSTRUCTION_COUNT");
            std::vector<iCAX::Geometry::SCadSnapFeature> sources;for(const auto& source:inputs)sources.push_back(PickFeature(scene,object,source.To<Map>(),depth+1));
            const auto f=scene.Services().Resolve<iCAX::Geometry::ICadSnapFeatureService>()->Construct(found->second,sources);
            if(String(query,"mode")!=SnapMode(f.Kind))throw std::invalid_argument("CAD_SNAP_FEATURE_KIND");return f;
        }
        const auto catalog=SnapCatalog(scene,object);const auto id=String(query,"featureId");
        const auto it=std::find_if(catalog->Features.begin(),catalog->Features.end(),[&](const auto& f){return f.Id==id;});
        if(it==catalog->Features.end())throw std::invalid_argument("CAD_SNAP_FEATURE_MISSING");
        if(String(query,"mode")!=SnapMode(it->Kind))throw std::invalid_argument("CAD_SNAP_FEATURE_KIND");return *it;
    }
    Array PickFaceRanges(const PickObject& object){
        if(object.Workpiece)return DisplayFaceRanges(*object.Workpiece,*object.Geometry);
        Array ranges;UInt first=0;for(const auto& mesh:object.Geometry->Triangulations3){if(mesh.Geometry.Vertices.empty()||mesh.Geometry.Triangles.empty())continue;
            for(const auto& face:object.Geometry->Faces)if(face.Triangulation3Id==mesh.Id)ranges.emplace_back(Map{{"faceId",std::to_string(face.Id)},{"first",std::to_string(first)},{"count",std::to_string(mesh.Geometry.Triangles.size())}});
            first+=mesh.Geometry.Triangles.size();}return ranges;
    }
    Map PickReference(Scene& scene,const PickObject& object,Map query){
        Map result;
        if(query.contains("featureId")||query.contains("construction")){
            const auto feature=PickFeature(scene,object,query);
            result=SnapWire(feature);result["faceId"]=feature.FaceIds.empty()?Text("0"):std::to_string(feature.FaceIds.front());result["distanceMm"]=0.;result["query"]=query;
        }else{
        if(!query.contains("faceId")&&object.Workpiece)query=DisplayReferenceQuery(*object.Workpiece,*object.Geometry,query);
        result=ResolveReference(scene,*object.Geometry,query);
        }
        Array ranges;UInt first=0;
        if(object.Workpiece){for(const auto& value:DisplayFaceRanges(*object.Workpiece,*object.Geometry)){auto f=value.To<Map>();if(f.at("faceId")==result.at("faceId"))ranges.emplace_back(Map{{"first",f.at("first")},{"count",f.at("count")}});}}
        else for(const auto& mesh:object.Geometry->Triangulations3){if(mesh.Geometry.Vertices.empty()||mesh.Geometry.Triangles.empty())continue;
            for(const auto& face:object.Geometry->Faces)if(face.Id==Integer(result,"faceId")&&face.Triangulation3Id==mesh.Id)ranges.emplace_back(Map{{"first",std::to_string(first)},{"count",std::to_string(mesh.Geometry.Triangles.size())}});
            first+=mesh.Geometry.Triangles.size();}
        result["triangleRanges"]=ranges;
        Array ends;const auto edgeID=Integer(result,"edgeId");
        for(const auto& edge:object.Geometry->Edges)if(edge.Id==edgeID)for(const auto vertexID:{edge.StartVertexId,edge.EndVertexId})for(const auto& vertex:object.Geometry->Vertices)if(vertex.Id==vertexID)ends.emplace_back(Array{vertex.Position.X,vertex.Position.Y,vertex.Position.Z});
        if(ends.empty()&&result.contains("boxKind")&&!String(result,"boxKind").empty()&&String(result,"mode")=="line"){
            const auto samples=result.at("edgeSamples").To<Array>();if(samples.size()==2)ends=samples;
        }
        result["lineEnds"]=ends;result["geometryRef"]=WireRef(object.GeometryRef);return result;
    }
}
Map ReadSnapFeatures(Scene& scene,const Map& request){
    CheckExpected(*Runtime(scene),request);const auto object=ReadPickObject(scene,Identifier(request,"objectId"),OptionalIdentifier(request,"setupId"));
    const auto catalog=SnapCatalog(scene,object);
    const bool resource=request.contains("transport")&&String(request,"transport")=="resource";
    if(request.contains("transport")&&!resource)throw std::invalid_argument("CAM_SNAP_TRANSPORT_INVALID");
    auto runtime=Runtime(scene);
    const auto key=iCAX::Data::VariantSerializer::Serialize(Map{{"geometry",object.GeometryRef},{"preview",object.Workpiece?WireRef(object.Workpiece->GetPreviewRef()):Map{}}});
    const auto descriptor=[&](const Ref& ref){return Map{{"geometryRef",WireRef(object.GeometryRef)},{"catalogRef",WireRef(StoredRef(ref))},{"featureCount",static_cast<UInt>(catalog->Features.size())}};};
    if(resource)if(const auto found=runtime->SnapCatalogResources.find(key);found!=runtime->SnapCatalogResources.end())return descriptor(found->second);
    Array features;for(const auto& feature:catalog->Features)features.emplace_back(SnapWire(feature));
    Map result{{"geometryRef",WireRef(object.GeometryRef)},{"features",features},{"faces",PickFaceRanges(object)}};
    if(!resource)return result;
    // Dense BReps exceed the bounded SDO message queue. Keep all exact feature
    // identities and display samples in an immutable resource, not that queue.
    const auto bytes=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(result);
    flatbuffers::FlatBufferBuilder builder;const auto json=builder.CreateString(bytes);
    const auto table=builder.StartTable();builder.AddElement<std::uint32_t>(4,1,0);builder.AddOffset(6,json);
    builder.Finish(flatbuffers::Offset<void>(builder.EndTable(table)),"ICSC");
    auto body=std::make_shared<iCAX::Resource::CFlatBufferResource>(std::vector<std::uint8_t>(builder.GetBufferPointer(),builder.GetBufferPointer()+builder.GetSize()));
    iCAX::Resource::CResourceInfo info;info.Name="几何吸附特征";info.ResourceTypeID="resource.flatbuffer";info.nVersion=1;info.nSchemaVersion=1;info.nMinimumReaderVersion=1;
    info.Persistence=iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;const auto sourceRef=iCAX::ProjectFile::TryGetResourceReferenceValue(object.GeometryRef);info.Dependencies.push_back(Ref{sourceRef->URL,sourceRef->nVersion});
    Ref ref{scene.Resources().AllocateResourceURL(),1};scene.Resources().Set(ref.URL,body,info);
    if(runtime->SnapCatalogResources.size()>=16)runtime->SnapCatalogResources.clear();
    runtime->SnapCatalogResources.emplace(key,ref);return descriptor(ref);
}
Map ResolvePickReference(Scene& scene,const Map& request){
    CheckExpected(*Runtime(scene),request);const auto object=ReadPickObject(scene,Identifier(request,"objectId"),OptionalIdentifier(request,"setupId"));
    auto query=request.at("query").To<Map>();
    if(String(query,"mode")!="auto")return PickReference(scene,object,query);
    for(const auto mode:{"endpoint","line","center","plane","axis","point"}){
        query["mode"]=Text(mode);try{return PickReference(scene,object,query);}catch(const std::invalid_argument& error){
            const Text message=error.what();if(message.find("CAD_REFERENCE_NOT_FOUND")!=0&&message.find("CAD_REFERENCE_TOO_FAR")!=0&&message.find("CAD_REFERENCE_REQUIRES_")!=0&&message.find("CAD_REFERENCE_AMBIGUOUS")!=0)throw;
        }
    }
    throw std::invalid_argument("CAD_REFERENCE_NOT_FOUND");
}
