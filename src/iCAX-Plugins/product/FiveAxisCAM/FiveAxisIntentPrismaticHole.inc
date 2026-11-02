SOperationCandidate PrismaticHoleDimensions(const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
{
    Check(input.Inputs.size()==2,"CAM_HOLE_NOMINAL_INPUT_REQUIRED");
    const auto& selected=input.Inputs[0];const auto& nominal=input.Inputs[1];
    auto value=Whole(nominal);
    if(selected.Value.Channels.contains("fiveaxis.geometryEdits"))
        for(const auto& item:selected.Value.Channels.at("fiveaxis.geometryEdits").To<Map>().at("entries").To<Array>())
        {
            const auto edit=item.To<Map>();Check(Text(edit,"type")=="fiveaxis.correction","CAM_HOLE_EDIT_HISTORY_UNSUPPORTED");
            auto stage=input;stage.Parameters=edit.at("parameters").To<Map>();stage.Inputs={SFrozenPathInput{{"stage",DeclaredToolpathPort(input.OperationId,"main"),true,{}},selected.PathId,value,ToolpathFingerprint(value)}};
            value=std::move(CorrectionOperation(stage,geometry).Outputs.front().Value);
        }
    // Rebuild the complete nominal loop. A compensated polygon may trim away
    // both ends of an edge; projecting only its retained pieces would create
    // artificial gaps at the original corners. Invert the exact address maps
    // for field transfer without treating those pieces as a new contour.
    const auto selectedFingerprint=GeometryFingerprint(selected);
    for(auto& segment:value.Geometry.Segments)
        for(const auto& piece:selected.Value.Geometry.Segments)
            for(const auto& source:piece.Sources)if(source.Source==nominal.Binding.Source&&source.SourceSegmentId==segment.Id)
            {
                Check(source.Precision=="Exact"&&source.ErrorBound==0,"CAM_HOLE_MAPPING_PRECISION_UNSUPPORTED");const auto domain=Range(piece);
                const auto lo=std::max(domain.First,source.OutputRange.First),hi=std::min(domain.Last,source.OutputRange.Last);if(hi<=lo+1e-12)continue;
                const ParameterRange retained{lo,hi,source.OutputRange.Reversed};
                const auto map=[&](double u){return Start(source.SourceRange)+(u-Start(source.OutputRange))/(End(source.OutputRange)-Start(source.OutputRange))*(End(source.SourceRange)-Start(source.SourceRange));};
                const auto a=map(Start(retained)),b=map(End(retained));segment.Sources.push_back({selected.Binding.Source,piece.Id,retained,{std::min(a,b),std::max(a,b),a>b},"Exact",0,source.Occurrence,selectedFingerprint});
            }
    auto geometryChannels=value.Channels;value.Channels=selected.Value.Channels;
    for(const auto* key:{"fiveaxis.correction","fiveaxis.segmentCorrections"})
        if(geometryChannels.contains(key))value.Channels[key]=geometryChannels.at(key);else value.Channels.erase(key);
    auto metadata=ReadFiveAxisIntentMetadata(selected.Value);auto hole=input.Parameters.contains("descriptor")?Object(input.Parameters,"descriptor"):Object(metadata.Feature,"standardHole");const auto kind=Text(hole,"kind");
    Check(kind=="Rectangle"||kind=="Slot","CAM_HOLE_DIMENSIONS_UNSUPPORTED");const auto dimensions=Object(input.Parameters,"dimensions");Fields(dimensions,{"widthMm","lengthMm"});
    const auto width=Number(dimensions,"widthMm"),length=Number(dimensions,"lengthMm"),oldWidth=Number(hole,"widthMm"),oldLength=Number(hole,"lengthMm");
    Check(width>1e-6&&length>1e-6&&width<=1e6&&length<=1e6&&oldWidth>0&&oldLength>0&&(kind!="Slot"||length>width),"CAM_HOLE_DIMENSIONS_INVALID");
    const auto coordinates=hole.at("center").To<Array>();const Point3 center{Numeric(coordinates[0]),Numeric(coordinates[1]),Numeric(coordinates[2])};
    const auto x=Direction(hole.at("xDirection")),normal=Direction(hole.contains("normal")?hole.at("normal"):hole.at("direction")),y=Cross(normal,x);
    const auto sx=kind=="Slot"?(length-width)/(oldLength-oldWidth):width/oldWidth,sy=kind=="Slot"?width/oldWidth:length/oldLength;
    Check(std::isfinite(sx)&&sx>0,"CAM_HOLE_DIMENSIONS_INVALID");
    for(auto& segment:value.Geometry.Segments)
    {
        Check(std::holds_alternative<CurveSegment3>(segment.Curve),"CAM_HOLE_NOMINAL_INPUT_REQUIRED");auto& curve=std::get<CurveSegment3>(segment.Curve);
        const auto first=geometry.Evaluate(value.Geometry,segment,curve.Range.First),last=geometry.Evaluate(value.Geometry,segment,curve.Range.Last);
        const auto along=[&](Point3 p){return Dot({p.X-center.X,p.Y-center.Y,p.Z-center.Z},x);};const auto half=(oldLength-oldWidth)/2;
        const int cap=kind!="Slot"?0:std::min(along(first),along(last))>=half-1e-7?1:std::max(along(first),along(last))<=-half+1e-7?-1:0;
        const auto point=[&](Point3 p){const Direction3 delta{p.X-center.X,p.Y-center.Y,p.Z-center.Z};auto a=Dot(delta,x),b=Dot(delta,y);const auto z=Dot(delta,normal);
            if(cap){a=(a-cap*half)*sy+cap*(length-width)/2;b*=sy;}else {a*=sx;b*=sy;}
            return Point3{center.X+a*x.X+b*y.X+z*normal.X,center.Y+a*x.Y+b*y.Y+z*normal.Y,center.Z+a*x.Z+b*y.Z+z*normal.Z};};
        if(std::holds_alternative<Line3>(curve.Curve)||std::holds_alternative<Segment3>(curve.Curve))
        {BSpline3 line;line.Degree=1;line.Poles={point(first),point(last)};line.Knots={curve.Range.First,curve.Range.Last};line.Multiplicities={2,2};curve.Curve=std::move(line);}
        else std::visit([&](auto& source){using T=std::decay_t<decltype(source)>;
            if constexpr(std::is_same_v<T,Circle3>){Check(cap!=0,"CAM_HOLE_GEOMETRY_MISMATCH");source.Placement.Location=point(source.Placement.Location);source.Radius*=sy;}
            else if constexpr(std::is_same_v<T,Arc3>){Check(cap!=0,"CAM_HOLE_GEOMETRY_MISMATCH");source.Basis.Placement.Location=point(source.Basis.Placement.Location);source.Basis.Radius*=sy;}
            else if constexpr(requires{source.Poles;})for(auto& pole:source.Poles)pole=point(pole);
            else throw std::invalid_argument("CAM_HOLE_GEOMETRY_MISMATCH");
        },curve.Curve);
    }
    value.Geometry.Closed=true;value.Geometry.Meaning="NominalBoundary";
    hole["widthMm"]=width;hole["lengthMm"]=length;metadata.Feature["standardHole"]=hole;metadata.Feature["geometryEdited"]=true;
    value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);
    for(const auto* name:{"fiveaxis.kerf","fiveaxis.trace","fiveaxis.leads","fiveaxis.geometryEdits"})value.Channels.erase(name);
    const auto port=DeclaredToolpathPort(input.OperationId,"main");SFrozenPathInput stage{{"stage",port,true,{}},selected.PathId,value,ToolpathFingerprint(value)};
    value=FinalizeProjectedToolpath(stage,input.OperationId,{input.OperationId},geometry);
    auto contextInput=input;contextInput.Inputs={selected};value.Channels["poseField"]=BuildFiveAxisIntentPoseField(Context(contextInput),value,port);
    if(value.Channels.contains("contourPoseField"))value.Channels["contourPoseField"]=BuildFiveAxisIntentPoseField(Context(contextInput),value,port,"contour");
    SOperationCandidate result;result.Outputs.push_back({"main",{},std::size_t(0),std::move(value)});result.ExcludedRanges=MissingRanges(selected,result.Outputs.front().Value);return result;
}
