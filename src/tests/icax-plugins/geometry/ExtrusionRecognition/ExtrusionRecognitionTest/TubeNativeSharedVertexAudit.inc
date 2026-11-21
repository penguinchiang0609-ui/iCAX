#include <ElCLib.hxx>

// stod rejects representable subnormal parameters through ERANGE. Preserve the
// exact original double domain instead of dropping its positive endpoint tail.
double ParseNativeIntervalParameter(const std::string& text)
{
    double value=0.;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(value))
        throw std::runtime_error("Invalid finite native interval parameter: "+text);
    return value;
}

int NativeCurveIntervalAudit(const fs::path& source,const std::string& intervalList,const fs::path& output)
{
    std::ifstream input(source,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    const auto geometry=G::Persistence::Deserialize(bytes);const auto built=O::ReconstructOpenCascadeShape(geometry);
    if(!built.bOK)throw std::runtime_error("Cannot reconstruct original native interval audit");VariantArray rows;
    std::istringstream list(intervalList);std::string token;
    while(std::getline(list,token,';')){std::istringstream fields(token);std::string idText,firstText,lastText;
        if(!std::getline(fields,idText,':')||!std::getline(fields,firstText,':')||!std::getline(fields,lastText,':'))throw std::runtime_error("Expected unchanged edgeId:first:last intervals");
        const auto id=std::stoull(idText);const double first=ParseNativeIntervalParameter(firstText),last=ParseNativeIntervalParameter(lastText);BRepAdaptor_Curve curve(built.Edges.at(id));
        if(!std::isfinite(first)||!std::isfinite(last)||!(last>first)||first<curve.FirstParameter()||last>curve.LastParameter())throw std::runtime_error("Diagnostic interval exceeds the original native edge range");
        const auto a=curve.Value(first),b=curve.Value(last);ObjectMap row{{"edgeId",std::to_string(id)},{"firstParameter",first},{"lastParameter",last},
            {"nativeFirstParameter",curve.FirstParameter()},{"nativeLastParameter",curve.LastParameter()},{"nativeCurveType",static_cast<int>(curve.GetType())},
            {"nativeFirstPoint",VariantArray{a.X(),a.Y(),a.Z()}},{"nativeLastPoint",VariantArray{b.X(),b.Y(),b.Z()}},{"physicalLengthLowerBoundEndpointChordMm",a.Distance(b)}};
        if(curve.GetType()==GeomAbs_Line)row["analyticNativeIntervalLengthMm"]=last-first;
        if(curve.GetType()==GeomAbs_Circle){row["nativeRadiusMm"]=curve.Circle().Radius();row["analyticNativeIntervalLengthMm"]=curve.Circle().Radius()*(last-first);}
        rows.emplace_back(std::move(row));}
    Save(output,{{"readOnly",true},{"source",Utf8(source)},{"sourceUnchanged",bytes==G::Persistence::Serialize(geometry)},
        {"rule",std::string("Physical measurements evaluate only each requested unchanged original edge interval's two endpoints; native Line/Circle lengths use their exact analytic parameter units. Other native curve lengths are not inferred from parameter width")},{"intervals",rows}});return 0;
}

int NativeLineBoundaryAudit(const fs::path& source,const std::string& edgeList,const fs::path& output)
{
    std::ifstream input(source,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    const auto geometry=G::Persistence::Deserialize(bytes);const auto built=O::ReconstructOpenCascadeShape(geometry);
    if(!built.bOK)throw std::runtime_error("Cannot reconstruct original native line-boundary audit");
    std::map<std::uint64_t,std::set<std::uint64_t>> owners;
    for(const auto& face:geometry.Faces)for(const auto wireId:face.WireIds)for(const auto& wire:geometry.Wires)if(wire.Id==wireId)
        for(const auto& use:wire.Coedges)owners[use.EdgeId].insert(face.Id);
    std::set<std::uint64_t> selected;std::istringstream list(edgeList);std::string token;while(std::getline(list,token,','))selected.insert(std::stoull(token));VariantArray rows;
    for(const auto edgeId:selected){const auto target=built.Edges.at(edgeId);BRepAdaptor_Curve curve(target);if(curve.GetType()!=GeomAbs_Line)throw std::runtime_error("Selected original edge is not a native line");
        const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());VariantArray candidates;
        for(const auto& [candidateId,edge]:built.Edges){if(candidateId==edgeId)continue;BRepAdaptor_Curve candidate(edge);if(candidate.GetType()!=GeomAbs_Line)continue;
            const auto line=candidate.Line();const double p=ElCLib::Parameter(line,a),q=ElCLib::Parameter(line,b);
            const auto pa=candidate.Value(std::clamp(p,candidate.FirstParameter(),candidate.LastParameter())),pb=candidate.Value(std::clamp(q,candidate.FirstParameter(),candidate.LastParameter()));
            const double error=std::max(a.Distance(pa),b.Distance(pb)),budget=std::min(BRep_Tool::Tolerance(target),BRep_Tool::Tolerance(edge));if(error>budget)continue;
            VariantArray faceIds;for(const auto id:owners[candidateId])faceIds.emplace_back(std::to_string(id));
            candidates.emplace_back(ObjectMap{{"edgeId",std::to_string(candidateId)},{"ownerFaceIds",faceIds},{"nativeRange",VariantArray{candidate.FirstParameter(),candidate.LastParameter()}},
                {"nativeProjectedTargetEndpoints",VariantArray{p,q}},{"targetEndpointsInsideUnchangedNativeRange",p>=candidate.FirstParameter()&&p<=candidate.LastParameter()&&q>=candidate.FirstParameter()&&q<=candidate.LastParameter()},
                {"targetToFiniteCandidateMaximumEndpointGapMm",error},{"minimumOriginalEdgeToleranceBudgetMm",budget},{"candidateOriginalEdgeToleranceMm",BRep_Tool::Tolerance(edge)},
                {"lineDirectionCrossMagnitude",gp_Vec(curve.Line().Direction()).Crossed(gp_Vec(line.Direction())).Magnitude()}});}
        rows.emplace_back(ObjectMap{{"edgeId",std::to_string(edgeId)},{"nativeRange",VariantArray{curve.FirstParameter(),curve.LastParameter()}},{"originalEdgeToleranceMm",BRep_Tool::Tolerance(target)},
            {"nativeStart",VariantArray{a.X(),a.Y(),a.Z()}},{"nativeEnd",VariantArray{b.X(),b.Y(),b.Z()}},{"finiteLineBoundaryCandidates",candidates}});}
    Save(output,{{"readOnly",true},{"source",Utf8(source)},{"sourceUnchanged",bytes==G::Persistence::Serialize(geometry)},
        {"rule",std::string("For two unchanged finite native Lines, distance to the finite witness segment is convex along the source: its endpoint maximum bounds the entire source; IDs and original ranges are retained")},{"edges",rows}});return 0;
}

int NativeSharedVertexAudit(const fs::path& source,const std::string& faceList,const fs::path& output)
{
    std::ifstream input(source,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    const auto geometry=G::Persistence::Deserialize(bytes);const auto built=O::ReconstructOpenCascadeShape(geometry);
    if(!built.bOK)throw std::runtime_error("Cannot reconstruct original native shared-vertex audit");
    std::set<std::uint64_t> selected;std::istringstream list(faceList);std::string token;
    while(std::getline(list,token,','))selected.insert(std::stoull(token));
    VariantArray faces;
    for(const auto& [faceId,face]:built.Faces)if(selected.empty()||selected.count(faceId)) {
        BRepAdaptor_Surface surface(face,true);const bool plane=surface.GetType()==GeomAbs_Plane;
        struct Endpoint{std::uint64_t EdgeId,VertexId;TopoDS_Vertex Vertex;double Parameter;};std::vector<Endpoint> endpoints;
        std::set<std::uint64_t> edges;
        for(const auto& record:geometry.Faces)if(record.Id==faceId)for(const auto wireId:record.WireIds)
            for(const auto& wire:geometry.Wires)if(wire.Id==wireId)for(const auto& use:wire.Coedges)edges.insert(use.EdgeId);
        for(const auto edgeId:edges){const auto edge=built.Edges.at(edgeId);TopoDS_Vertex first,last;TopExp::Vertices(edge,first,last,false);
            if(first.IsNull()||last.IsNull()||first.IsSame(last))continue;
            double a,b;BRep_Tool::Range(edge,a,b);std::uint64_t firstId=0,lastId=0;
            for(const auto& record:geometry.Edges)if(record.Id==edgeId){firstId=record.StartVertexId;lastId=record.EndVertexId;break;}
            endpoints.push_back({edgeId,firstId,first,a});endpoints.push_back({edgeId,lastId,last,b});}
        VariantArray vertices;std::vector<bool> done(endpoints.size(),false);
        for(std::size_t i=0;i<endpoints.size();++i){if(done[i])continue;std::vector<std::size_t> group;std::set<std::uint64_t> incident;
            for(std::size_t j=i;j<endpoints.size();++j)if(endpoints[i].Vertex.IsSame(endpoints[j].Vertex)){done[j]=true;group.push_back(j);incident.insert(endpoints[j].EdgeId);}
            if(incident.size()<2)continue;
            const auto point=BRep_Tool::Pnt(endpoints[i].Vertex);VariantArray uses;bool allInside=true;double maxError=0.;
            for(const auto j:group){const auto& endpoint=endpoints[j];const auto edge=built.Edges.at(endpoint.EdgeId);double a,b;const auto pcurve=BRep_Tool::CurveOnSurface(edge,face,a,b);
                ObjectMap use{{"edgeId",std::to_string(endpoint.EdgeId)},{"parameter",endpoint.Parameter},{"edgeToleranceMm",BRep_Tool::Tolerance(edge)},
                    {"sameParameter",BRep_Tool::SameParameter(edge)},{"sameRange",BRep_Tool::SameRange(edge)},{"pcurvePresent",!pcurve.IsNull()}};
                use["native3dCurveType"]=static_cast<int>(BRepAdaptor_Curve(edge).GetType());
                if(!pcurve.IsNull())use["nativePCurveType"]=static_cast<int>(BRepAdaptor_Curve2d(edge,face).GetType());
                const auto native=BRepAdaptor_Curve(edge).Value(endpoint.Parameter);const double curveError=native.Distance(point);
                use["nativeCurvePoint"]=VariantArray{native.X(),native.Y(),native.Z()};use["nativeCurveToVertexMm"]=curveError;
                const double budget=std::min({BRep_Tool::Tolerance(face),BRep_Tool::Tolerance(edge),BRep_Tool::Tolerance(endpoint.Vertex)});
                use["minimumDeclaredBudgetMm"]=budget;double error=curveError;
                if(!pcurve.IsNull()){const auto uv=pcurve->Value(endpoint.Parameter);const auto onFace=surface.Value(uv.X(),uv.Y());const double pcurveError=onFace.Distance(point);
                    use["nativePCurveUV"]=VariantArray{uv.X(),uv.Y()};use["nativePCurveSurfacePoint"]=VariantArray{onFace.X(),onFace.Y(),onFace.Z()};
                    use["nativePCurveSurfaceToVertexMm"]=pcurveError;use["nativeCurveToPCurveSurfaceMm"]=native.Distance(onFace);error=std::max(error,pcurveError);}
                if(plane){const double planeError=surface.Plane().Distance(point);use["nativePlaneToVertexMm"]=planeError;error=std::max(error,planeError);}
                use["maximumGuardErrorMm"]=error;use["withinOriginalMinimumBudget"]=error<=budget;maxError=std::max(maxError,error);allInside=allInside&&error<=budget;uses.emplace_back(std::move(use));}
            vertices.emplace_back(ObjectMap{{"vertexId",std::to_string(endpoints[i].VertexId)},{"point",VariantArray{point.X(),point.Y(),point.Z()}},
                {"vertexToleranceMm",BRep_Tool::Tolerance(endpoints[i].Vertex)},{"incidentEdges",static_cast<int>(incident.size())},{"allUsesWithinOriginalMinimumBudget",allInside},{"maximumGuardErrorMm",maxError},{"uses",uses}});
        }
        faces.emplace_back(ObjectMap{{"faceId",std::to_string(faceId)},{"nativeSurfaceType",static_cast<int>(surface.GetType())},{"nativePlane",plane},
            {"faceToleranceMm",BRep_Tool::Tolerance(face)},{"originalNativeFaceValid",BRepCheck_Analyzer(face,true).IsValid()},{"sharedVertices",vertices}});
    }
    Save(output,{{"readOnly",true},{"source",Utf8(source)},{"sourceUnchanged",bytes==G::Persistence::Serialize(geometry)},
        {"rule",std::string("per original edge endpoint: max(native curve to vertex, pcurve-on-support to vertex, native plane to vertex) <= min(original face, edge, vertex tolerances); no geometry edited")},{"faces",faces}});return 0;
}
