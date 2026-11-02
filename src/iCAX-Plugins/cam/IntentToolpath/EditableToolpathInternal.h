#pragma once
#include "EditableToolpath.h"
#include "ComponentValueHelpers.h"
#include "Database/DerivedComponentBase.h"
#include "Data/VariantSerializer.h"
#include "GeometryData/BRepPersistence.h"
#include <cmath>
#include <stdexcept>

namespace iCAX::CAM::Intent::Editable
{
using namespace iCAX::Data;
using namespace iCAX::GeometryData;
using Map = IntentMap;
using Array = IntentArray;
inline void Require(bool condition, const std::string& message)
{ if (!condition) throw std::invalid_argument(message); }
inline const Variant& At(const Map& m, const std::string& key)
{ const auto i=m.find(key); if(i==m.end()) throw std::invalid_argument("TOOLPATH_FIELD_REQUIRED: "+key); return i->second; }
inline std::string Text(const Map& m,const std::string& k,const std::string& fallback={})
{const auto i=m.find(k);return i==m.end()?fallback:i->second.To<std::string>();}
inline bool Flag(const Map& m,const std::string& k,bool fallback=false)
{const auto i=m.find(k);return i==m.end()?fallback:i->second.To<bool>();}
inline bool IsNumericValue(const Variant& value)
{return value.Is<double>()||value.Is<float>()||value.Is<int>()||value.Is<unsigned int>()||value.Is<long long>()||value.Is<unsigned long long>();}
inline double NumericValue(const Variant& value)
{
    double number=0;
    if(value.Is<double>())number=value.To<double>();else if(value.Is<float>())number=value.To<float>();
    else if(value.Is<int>())number=value.To<int>();else if(value.Is<unsigned int>())number=value.To<unsigned int>();
    else if(value.Is<long long>()){const auto integer=value.To<long long>();Require(integer>=-9007199254740991LL&&integer<=9007199254740991LL,"TOOLPATH_NUMBER_INTEGER_PRECISION");number=static_cast<double>(integer);}
    else if(value.Is<unsigned long long>()){const auto integer=value.To<unsigned long long>();Require(integer<=9007199254740991ULL,"TOOLPATH_NUMBER_INTEGER_PRECISION");number=static_cast<double>(integer);}
    else throw std::invalid_argument("TOOLPATH_NUMBER_REQUIRED");
    Require(std::isfinite(number),"TOOLPATH_NONFINITE");return number;
}
inline double Number(const Map& m,const std::string& k){return NumericValue(At(m,k));}
inline std::uint64_t UnsignedValue(const Variant& value)
{
    if(value.Is<unsigned long long>())return value.To<unsigned long long>();if(value.Is<unsigned int>())return value.To<unsigned int>();
    if(value.Is<int>()){const auto number=value.To<int>();Require(number>=0,"TOOLPATH_UNSIGNED_INTEGER_REQUIRED");return static_cast<std::uint64_t>(number);}
    if(value.Is<long long>()){const auto number=value.To<long long>();Require(number>=0,"TOOLPATH_UNSIGNED_INTEGER_REQUIRED");return static_cast<std::uint64_t>(number);}
    if(value.Is<double>()||value.Is<float>())
    {const auto number=value.Is<double>()?value.To<double>():static_cast<double>(value.To<float>());const auto limit=value.Is<float>()?16777215.:9007199254740991.;
     Require(std::isfinite(number)&&number>=0&&number<=limit&&std::floor(number)==number,"TOOLPATH_UNSIGNED_INTEGER_PRECISION");return static_cast<std::uint64_t>(number);}
    throw std::invalid_argument("TOOLPATH_UNSIGNED_INTEGER_REQUIRED");
}
inline IntentId ID(const Variant& v)
{if(v.Is<IntentId>())return v.To<IntentId>();const auto p=IntentId::from_string(v.To<std::string>());Require(p.has_value(),"TOOLPATH_INVALID_UUID");return *p;}
inline std::string Key(IntentId id){return iCAX::Data::to_string(id);}
inline std::string Key(const SPortRef& p){return Key(p.OperationId)+"/"+Key(p.PortId);}
inline IntentId NamedID(const std::string& key)
{uuid_name_generator g(*IntentId::from_string("521a410c-035b-5b8f-a09e-08b8d608a7e2"));return g(key);}
inline std::string Hash(const Variant& v)
{return Key(NamedID(VariantSerializer::Serialize(v)));}
inline Map MapAt(const Map& m,const std::string& k){return At(m,k).To<Map>();}
inline Array ArrayAt(const Map& m,const std::string& k){return At(m,k).To<Array>();}
inline Map RangeData(const ParameterRange& r){return {{"first",r.First},{"last",r.Last},{"reversed",r.Reversed}};}
inline ParameterRange NativeRange(const Map& m){return {Number(m,"first"),Number(m,"last"),Flag(m,"reversed")};}
inline ParameterRange Range(const SEditableSegment& s){return std::visit([](const auto& c){if constexpr(requires{c.Source.Range;})return c.Source.Range;else return c.Range;},s.Curve);}
inline void SetRange(SEditableSegment& s,ParameterRange r){std::visit([&](auto& c){if constexpr(requires{c.Source.Range;})c.Source.Range=r;else c.Range=r;},s.Curve);}
inline std::size_t GeometryCurveFamily(const SEditableSegment& s)
{return std::visit([](const auto& c){if constexpr(requires{c.Source.Curve;})return c.Source.Curve.index();else return c.Curve.index();},s.Curve);}
inline double First(const ParameterRange& r){return r.Reversed?r.Last:r.First;}
inline double Last(const ParameterRange& r){return r.Reversed?r.First:r.Last;}
inline double Distance(Point3 a,Point3 b){return std::hypot(a.X-b.X,a.Y-b.Y,a.Z-b.Z);}
inline Point3 Local(const Placement3& p,double x,double y,double z=0)
{return {p.Location.X+x*p.XDirection.X+y*p.YDirection.X+z*p.ZDirection.X,
 p.Location.Y+x*p.XDirection.Y+y*p.YDirection.Y+z*p.ZDirection.Y,
 p.Location.Z+x*p.XDirection.Z+y*p.YDirection.Z+z*p.ZDirection.Z};}
template<class T> std::string ExactData(const T& value)
{
    Persistence::Writer writer;writer.Write(value);
    static constexpr char digits[]="0123456789abcdef";
    std::string hex;hex.reserve(writer.Bytes.size()*2);
    for(auto b:writer.Bytes){hex+=digits[b>>4];hex+=digits[b&15];}return hex;
}
template<class T> T ExactValue(const std::string& hex)
{
    Require(hex.size()%2==0&&hex.size()<=128*1024*1024,"TOOLPATH_EXACT_DATA_SIZE");
    const auto digit=[](char c)->unsigned {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::invalid_argument("TOOLPATH_EXACT_DATA_HEX");};
    std::vector<std::uint8_t> bytes;bytes.reserve(hex.size()/2);
    for(std::size_t i=0;i<hex.size();i+=2)bytes.push_back(static_cast<std::uint8_t>((digit(hex[i])<<4)|digit(hex[i+1])));
    Persistence::Reader reader(bytes);T value;reader.Read(value);Require(reader.End(),"TOOLPATH_EXACT_DATA_TRAILING");return value;
}

class CEditableDocumentComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CEditableDocumentComponent,CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CEditableDocumentComponent)
    DECLARED_ICAX_FIELD(CEditableDocumentComponent,Map,Definition,Map(),ObjectMapEqual,ToObjectMapVariant,FromObjectMapVariant)
};
class CEditablePlanComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CEditablePlanComponent,CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CEditablePlanComponent)
    DECLARED_ICAX_FIELD(CEditablePlanComponent,Map,Definition,Map(),ObjectMapEqual,ToObjectMapVariant,FromObjectMapVariant)
};
class CEditableOperationComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CEditableOperationComponent,CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CEditableOperationComponent)
    DECLARED_ICAX_FIELD(CEditableOperationComponent,Map,Definition,Map(),ObjectMapEqual,ToObjectMapVariant,FromObjectMapVariant)
};
class CEditablePathLabelsComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CEditablePathLabelsComponent,CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CEditablePathLabelsComponent)
    DECLARED_ICAX_FIELD(CEditablePathLabelsComponent,std::string,Name,std::string(),StringEqual,ToStringVariant,FromStringVariant)
    DECLARED_ICAX_FIELD(CEditablePathLabelsComponent,std::string,UserNumber,std::string(),StringEqual,ToStringVariant,FromStringVariant)
    DECLARED_ICAX_FIELD(CEditablePathLabelsComponent,bool,OwnLocked,false,BoolEqual,ToBoolVariant,FromBoolVariant)
};
class CEditableDerivedComponent final : public iCAX::Database::CDerivedComponentBase
{
    DECLARE_ICAX_COMPONENT(CEditableDerivedComponent,CDerivedComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CEditableDerivedComponent)
    DECLARED_ICAX_FIELD(CEditableDerivedComponent,IntentId,Owner,IntentId(),UuidEqual,ToUuidVariant,FromUuidVariant)
    DECLARED_ICAX_FIELD(CEditableDerivedComponent,Map,Data,Map(),ObjectMapEqual,ToObjectMapVariant,FromObjectMapVariant)
public:
    IntentId GetDerivedOwnerID() const override{return GetOwner();}
};

Map InputData(const SOperationInput&);
SOperationInput InputValue(const Map&);
std::vector<SOperationInput> Inputs(const Map&);
void ValidateCandidate(const SOperationEvaluation&,const SOperationCandidate&,const IToolpathGeometryServices&);
void ValidateParameterSchema(const IntentMap& schema,const IntentMap& parameters);
void PropagateChannels(const SOperationEvaluation&,SOperationCandidate&,const SOperationType&);
void RebindChannelPorts(IntentMap& channels,const SPortRef& from,const SPortRef& to);
SEditableGeometry Slice(const SFrozenPathInput&,double first,double last,const std::string& role);
double Locate(const SPathAnchor&,const SFrozenPathInput&);
SPathAnchor AnchorAt(const SFrozenPathInput&,double position);
SPathRange NormalizeRange(SPathRange,const SFrozenPathInput&);
std::vector<std::pair<double,double>> RangeWindows(const SPathRange&,const SFrozenPathInput&);
std::string BoundaryKey(const SPathAnchor&);
// Pure preflight: no Repository access and no requirement to load saved algorithms.
_INTENT_TOOLPATH_EXP void ValidateToolpathImport(const IntentMap&,const IToolpathGeometryServices&);
}
