#pragma once
#include "TubeCadFieldNames.h"
#include "Data/Variant.h"
#include <charconv>
#include <cmath>
#include <limits>

namespace iCAX::CAD::TubeCadJson
{
using Data::Variant; using Data::VariantArray; using Data::ObjectMap;
using GeometryData::Persistence::Members;
inline void Require(bool value) { if(!value)throw std::invalid_argument("Invalid neutral tube CAD value"); }
template<class T> std::string Name() {
    if constexpr(std::is_same_v<T,double>)return "scalar";
    else return std::string(TypeName(static_cast<T*>(nullptr)));
}
class Writer {
public:
    Variant Write(bool value) const {return value;}
    Variant Write(double value) const {Require(std::isfinite(value));return value;}
    template<class T> requires std::is_integral_v<T> Variant Write(T value) const {
        if constexpr(sizeof(T)==8)return std::to_string(value); // JS cannot exactly carry all topology IDs as numbers.
        else if constexpr(std::is_unsigned_v<T>)return static_cast<unsigned long long>(value);
        else return static_cast<int>(value);
    }
    template<class T> requires std::is_enum_v<T> Variant Write(T value) const {return static_cast<int>(value);}
    Variant Write(const std::string& value) const {return value;}
    template<class T> Variant Write(const std::vector<T>& value) const {VariantArray result;result.reserve(value.size());for(const auto& item:value)result.push_back(Write(item));return result;}
    template<class T,std::size_t N> Variant Write(const std::array<T,N>& value) const {VariantArray result;for(const auto& item:value)result.push_back(Write(item));return result;}
    template<class T> Variant Write(const std::optional<T>& value) const {return value?Write(*value):Variant();}
    template<class T> Variant Write(const std::map<std::string,T>& value) const {ObjectMap result;for(const auto& [key,item]:value)result[key]=Write(item);return result;}
    template<class... T> Variant Write(const std::variant<T...>& value) const {
        Require(!value.valueless_by_exception());return std::visit([&](const auto& item)->Variant {
            return ObjectMap{{"type",Name<std::decay_t<decltype(item)>>()},{"value",Write(item)}};
        },value);
    }
    template<class T> requires requires {Members(static_cast<T*>(nullptr));}
    Variant Write(const T& value) const {
        ObjectMap result;const auto names=FieldNames(static_cast<T*>(nullptr));std::size_t index=0;
        std::apply([&](auto... member){((result[std::string(names[index++])]=Write(value.*member)),...);},Members(static_cast<T*>(nullptr)));
        return result;
    }
};
class Reader {
    static long double Number(const Variant& input) {
        return std::visit([](const auto& value)->long double {
            using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_arithmetic_v<T>&&!std::is_same_v<T,bool>)return static_cast<long double>(value);
            else throw std::invalid_argument("Neutral CAD numeric value required");
        },input.m_Value);
    }
public:
    void Read(const Variant& input,bool& value) const {value=input.To<bool>();}
    void Read(const Variant& input,double& value) const {value=static_cast<double>(Number(input));Require(std::isfinite(value));}
    template<class T> requires std::is_integral_v<T> void Read(const Variant& input,T& value) const {
        if constexpr(sizeof(T)==8) {
            const auto text=input.To<std::string>();const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
            Require(parsed.ec==std::errc()&&parsed.ptr==text.data()+text.size());
        }else {
            const auto number=Number(input);Require(std::isfinite(number)&&std::trunc(number)==number
                &&number>=std::numeric_limits<T>::lowest()&&number<=std::numeric_limits<T>::max());value=static_cast<T>(number);
        }
    }
    template<class T> requires std::is_enum_v<T> void Read(const Variant& input,T& value) const {
        std::underlying_type_t<T> raw;Read(input,raw);value=static_cast<T>(raw);
        if constexpr(requires {ValidPersistenceValue(value);})Require(ValidPersistenceValue(value));
        else if constexpr(std::is_same_v<T,GeometryData::ETopologyOrientation>)Require(raw<=3);
        else if constexpr(std::is_same_v<T,GeometryData::EBRepShapeKind>)Require(raw<=7);
    }
    void Read(const Variant& input,std::string& value) const {value=input.To<std::string>();}
    template<class T> void Read(const Variant& input,std::vector<T>& value) const {
        const auto& items=std::get<VariantArray>(input.m_Value);Require(items.size()<=1000000);value.resize(items.size());
        for(std::size_t i=0;i<items.size();++i)Read(items[i],value[i]);
    }
    template<class T,std::size_t N> void Read(const Variant& input,std::array<T,N>& value) const {
        const auto& items=std::get<VariantArray>(input.m_Value);Require(items.size()==N);for(std::size_t i=0;i<N;++i)Read(items[i],value[i]);
    }
    template<class T> void Read(const Variant& input,std::optional<T>& value) const {
        if(input.Is<std::monostate>())value.reset();else{value.emplace();Read(input,*value);}
    }
    template<class T> void Read(const Variant& input,std::map<std::string,T>& value) const {
        value.clear();for(const auto& [key,item]:std::get<ObjectMap>(input.m_Value))Read(item,value[key]);
    }
    template<std::size_t I=0,class... T> void Alternative(const ObjectMap& object,std::variant<T...>& value) const {
        if constexpr(I<sizeof...(T)) {
            if(object.at("type").To<std::string>()==Name<std::variant_alternative_t<I,std::variant<T...>>>()) {
                value.template emplace<I>();Read(object.at("value"),std::get<I>(value));
            }else Alternative<I+1>(object,value);
        }else throw std::invalid_argument("Unsupported neutral CAD geometry type");
    }
    template<class... T> void Read(const Variant& input,std::variant<T...>& value) const {
        const auto& object=std::get<ObjectMap>(input.m_Value);Require(object.size()==2);Alternative(object,value);
    }
    template<class T> requires requires {Members(static_cast<T*>(nullptr));}
    void Read(const Variant& input,T& value) const {
        const auto& object=std::get<ObjectMap>(input.m_Value);const auto names=FieldNames(static_cast<T*>(nullptr));
        Require(object.size()==names.size());std::size_t index=0;
        std::apply([&](auto... member){(Read(object.at(std::string(names[index++])),value.*member),...);},Members(static_cast<T*>(nullptr)));
    }
};
template<class T> Variant Encode(const T& value){return Writer().Write(value);}
template<class T> T Decode(const Variant& value){T result;Reader().Read(value,result);return result;}
}
