// Bearing visibility is evaluated on trimmed BRep faces. Split at surface
// projection contours, sweep each monotone patch upward, and subtract those
// exact occluding volumes. No display triangles participate in selection.
std::vector<std::uint64_t> LowerEnvelopeFaces(const SectionPlateInput& in,const std::function<bool()>& cancelled) {
    using namespace plate_exact;
    const Check check=[&]{if(cancelled&&cancelled())throw std::runtime_error("PLATE_CANCELLED");};check();
    if(!in.Geometry)throw std::invalid_argument("PLATE_INPUT_REQUIRED");
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);Require(built.bOK,"PLATE_BREP_INVALID");
    const TopLoc_Location location(PlateDirectionFrame::Transform(in.Transform));const auto bounds=Bounds(built.Shape.Moved(location));
    const gp_Vec sweep(0,0,bounds[5]-bounds[2]+1.);
    struct Patch {std::uint64_t FaceID;TopoDS_Face Face;TopoDS_Shape Shadow;std::array<double,6> Bounds;};
    std::vector<Patch> patches;
    for(const auto& [id,original]:built.Faces){check();const auto face=TopoDS::Face(original.Moved(location));const auto contours=SurfaceContours(face,check);TopoDS_Shape pieces=face;
        if(!contours.empty()){BRepFeat_SplitShape split(face);for(const auto& edge:contours)split.Add(edge,face);split.Build();Require(split.IsDone(),"PLATE_BEARING_SPLIT_FAILED");pieces=split.Shape();}
        Valid(pieces,"PLATE_BEARING_PATCH_INVALID",id);
        for(TopExp_Explorer e(pieces,TopAbs_FACE);e.More();e.Next()){check();const auto patch=TopoDS::Face(e.Current());BRepPrimAPI_MakePrism extrusion(patch,sweep,true);
            Require(extrusion.IsDone(),"PLATE_BEARING_SHADOW_FAILED");auto shadow=extrusion.Shape();if(shadow.ShapeType()!=TopAbs_SOLID)continue;
            GProp_GProps volume;BRepGProp::VolumeProperties(shadow,volume);if(std::abs(volume.Mass())<=Tol*Tol*sweep.Magnitude())continue;
            Valid(shadow,"PLATE_BEARING_SHADOW_INVALID",id);BRepClass3d_SolidClassifier classifier(TopoDS::Solid(shadow));classifier.PerformInfinitePoint(Tol);
            if(classifier.State()==TopAbs_IN)shadow.Reverse();else Require(classifier.State()==TopAbs_OUT,"PLATE_BEARING_SHADOW_INVALID");
            patches.push_back({id,patch,shadow,Bounds(patch)});
        }
    }
    std::set<std::uint64_t> visible;
    for(size_t i=0;i<patches.size();++i){check();const auto& candidate=patches[i];if(visible.contains(candidate.FaceID))continue;TopoDS_Shape remaining=candidate.Face;
        for(size_t j=0;j<patches.size();++j){if(i==j)continue;check();const auto& other=patches[j];const auto& a=candidate.Bounds;const auto& b=other.Bounds;
            if(b[0]>a[3]+Tol||b[3]<a[0]-Tol||b[1]>a[4]+Tol||b[4]<a[1]-Tol||b[2]>a[5]+Tol)continue;
            // Coplanar/coincident material is equally accessible. Subtraction
            // of a closed shadow also removes its starting boundary, so retain
            // that exact common face instead of moving the shadow by epsilon.
            const auto coincident=Common(remaining,other.Face,check);auto reduced=Cut(remaining,{other.Shadow},check);
            if(Has(coincident,TopAbs_FACE))remaining=Compound({reduced,coincident});else remaining=reduced;
            if(!Has(remaining,TopAbs_FACE))break;
        }
        if(Has(remaining,TopAbs_FACE)&&Area(remaining)>Tol*Tol)visible.insert(candidate.FaceID);
    }
    check();return {visible.begin(),visible.end()};
}
