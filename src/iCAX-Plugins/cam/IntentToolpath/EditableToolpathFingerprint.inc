namespace
{
// Keep the persisted Variant wire format while writing this fixed projection
// directly. Extension values are serialized by reference; no duplicate Variant
// tree is built just to hash and immediately destroy it.
class CFingerprintWriter final
{
    bool First[8]{};std::size_t Depth=0;
    void Next(){if(First[Depth-1])First[Depth-1]=false;else Bytes+=',';}
    void Quoted(std::string_view text)
    {
        Bytes+='"';std::size_t begin=0;
        for(std::size_t i=0;i<text.size();++i)
        {
            const char* escape=nullptr;
            switch(text[i]){case '"':escape="\\\"";break;case '\\':escape="\\\\";break;
                case '\b':escape="\\b";break;case '\f':escape="\\f";break;case '\n':escape="\\n";break;
                case '\r':escape="\\r";break;case '\t':escape="\\t";break;default:break;}
            if(escape){Bytes.append(text.data()+begin,i-begin);Bytes.append(escape,2);begin=i+1;}
        }
        Bytes.append(text.data()+begin,text.size()-begin);Bytes+='"';
    }
public:
    std::string Bytes;
    void Object(){Bytes+="{\"__variant_type\":\"Object\",\"value\":{";First[Depth++]=true;}
    void EndObject(){--Depth;Bytes+="}}";}
    void Array(){Bytes+="{\"__variant_type\":\"Array\",\"value\":[";First[Depth++]=true;}
    void EndArray(){--Depth;Bytes+="]}";}
    void Key(std::string_view name){Next();Quoted(name);Bytes+=':';}
    void Item(){Next();}
    void Value(const Variant& value){Bytes+=VariantSerializer::Serialize(value);}
    void Value(const std::string& value){Bytes+="{\"__variant_type\":\"string\",\"value\":";Quoted(value);Bytes+='}';}
    // ExactData emits only hexadecimal bytes, so these large curve payloads
    // cannot contain any JSON escape characters.
    void ExactValue(const std::string& value){Bytes+="{\"__variant_type\":\"string\",\"value\":\"";Bytes+=value;Bytes+="\"}";}
    void Value(const IntentId& value){Bytes+="{\"__variant_type\":\"uuid\",\"value\":";Quoted(Editable::Key(value));Bytes+='}';}
    void Value(bool value){Bytes+=value?"{\"__variant_type\":\"bool\",\"value\":true}":"{\"__variant_type\":\"bool\",\"value\":false}";}
    template<class T>void Number(const char* type,T value)
    {
        char buffer[128];std::to_chars_result converted;
        if constexpr(std::is_same_v<T,double>){if(!std::isfinite(value)){Value(Variant(value));return;}converted=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,std::numeric_limits<double>::max_digits10);}
        else converted=std::to_chars(buffer,buffer+sizeof(buffer),value);
        if(converted.ec!=std::errc{}){Value(Variant(value));return;}
        Bytes+="{\"__variant_type\":\"";Bytes+=type;Bytes+="\",\"value\":";Bytes.append(buffer,converted.ptr);Bytes+='}';
    }
    void Value(int value){Number("int",value);}
    void Value(unsigned long long value){Number("unsigned_long_long",value);}
    void Value(double value){Number("double",value);}
    template<class T>void Field(std::string_view name,const T& value){Key(name);Value(value);}
    void RangeValue(const ParameterRange& range)
    {Object();Field("first",range.First);Field("last",range.Last);Field("reversed",range.Reversed);EndObject();}
    void PortValue(const SPortRef& port)
    {Object();Field("operationId",port.OperationId);Field("portId",port.PortId);EndObject();}
};
}
std::string ToolpathFingerprint(const SToolpathValue& value,const std::set<std::string>& channels)
{
    const bool all=channels.contains("*");const auto& geometry=value.Geometry;CFingerprintWriter out;
    const bool readGeometry=all||channels.contains("geometry"),readProvenance=all||channels.contains("provenance");
    std::vector<std::string> curves;
    if(readGeometry||readProvenance){curves.reserve(geometry.Segments.size());for(const auto& segment:geometry.Segments)curves.push_back(ExactData(segment.Curve));}
    out.Object();out.Key("extensionChannels");out.Object();
    for(const auto& [name,item]:value.Channels)if(all||channels.contains(name)){out.Key(name);out.Value(item);}
    out.EndObject();
    if(all||channels.contains("frame"))out.Field("frame",geometry.FrameId);
    if(readGeometry)
    {
        out.Key("geometry");out.Object();out.Field("authority",static_cast<int>(geometry.Authority));out.Key("curves");out.Array();
        for(const auto& curve:curves){out.Item();out.ExactValue(curve);}out.EndArray();out.Field("meaning",geometry.Meaning);
        if(std::any_of(geometry.Segments.begin(),geometry.Segments.end(),[](const auto& segment){return segment.Parameterization!="Canonical";}))
        {out.Key("parameterizations");out.Array();for(const auto& segment:geometry.Segments){out.Item();out.Value(segment.Parameterization);}out.EndArray();}
        out.Key("plane");out.ExactValue(ExactData(geometry.Plane));out.Key("support");out.ExactValue(ExactData(geometry.SupportSurface));out.Field("tolerance",geometry.Tolerance);out.EndObject();
    }
    if(readProvenance)
    {
        out.Key("provenance");out.Array();
        for(std::size_t i=0;i<geometry.Segments.size();++i)
        {
            const auto& segment=geometry.Segments[i];out.Item();out.Object();out.Field("construction",segment.ConstructionRole);out.Key("curve");out.ExactValue(curves[i]);out.Field("id",segment.Id);
            if(segment.Parameterization!="Canonical")out.Field("parameterization",segment.Parameterization);
            out.Key("sources");out.Array();
            for(const auto& source:segment.Sources)
            {
                out.Item();out.Object();out.Field("error",source.ErrorBound);out.Field("occurrence",static_cast<unsigned long long>(source.Occurrence));
                out.Key("outputRange");out.RangeValue(source.OutputRange);out.Key("port");out.PortValue(source.Source);
                out.Field("precision",source.Precision);out.Field("segmentId",source.SourceSegmentId);out.Field("sourceGeometryFingerprint",source.SourceGeometryFingerprint);
                out.Key("sourceRange");out.RangeValue(source.SourceRange);out.EndObject();
            }
            out.EndArray();out.EndObject();
        }
        out.EndArray();
    }
    if(all||channels.contains("topology")||channels.contains("traversal"))
    {
        out.Key("topology");out.Object();out.Field("closed",geometry.Closed);out.Key("segments");out.Array();
        for(const auto& segment:geometry.Segments){out.Item();out.Object();out.Field("id",segment.Id);out.Key("range");out.RangeValue(Range(segment));out.EndObject();}
        out.EndArray();out.EndObject();
    }
    out.EndObject();return Key(NamedID(out.Bytes));
}
