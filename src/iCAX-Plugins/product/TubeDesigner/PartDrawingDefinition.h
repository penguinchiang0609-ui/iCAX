#pragma once
#include <Data/Variant.h>
#include <Data/VariantSerializer.h>

namespace iCAX::TubeDesigner
{
    // JSON round-trips turn 1.0 into 1. This must not masquerade as a main-blank
    // edit (especially when the project has immutable, degraded cutter nodes).
    inline iCAX::Data::Variant CanonicalDrawingGeometry(const iCAX::Data::Variant& Value)
    {
        using namespace iCAX::Data;
        if(Value.Is<ObjectMap>()) {
            ObjectMap result;for(const auto& [key,child]:Value.To<ObjectMap>())result[key]=CanonicalDrawingGeometry(child);return result;
        }
        if(Value.Is<VariantArray>()) {
            VariantArray result;for(const auto& child:Value.To<VariantArray>())result.push_back(CanonicalDrawingGeometry(child));return result;
        }
        double number;
        if(Value.Is<double>())number=Value.To<double>();
        else if(Value.Is<float>())number=Value.To<float>();
        else if(Value.Is<int>())number=Value.To<int>();
        else if(Value.Is<unsigned int>())number=Value.To<unsigned int>();
        else if(Value.Is<long long>())number=static_cast<double>(Value.To<long long>());
        else if(Value.Is<unsigned long long>())number=static_cast<double>(Value.To<unsigned long long>());
        else if(Value.Is<short>())number=Value.To<short>();
        else if(Value.Is<unsigned short>())number=Value.To<unsigned short>();
        else if(Value.Is<char>())number=Value.To<char>();
        else if(Value.Is<uint8_t>())number=Value.To<uint8_t>();
        else return Value;
        return number==0?0.:number;
    }
    inline std::string PartDrawingGeometrySignature(const iCAX::Data::ObjectMap& Drawing)
    {
        using namespace iCAX::Data;
        const auto profile=Drawing.at("section").To<ObjectMap>().at("profile").To<ObjectMap>();
        return VariantSerializer::Serialize(CanonicalDrawingGeometry(ObjectMap{
            {"length",Drawing.at("length")},{"contours",profile.at("contours")}}));
    }
}
