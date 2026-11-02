// Numerical convention for the product's direction-only transported head.
// The axis is retained geometrically; only its remaining scalar roll is
// quantized. Component-wise quaternion rounding would also perturb the axis
// and make a closed local orientation loop impossible to reproduce exactly.
#pragma once
#include <GeometryServices/RigidMotionCollisionService.h>
namespace {
namespace CollisionRotation {
    namespace Math=::iCAX::GeometryServices::RigidMotion;
    constexpr double RollQuantumRad=1e-13;
    constexpr double MaximumStepCorrectionRad=1e-12;
    constexpr double Pi=3.14159265358979323846;
    inline Math::Quaternion Between(Math::Vector from,Math::Vector to){
        from=Math::Unit(from);to=Math::Unit(to);const auto cross=Math::Cross(from,to);const auto length=Math::Length(cross),dot=std::clamp(Math::Dot(from,to),-1.,1.);
        if(length==0){if(dot>=0)return {0,0,0,1};int index=0;for(int i=1;i<3;++i)if(std::abs(from[i])<std::abs(from[index]))index=i;Math::Vector basis{};basis[index]=1;const auto axis=Math::Unit(Math::Cross(from,basis));return {axis[0],axis[1],axis[2],0};}
        // Stable also beside the antipode: avoid subtracting almost equal
        // numbers and never substitute a half-turn for a nearby actual axis.
        const double sine=dot<0?std::sqrt((1-dot)/2):length/(2*std::sqrt((1+dot)/2));
        const double cosine=dot<0?length/(2*sine):std::sqrt((1+dot)/2);
        return Math::Unit(Math::Quaternion{cross[0]*sine/length,cross[1]*sine/length,cross[2]*sine/length,cosine});
    }
    inline Math::Quaternion Canonical(Math::Quaternion transported,Math::Vector axis){
        axis=Math::Unit(axis);const auto base=Between({0,0,1},axis);
        const auto local=Math::Multiply({-base[0],-base[1],-base[2],base[3]},transported);
        auto roll=std::remainder(2*std::atan2(local[2],local[3]),2*Pi);
        roll=std::remainder(std::round(roll/RollQuantumRad)*RollQuantumRad,2*Pi);
        return Math::Multiply(base,{0,0,std::sin(roll/2),std::cos(roll/2)});
    }
    inline Math::Quaternion Step(Math::Vector previous,Math::Vector axis,Math::Quaternion rotation,double* maximumObserved=nullptr){
        const auto transported=Math::Multiply(Between(previous,axis),rotation),canonical=Canonical(transported,axis);
        const auto delta=Math::Multiply(canonical,{-transported[0],-transported[1],-transported[2],transported[3]});
        const auto change=2*std::atan2(Math::Length({delta[0],delta[1],delta[2]}),std::abs(delta[3]));
        if(!std::isfinite(change)||change>MaximumStepCorrectionRad)throw std::invalid_argument("CAM_COLLISION_ROTATION_PRECISION");
        if(maximumObserved)*maximumObserved=std::max(*maximumObserved,change);return canonical;
    }
}
}
