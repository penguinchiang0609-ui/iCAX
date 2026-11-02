// Frozen pre-streaming projection: changes to the production writer must keep
// existing project/result fingerprints compatible with this reference.
std::string LegacyPersistedFingerprint(const SToolpathValue& value,const std::set<std::string>& channels)
{
    const auto all=channels.contains("*");Map projection;
    // Build only the requested projection. Geometry-only reads must not copy
    // unrelated interval channels or serialize the complete provenance graph.
    if(all||channels.contains("geometry"))
    {
        Array curves;for(const auto& s:value.Geometry.Segments)curves.emplace_back(ExactData(s.Curve));
        projection["geometry"]=Map{{"authority",static_cast<int>(value.Geometry.Authority)},{"curves",std::move(curves)},
            {"support",ExactData(value.Geometry.SupportSurface)},{"plane",ExactData(value.Geometry.Plane)},{"tolerance",value.Geometry.Tolerance},{"meaning",value.Geometry.Meaning}};
        Array parameterizations;bool noncanonical=false;for(const auto& segment:value.Geometry.Segments){parameterizations.emplace_back(segment.Parameterization);noncanonical|=segment.Parameterization!="Canonical";}
        if(noncanonical){auto& intrinsic=std::get<Map>(projection.at("geometry").m_Value);intrinsic["parameterizations"]=std::move(parameterizations);}
    }
    if(all||channels.contains("topology")||channels.contains("traversal"))
    {Array ids;for(const auto& s:value.Geometry.Segments)ids.emplace_back(Map{{"id",s.Id},{"range",RangeData(Range(s))}});
     projection["topology"]=Map{{"closed",value.Geometry.Closed},{"segments",std::move(ids)}};}
    if(all||channels.contains("provenance"))projection["provenance"]=MapAt(ToData(value),"geometry").at("segments");
    if(all||channels.contains("frame"))projection["frame"]=value.Geometry.FrameId;
    Map extensions;for(const auto& [name,v]:value.Channels)if(all||channels.contains(name))extensions[name]=v;
    projection["extensionChannels"]=std::move(extensions);
    return Hash(std::move(projection));
}
