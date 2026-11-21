// Read-only presentation of an already-produced Work result. No Work is run
// here; all IDs and parameter ranges belong to the supplied neutral BREP.
namespace work_inspection_mesh
{
VariantArray XYZ(const gp_Pnt& point) { return {point.X(),point.Y(),point.Z()}; }

bool Finite(const gp_Pnt& point)
{
    return std::isfinite(point.X())&&std::isfinite(point.Y())&&std::isfinite(point.Z());
}

std::vector<std::uint8_t> Bytes(const fs::path& path)
{
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read Work inspection source: "+Utf8(path));
    return {(std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>()};
}

int Export(const fs::path& source,const fs::path& output)
{
    if(fs::weakly_canonical(source)==fs::weakly_canonical(output))
        throw std::invalid_argument("Work inspection output must differ from its source");
    VariantArray meshes,edgeRows,issues,diagnostics;
    std::size_t meshed=0,failedFaces=0,zeroFaces=0,physicalEdges=0,failedEdges=0;
    bool unchanged=false;
    ObjectMap counts;
    try
    {
        const auto input=Bytes(source);
        if(input.empty())throw std::runtime_error("Work inspection source is empty");
        const auto model=G::Persistence::Deserialize(input);
        const auto before=G::Persistence::Serialize(model);
        const auto built=O::ReconstructOpenCascadeShape(model);
        if(!built.bOK||built.Shape.IsNull())throw std::runtime_error("Cannot reconstruct Work inspection BREP");
        Bnd_Box bounds;BRepBndLib::AddOptimal(built.Shape,bounds,false,false);
        double diagonal=1;
        if(!bounds.IsVoid())
        {
            double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
            diagonal=gp_Pnt(x,y,z).Distance(gp_Pnt(X,Y,Z));
        }
        const double deflection=std::clamp(diagonal*.0002,1e-7,.1);
        std::map<std::uint64_t,const G::BRepWire*> wires;
        std::map<std::uint64_t,std::set<std::uint64_t>> owners;
        for(const auto& wire:model.Wires)wires.emplace(wire.Id,&wire);
        for(const auto& face:model.Faces)
            for(const auto wireId:face.WireIds)
                for(const auto& use:wires.at(wireId)->Coedges)owners[use.EdgeId].insert(face.Id);

        for(const auto& face:model.Faces)
        {
            VariantArray positions,indices;
            ObjectMap row{{"faceId",std::to_string(face.Id)},{"sourceId",face.Metadata.SourceId},
                {"sourceTags",Strings(face.Metadata.Tags)}};
            try
            {
                const auto& native=built.Faces.at(face.Id);
                const auto hasTag=[&](const std::string& value)
                    {return std::find(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),value)!=face.Metadata.Tags.end();};
                const auto hasPrefix=[&](const std::string& prefix)
                    {return std::any_of(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),[&](const auto& tag)
                        {return tag.starts_with(prefix)&&tag.size()>prefix.size();});};
                const bool zeroProof=hasTag("classify.zeroAreaTrim=true")
                    &&hasPrefix("classify.zeroAreaProof=")&&hasPrefix("classify.zeroAreaSourceFaceId=");
                const double area=zeroProof?0.0:Area(native);
                row["areaMm2"]=area;
                if(zeroProof||area==0.0)
                {
                    ++zeroFaces;row["displaySkippedZeroArea"]=true;
                    row["displaySkipReason"]=std::string(zeroProof?"Source Work proved zero-area trim":"Source face has zero surface area");
                    issues.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"kind",std::string("zero-area")},
                        {"reason",row.at("displaySkipReason")},{"topologyRetained",true}});
                }
                else
                {
                    const auto preview=machining_edge_export::PreviewFace(native,deflection);
                    for(int i=1;i<=preview.Triangles->NbNodes();++i)
                    {
                        const auto p=preview.Triangles->Node(i).Transformed(preview.Location.Transformation());
                        if(!Finite(p))throw std::runtime_error("Non-finite Work face preview vertex");
                        positions.emplace_back(p.X());positions.emplace_back(p.Y());positions.emplace_back(p.Z());
                    }
                    for(int i=1;i<=preview.Triangles->NbTriangles();++i)
                    {
                        int a,b,c;preview.Triangles->Triangle(i).Get(a,b,c);
                        if(a<1||b<1||c<1||a>preview.Triangles->NbNodes()||b>preview.Triangles->NbNodes()
                            ||c>preview.Triangles->NbNodes())throw std::runtime_error("Invalid Work face preview triangle index");
                        if(preview.Face.Orientation()==TopAbs_REVERSED)std::swap(b,c);
                        indices.emplace_back(a-1);indices.emplace_back(b-1);indices.emplace_back(c-1);
                    }
                    if(indices.empty())throw std::runtime_error("Work face preview contains no triangles");
                    row["displayPreparation"]=preview.Preparation;
                    row["displayDeflectionMm"]=preview.Deflection;
                    row["displaySkippedZeroArea"]=false;++meshed;
                }
            }
            catch(const Standard_Failure& error)
            {
                ++failedFaces;positions.clear();indices.clear();
                issues.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"kind",std::string("mesh-failed")},
                    {"reason",std::string(error.GetMessageString())},{"topologyRetained",true}});
                row["displayIssue"]=std::string(error.GetMessageString());
            }
            catch(const std::exception& error)
            {
                ++failedFaces;positions.clear();indices.clear();
                issues.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"kind",std::string("mesh-failed")},
                    {"reason",std::string(error.what())},{"topologyRetained",true}});
                row["displayIssue"]=std::string(error.what());
            }
            row["positions"]=positions;row["indices"]=indices;meshes.emplace_back(std::move(row));
        }
        for(const auto& edge:model.Edges)
        {
            const auto& native=built.Edges.at(edge.Id);
            const auto& faceIds=owners[edge.Id];VariantArray faceArray,points;
            bool seam=false;
            for(const auto face:faceIds)
            {
                faceArray.emplace_back(std::to_string(face));
                seam=seam||BRep_Tool::IsClosed(native,built.Faces.at(face));
            }
            const bool degenerated=BRep_Tool::Degenerated(native);
            const bool physical=!degenerated&&!(faceIds.size()==1&&seam);
            ObjectMap row{{"edgeId",std::to_string(edge.Id)},{"faceIds",faceArray},
                {"firstParameter",edge.Range.First},{"lastParameter",edge.Range.Last},
                {"degenerated",degenerated},{"parametricSeam",seam},{"physical",physical},{"previewOnly",true}};
            if(physical)
            {
                ++physicalEdges;
                try
                {
                    BRepAdaptor_Curve curve(native);
                    const double first=curve.FirstParameter(),last=curve.LastParameter();
                    if(!std::isfinite(first)||!std::isfinite(last)||last<=first)
                        throw std::runtime_error("Invalid finite original Work edge range");
                    row["firstParameter"]=first;row["lastParameter"]=last;
                    GCPnts_QuasiUniformDeflection preview(curve,deflection,first,last);
                    if(!preview.IsDone()||preview.NbPoints()<2)
                        throw std::runtime_error("Adaptive original Work edge preview failed");
                    for(int i=1;i<=preview.NbPoints();++i)
                    {
                        const auto p=preview.Value(i);
                        if(!Finite(p))throw std::runtime_error("Non-finite Work edge preview point");
                        points.emplace_back(XYZ(p));
                    }
                }
                catch(const Standard_Failure& error)
                {
                    ++failedEdges;points.clear();row["displayIssue"]=std::string(error.GetMessageString());
                    issues.emplace_back(ObjectMap{{"edgeId",std::to_string(edge.Id)},{"kind",std::string("edge-preview-failed")},
                        {"reason",std::string(error.GetMessageString())},{"topologyRetained",true}});
                }
                catch(const std::exception& error)
                {
                    ++failedEdges;points.clear();row["displayIssue"]=std::string(error.what());
                    issues.emplace_back(ObjectMap{{"edgeId",std::to_string(edge.Id)},{"kind",std::string("edge-preview-failed")},
                        {"reason",std::string(error.what())},{"topologyRetained",true}});
                }
            }
            row["points"]=points;edgeRows.emplace_back(std::move(row));
        }
        unchanged=before==G::Persistence::Serialize(model)&&input==Bytes(source);
        counts={{"wholeFaces",static_cast<int>(model.Faces.size())},{"edges",static_cast<int>(model.Edges.size())},
            {"meshedFaces",static_cast<int>(meshed)},{"meshFailedFaces",static_cast<int>(failedFaces)},
            {"zeroAreaFaces",static_cast<int>(zeroFaces)},{"physicalEdges",static_cast<int>(physicalEdges)},
            {"excludedTopologyEdges",static_cast<int>(model.Edges.size()-physicalEdges)},
            {"edgePreviewFailures",static_cast<int>(failedEdges)}};
        diagnostics.emplace_back(std::string("Read-only preview of the supplied Work result; no recognition, healing, alignment, classification, edge analysis or path analysis was run"));
        diagnostics.emplace_back(std::string("Adaptive edge polylines are display-only representations of unchanged original 3D curves, never toolpath processing points"));
        if(!unchanged)diagnostics.emplace_back(std::string("Source neutral geometry or source file changed during preview export"));
        const bool complete=failedFaces==0&&failedEdges==0;
        Save(output,{{"schema",1},{"stage",std::string("inspectionMesh")},{"success",unchanged},
            {"complete",complete},{"source",Utf8(source)},{"sourceUnchanged",unchanged},
            {"geometry",ObjectMap{{"faces",meshes},{"previewOnly",true},{"completeModel",true},
                {"displayDeflectionMm",deflection},{"issues",issues}}},
            {"edges",edgeRows},{"counts",counts},{"diagnostics",diagnostics}});
        return unchanged&&complete?0:1;
    }
    catch(const Standard_Failure& error){diagnostics.emplace_back(std::string(error.GetMessageString()));}
    catch(const std::exception& error){diagnostics.emplace_back(std::string(error.what()));}
    Save(output,{{"schema",1},{"stage",std::string("inspectionMesh")},{"success",false},{"complete",false},
        {"source",Utf8(source)},{"sourceUnchanged",unchanged},
        {"geometry",ObjectMap{{"faces",meshes},{"previewOnly",true},{"issues",issues}}},
        {"edges",edgeRows},{"counts",counts},{"diagnostics",diagnostics}});
    return 1;
}
}
