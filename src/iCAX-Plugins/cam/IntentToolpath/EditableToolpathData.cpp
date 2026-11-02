#include "pch.h"
#include "EditableToolpathInternal.h"
#include <limits>
#include <charconv>

namespace iCAX::CAM::Intent
{
using namespace Editable;
IntentMap ToData(const SPortRef& p){return {{"operationId",p.OperationId},{"portId",p.PortId}};}
std::string ToolpathPortKey(const SPortRef& p){return Key(p);}
SPortRef SourceToolpathPort(IntentId operation)
{Require(!operation.is_nil(),"TOOLPATH_SOURCE_OPERATION_REQUIRED");return {operation,NamedID(Key(operation)+"/port/main")};}
SPortRef DeclaredToolpathPort(IntentId operation,const std::string& role)
{Require(!operation.is_nil()&&!role.empty(),"TOOLPATH_OUTPUT_DECLARATION_REQUIRED");return {operation,NamedID(Key(operation)+"/port/"+role)};}
IntentMap ToData(const SOperationInput& input){return InputData(input);}
SPortRef PortFromData(const IntentMap& m)
{SPortRef p{ID(At(m,"operationId")),ID(At(m,"portId"))};Require(!p.OperationId.is_nil()&&!p.PortId.is_nil(),"TOOLPATH_PORT_REQUIRED");return p;}
IntentMap ToData(const SPathAnchor& a)
{return {{"port",ToData(a.Port)},{"segmentId",a.SegmentId},{"parameter",a.Parameter},{"occurrence",static_cast<unsigned long long>(a.Occurrence)},{"side",a.Side},{"observedGeometryFingerprint",a.ObservedGeometryFingerprint}};}
SPathAnchor AnchorFromData(const IntentMap& m)
{SPathAnchor a;a.Port=PortFromData(MapAt(m,"port"));a.SegmentId=Text(m,"segmentId");a.Parameter=Number(m,"parameter");
 if(m.contains("occurrence"))a.Occurrence=UnsignedValue(At(m,"occurrence"));a.Side=Text(m,"side");a.ObservedGeometryFingerprint=Text(m,"observedGeometryFingerprint");Require(!a.SegmentId.empty(),"TOOLPATH_SEGMENT_REQUIRED");return a;}
IntentMap ToData(const SPathRange& r)
{Array spans;for(const auto& span:r.ExactSpans)spans.emplace_back(Map{{"port",ToData(span.Port)},{"segmentId",span.SegmentId},{"range",RangeData(span.Range)},{"occurrence",static_cast<unsigned long long>(span.Occurrence)}});
 return {{"start",ToData(r.Start)},{"end",ToData(r.End)},{"wrapsSeam",r.WrapsSeam},{"direction",r.TraversalDirection},{"spans",std::move(spans)}};}
SPathRange RangeFromData(const IntentMap& m)
{SPathRange r{AnchorFromData(MapAt(m,"start")),AnchorFromData(MapAt(m,"end")),Flag(m,"wrapsSeam")};r.TraversalDirection=m.contains("direction")?At(m,"direction").To<int>():1;Require(r.TraversalDirection==1||r.TraversalDirection==-1,"TOOLPATH_RANGE_DIRECTION_INVALID");
 if(m.contains("spans"))for(const auto& item:ArrayAt(m,"spans")){const auto span=item.To<Map>();r.ExactSpans.push_back({PortFromData(MapAt(span,"port")),Text(span,"segmentId"),NativeRange(MapAt(span,"range")),UnsignedValue(At(span,"occurrence"))});}return r;}
SPathRange CaptureToolpathRange(const SResolvedPath& path,SPathAnchor start,SPathAnchor end,bool wrapsSeam,int direction)
{Require(direction==1||direction==-1,"TOOLPATH_RANGE_DIRECTION_INVALID");SPathRange range{std::move(start),std::move(end),wrapsSeam,direction,{}};return NormalizeRange(std::move(range),{{"capture",path.Port,false,{}},path.PathId,path.Value,path.Fingerprint});}
namespace
{
Array GeometrySegmentsData(const SEditableGeometry& g)
{
    Array segments;segments.reserve(g.Segments.size());
    for(const auto& s:g.Segments)
    {
        Array sources;
        for(const auto& p:s.Sources)sources.emplace_back(Map{{"port",ToData(p.Source)},{"segmentId",p.SourceSegmentId},
            {"sourceRange",RangeData(p.SourceRange)},{"outputRange",RangeData(p.OutputRange)},{"precision",p.Precision},{"error",p.ErrorBound},{"occurrence",static_cast<unsigned long long>(p.Occurrence)},{"sourceGeometryFingerprint",p.SourceGeometryFingerprint}});
        Map segment{{"id",s.Id},{"curve",ExactData(s.Curve)},{"sources",std::move(sources)},{"construction",s.ConstructionRole}};
        if(s.Parameterization!="Canonical")segment["parameterization"]=s.Parameterization;
        segments.emplace_back(std::move(segment));
    }
    return segments;
}
}
IntentMap ToData(const SToolpathValue& value)
{
    const auto& g=value.Geometry;
    Map geometry{{"schema",std::string("icax.editable.geometry/3")},{"authority",static_cast<int>(g.Authority)},
        {"frame",g.FrameId},{"plane",ExactData(g.Plane)},{"support",ExactData(g.SupportSurface)},
        {"segments",GeometrySegmentsData(g)},{"closed",g.Closed},{"tolerance",g.Tolerance},{"meaning",g.Meaning}};
    return {{"geometry",std::move(geometry)},{"channels",value.Channels}};
}
SToolpathValue ToolpathFromData(const IntentMap& data)
{
    // The serialized value is immutable during parsing. Traverse its nested
    // arrays/maps by reference instead of cloning the full geometry at each level.
    SToolpathValue value;const auto& m=std::get<Map>(At(data,"geometry").m_Value);auto& g=value.Geometry;
    Require(Text(m,"schema")=="icax.editable.geometry/3","TOOLPATH_GEOMETRY_SCHEMA_UNSUPPORTED");
    const auto authority=At(m,"authority").To<int>();Require(authority>=0&&authority<=2,"TOOLPATH_GEOMETRY_AUTHORITY");
    g.Authority=static_cast<EGeometryAuthority>(authority);g.FrameId=Text(m,"frame");
    g.Plane=ExactValue<Placement3>(Text(m,"plane"));g.SupportSurface=ExactValue<std::optional<Surface3>>(Text(m,"support"));
    g.Closed=Flag(m,"closed");g.Tolerance=Number(m,"tolerance");g.Meaning=Text(m,"meaning");
    const auto& segments=std::get<Array>(At(m,"segments").m_Value);Require(segments.size()<=1000000,"TOOLPATH_SEGMENT_BUDGET");
    for(const auto& item:segments)
    {
        const auto& s=std::get<Map>(item.m_Value);SEditableSegment segment;segment.Id=Text(s,"id");segment.ConstructionRole=Text(s,"construction");
        segment.Curve=ExactValue<decltype(segment.Curve)>(Text(s,"curve"));
        segment.Parameterization=Text(s,"parameterization","Canonical");
        Require(segment.Parameterization=="Canonical"||segment.Parameterization=="CadNative","TOOLPATH_PARAMETERIZATION_UNSUPPORTED");
        for(const auto& origin:std::get<Array>(At(s,"sources").m_Value))
        {
            const auto& p=std::get<Map>(origin.m_Value);segment.Sources.push_back({PortFromData(std::get<Map>(At(p,"port").m_Value)),Text(p,"segmentId"),
                NativeRange(std::get<Map>(At(p,"sourceRange").m_Value)),NativeRange(std::get<Map>(At(p,"outputRange").m_Value)),Text(p,"precision"),Number(p,"error"),
                p.contains("occurrence")?UnsignedValue(At(p,"occurrence")):0,Text(p,"sourceGeometryFingerprint")});
        }
        g.Segments.push_back(std::move(segment));
    }
    value.Channels=MapAt(data,"channels");return value;
}
#include "EditableToolpathFingerprint.inc"
SEditableGeometry MakeEditableGeometry(const CompositeCurve3& curve,const std::string& source,const std::string& frame,double tolerance)
{SEditableGeometry g;g.FrameId=frame;g.Tolerance=tolerance;g.Closed=curve.Closed;
 for(std::size_t i=0;i<curve.Segments.size();++i)g.Segments.push_back({source+"/"+std::to_string(i),curve.Segments[i],{}, {}});return g;}
SEditableGeometry MakeEditableGeometry(const CompositeCurve2& curve,const std::string& source,const Placement3& plane,double tolerance)
{SEditableGeometry g;g.Authority=EGeometryAuthority::Planar2D;g.Plane=plane;g.Tolerance=tolerance;g.Closed=curve.Closed;
 for(std::size_t i=0;i<curve.Segments.size();++i)g.Segments.push_back({source+"/"+std::to_string(i),curve.Segments[i],{}, {}});return g;}
}

namespace iCAX::CAM::Intent::Editable
{
Map InputData(const SOperationInput& input)
{Array ranges;for(const auto& r:input.Ranges)ranges.emplace_back(ToData(r));return {{"slot",input.Slot},{"source",ToData(input.Source)},{"replace",input.Replace},{"ranges",std::move(ranges)}};}
SOperationInput InputValue(const Map& data)
{SOperationInput input;input.Slot=Text(data,"slot");input.Source=PortFromData(MapAt(data,"source"));input.Replace=Flag(data,"replace",true);
 if(data.contains("ranges"))for(const auto& r:ArrayAt(data,"ranges"))input.Ranges.push_back(RangeFromData(r.To<Map>()));return input;}
std::vector<SOperationInput> Inputs(const Map& definition)
{std::vector<SOperationInput> result;for(const auto& input:std::get<Array>(At(definition,"inputs").m_Value))result.push_back(InputValue(std::get<Map>(input.m_Value)));return result;}
std::string BoundaryKey(const SPathAnchor& a)
{return Key(a.Port)+"/"+a.SegmentId+"/"+Hash(Map{{"parameter",a.Parameter},{"occurrence",static_cast<unsigned long long>(a.Occurrence)},{"side",a.Side}});}
}
