#pragma once
#include "MachineResources.h"
#include "GeometryData/GeometryData.h"
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace iCAX::CAM {
// Display tessellation of the source's analytic visual, in source length units.
// It adds no tool, process, joint or collision semantics.
inline iCAX::GeometryData::Triangulation3 TessellateMachinePrimitive(const iCAX::Data::ObjectMap& geometry) {
    const auto number=[](const iCAX::Data::Variant& value){return std::visit([](const auto& v)->double{
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_arithmetic_v<T>&&!std::is_same_v<T,bool>&&!std::is_same_v<T,char>){const double x=static_cast<double>(v);if(std::isfinite(x)&&x>0)return x;}
        throw std::invalid_argument("Invalid primitive dimension");
    },value.m_Value);};
    iCAX::GeometryData::Triangulation3 result;const auto type=geometry.at("type").To<std::string>();
    if(type=="box"){
        const auto size=geometry.at("size").To<iCAX::Data::VariantArray>();if(size.size()!=3)throw std::invalid_argument("Invalid box dimensions");
        const double x=number(size[0])/2,y=number(size[1])/2,z=number(size[2])/2;
        result.Vertices={{-x,-y,-z},{x,-y,-z},{x,y,-z},{-x,y,-z},{-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}};
        result.Triangles={{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
    }else if(type=="cylinder"||type=="sphere"){
        const double radius=number(geometry.at("radius"));constexpr std::uint32_t segments=48;
        if(type=="cylinder"){
            const double half=number(geometry.at("length"))/2;
            for(int row=0;row<2;++row)for(std::uint32_t i=0;i<segments;++i){const double a=2*std::numbers::pi*i/segments;result.Vertices.push_back({radius*std::cos(a),radius*std::sin(a),row?half:-half});}
            result.Vertices.push_back({0,0,-half});result.Vertices.push_back({0,0,half});
            for(std::uint32_t a=0;a<segments;++a){const auto b=(a+1)%segments;result.Triangles.push_back({a,b,b+segments});result.Triangles.push_back({a,b+segments,a+segments});result.Triangles.push_back({2*segments,b,a});result.Triangles.push_back({2*segments+1,a+segments,b+segments});}
        }else{
            constexpr std::uint32_t rows=24;
            for(std::uint32_t j=0;j<=rows;++j)for(std::uint32_t i=0;i<segments;++i){const double p=std::numbers::pi*j/rows,a=2*std::numbers::pi*i/segments;result.Vertices.push_back({radius*std::sin(p)*std::cos(a),radius*std::sin(p)*std::sin(a),radius*std::cos(p)});}
            for(std::uint32_t j=0;j<rows;++j)for(std::uint32_t i=0;i<segments;++i){const auto a=j*segments+i,b=j*segments+(i+1)%segments,c=b+segments,d=a+segments;if(j)result.Triangles.push_back({a,b,d});if(j+1<rows)result.Triangles.push_back({b,c,d});}
        }
    }else throw std::invalid_argument("Unsupported primitive visual: "+type);
    return result;
}
}
