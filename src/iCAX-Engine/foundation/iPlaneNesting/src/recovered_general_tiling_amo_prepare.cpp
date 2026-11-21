#include "recovered_general_tiling_amo.hpp"
namespace study::recovered {
namespace {
bool excluded(const GeneralTilingAmoSheet &value){
    if(!value.contourRecords)return false;
    const auto &s=value.sheet;
    const size_t trimRecords=size_t(0.<s.top)+size_t(!(s.right<=0.))+size_t(!(s.bottom<=0.))+size_t(!(s.left<=0.));
    return value.contourRecords!=trimRecords;
}
AmoTilingTransformedShape operand(std::span<const GeneralTilingComponent> components,double gap,
    const AmoTilingInputFinalizer &finalize){
    require(!components.empty(),PN2S_INVALID,"Amo variant has no base component");
    const auto create=[&](const GeneralTilingComponent &component){
        auto result=finalize(component.points,gap*.5,false); //8b4190 initializes job+28 to0
        require(!result.properties.flag59&&!result.properties.flag5a,PN2S_INVALID,"Invalid finalized Amo component");
        return result;
    };
    auto result=create(components.front());
    for(const auto &component:components.subspan(1))if(0.<component.signedArea){
        const auto child=create(component);amoTilingAttachComponent(result,child);
    }
    return result;
}
}
Point generalTilingAmoSheetExtents(std::span<const GeneralTilingAmoSheet> sheets){
    Point result{};
    for(const auto &value:sheets)if(!excluded(value)){double width=value.sheet.usable().x;if(width<=result.x)width=result.x;result.x=width;
        const double height=value.doubledHeight68?value.sheet.height+value.sheet.height:value.sheet.usable().y;if(result.y<=height)result.y=height;}
    if(result.x==0.)for(const auto &value:sheets){double width=value.sheet.usable().x;if(width<=result.x)width=result.x;result.x=width;}
    if(result.y==0.)for(const auto &value:sheets){const double height=value.doubledHeight68?value.sheet.height+value.sheet.height:value.sheet.usable().y;if(result.y<=height)result.y=height;}
    return result;
}
std::optional<GeneralTilingAmoPreparation> generalTilingPrepareOrientationInput(
    const GeneralTilingAmoSource &source,PlacementOrientation first,PlacementOrientation second,
    const AmoTilingInputFinalizer &finalize){
    require(bool(source.variant)&&bool(finalize),PN2S_INVALID,"Amo upper production requires actual variant and component services");
    first=canonicalOrientation(first,source.symmetryX,source.symmetryY);
    second=canonicalOrientation(second,source.symmetryX,source.symmetryY);
    const auto &lookup=source.currentVariant?source.currentVariant:source.variant;
    if(source.check)source.check();
    const std::array variants{lookup(first),lookup(second)};
    if(!variants[1]||variants[1]->orientation.angle!=second.angle)return {};
    require(bool(variants[0]),PN2S_INVALID,"First canonical Amo variant is missing");
    GeneralTilingAmoPreparation result;
    for(size_t member=0;member<2;++member){auto input=operand(variants[member]->components,source.gap,finalize);
        result.job.members[member]=std::move(input.geometry);
        result.request.sourceFlags[member]={input.properties.flag59!=0,input.properties.flag5a!=0};}
    AmoTilingOrientationRange range;
    result.request.check=source.check;
    if(equivalentOrientation(source.orientations,{1800000,0,0},source.symmetryX,source.symmetryY).found&&
       equivalentOrientation(source.orientations,{22200000,0,0},source.symmetryX,source.symmetryY).found)
        range={180.,source.angleStep988};
    else range=amoTilingOrientationRange(source.moduleOrientations,first,second,source.symmetryX,source.symmetryY);
    // 8b4710 alone enables edge angles: a zero supplied step becomes5 degrees.
    result.request.angles={0.,range.maximum,range.step==0.?5.:range.step,range.step==0.};
    const auto usable=generalTilingAmoSheetExtents(source.sheets);result.request.criterion.width=source.gap+usable.x;result.request.criterion.height=source.gap+usable.y;
    result.required=source.optional?0:source.required;
    result.conversion.part=source.part;result.conversion.base={first,second};result.conversion.originalBounds=source.originalBounds;
    result.conversion.symmetryX=source.symmetryX;result.conversion.symmetryY=source.symmetryY;
    result.conversion.required=source.required;result.conversion.maximumX=source.maximumX;result.conversion.maximumY=source.maximumY;
    result.conversion.variantBounds=[&source](PlacementOrientation orientation){const auto variant=source.variant(orientation);require(bool(variant),PN2S_INVALID,"Amo conversion canonical variant is missing");return variant->bounds;};
    return result;
}
}
