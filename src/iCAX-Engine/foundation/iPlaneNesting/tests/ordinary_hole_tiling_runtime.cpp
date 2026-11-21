// Include this production translation unit to inspect its private real variant
// provider through the downstream Amo finalizer, without adding a test API.
#define ordinaryGeneralTilingModelsForInput ordinaryGeneralTilingModelsForInputHoleRuntimeProbe
#include "../src/ordinary_general_tiling.cpp"
#undef ordinaryGeneralTilingModelsForInput
#include "ordinary_rectangle_groups.hpp"
#include "ordinary_rectangle_tiling.hpp"
#include "recovered_amo_input.hpp"
#include <iostream>

// The ordinary production entry remains supplied by the same core objects as
// the SDK; only the private-provider probe above has a distinct exported name.
namespace study {
std::vector<RectangleTilingModel> ordinaryGeneralTilingModelsForInput(
    const Input &, Budget &, double, OrdinaryGeneralTilingTrace *);
}

using namespace study;
namespace {
void shift(PartInput &part) {
    for (auto &point : part.polygon) { point.x += 7.125; point.y -= 12.5; }
    for (auto &hole : part.holes) for (auto &point : hole) { point.x += 7.125; point.y -= 12.5; }
    part.pivot = {9.25, -9.75};
}
Polygon square(double x, double y, double size) {
    return {{x,y},{x+size,y},{x+size,y+size},{x,y+size}};
}
Input primitiveInput(std::vector<Polygon> holes, bool shifted, bool reversed) {
    Input result; result.width = result.height = 30.; result.pitch = .1;
    PartInput part; part.polygon = square(0,0,10); part.holes = std::move(holes);
    part.quantity = 1; part.poses = {{0}};
    if (reversed) for (auto &hole : part.holes) std::reverse(hole.begin(),hole.end());
    if (shifted) shift(part);
    result.parts.push_back(std::move(part)); return result;
}
void expectModels(const Input &input, size_t count, bool force = false) {
    RectanglePrimitiveSettings settings; settings.forceB0 = force;
    const auto actual = ordinaryRectanglePrimitiveGroups(input, settings);
    require(actual.models.size() == count && actual.complexGroups.empty(), PN2S_INTERNAL,
            "Primitive eligibility must use net material and whole-part orientation equivalence");
    for (const auto &model : actual.models) {
        require(model.members.size() == 1 && model.members.front().poseIndex < input.parts.front().poses.size(),
                PN2S_INTERNAL, "Primitive model fabricated an unavailable pose");
        require(model.u.x == 10. && model.v.y == 10., PN2S_INTERNAL,
                "Primitive lattice changed under equivalent source translation/pivot");
    }
}
}
int main() { try {
    size_t primitiveCases = 0, amoCases = 0, modelCases = 0, models = 0, placements = 0;
    for (const bool shifted : {false,true}) for (const bool reversed : {false,true}) {
        auto input = primitiveInput({square(1,2,1)}, shifted, reversed);
        expectModels(input,1); ++primitiveCases;
        input.parts.front().poses = {{90}}; expectModels(input,1); ++primitiveCases;
        input.parts.front().poses = {{0},{90}};
        const auto both = ordinaryRectanglePrimitiveGroups(input);
        require(both.models.size() == 2 && both.models[0].members[0].poseIndex == 0 &&
                both.models[1].members[0].poseIndex == 1, PN2S_INTERNAL,
                "Off-centre hole must preserve the two distinct allowed orientation modules");
        ++primitiveCases;
        input = primitiveInput({square(4.5,4.5,1)},shifted,reversed);
        expectModels(input,2); ++primitiveCases;
        // Rotation permutes the four holes; input order and ring direction
        // cannot make a genuinely symmetric part asymmetric.
        input = primitiveInput({square(1,1,.5),square(8.5,1,.5),square(8.5,8.5,.5),square(1,8.5,.5)},shifted,reversed);
        std::reverse(input.parts[0].holes.begin(),input.parts[0].holes.end());
        std::rotate(input.parts[0].holes[1].begin(),input.parts[0].holes[1].begin()+1,input.parts[0].holes[1].end());
        expectModels(input,2); ++primitiveCases;
        input = primitiveInput({square(1,2,3)},shifted,reversed);
        require(material_area(input.parts[0]) == 91.,PN2S_INTERNAL,"Net-area threshold fixture");
        expectModels(input,0); ++primitiveCases;
        expectModels(input,1,true); ++primitiveCases;
        input = primitiveInput({{{1,1},{2,1},{2,6},{1,6}}},shifted,reversed);
        expectModels(input,0); ++primitiveCases; // Exactly 95 percent is excluded.
        input = primitiveInput({{{1,1},{2,1},{2,5.5},{1,5.5}}},shifted,reversed);
        expectModels(input,1); ++primitiveCases;

        Input tiling; tiling.width = 30.; tiling.height = 20.; tiling.pitch = .1;
        PartInput part; part.polygon = {{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
        part.holes = {square(.5,.5,1),square(.5,3.5,1)};
        part.quantity = 20; part.poses = {{0},{180}};
        if (reversed) for (auto &hole : part.holes) std::reverse(hole.begin(),hole.end());
        if (shifted) shift(part); tiling.parts.push_back(part);
        Budget time(20.); study::PartGeometry geometry(tiling.parts[0], time);
        recovered::GeneralTilingAmoSource source; source.part = 1; source.required = 20;
        source.sheets = {{{30.,20.},0,false}}; source.orientations = geometry.modules;
        source.symmetryX = geometry.symmetry.horizontal; source.symmetryY = geometry.symmetry.vertical;
        source.originalBounds = geometry.original;
        source.variant = [&](auto pose) { return geometry.variant(pose); };
        source.currentVariant = [&](auto pose) { return geometry.variant(pose,true); };
        source.moduleOrientations = [&] { return geometry.modules; };
        size_t finalized = 0;
        const auto prepared = recovered::generalTilingPrepareOrientationInput(source,{0,0,0},{18000000,0,0},
            [&](auto points, double halfGap, bool flag) {
                ++finalized; return recovered::amoTilingCreateInputShape(points,halfGap,flag);
            });
        require(prepared.has_value() && finalized == 2,PN2S_INTERNAL,
                "Both native Amo operands must exclude semantic holes from their positive exterior component union, regardless of winding");
        ++amoCases;
        OrdinaryGeneralTilingTrace trace;
        const auto supplied = ordinaryGeneralTilingModelsForInput(tiling,time,19.,&trace);
        if(supplied.empty()||trace.generalGroups!=1||trace.providerGroups!=1||!trace.missingProviders.empty())
            std::cerr<<"general-hole scene shifted="<<shifted<<" reversed="<<reversed<<" supplied="<<supplied.size()
                     <<" groups="<<trace.generalGroups<<" providers="<<trace.providerGroups<<" missing="<<trace.missingProviders.size()<<'\n';
        require(!supplied.empty() && trace.generalGroups == 1 && trace.providerGroups == 1 && trace.missingProviders.empty(),
                PN2S_INTERNAL,"Real hole tiling must traverse the actual general provider and integer owner");
        RectangleCoreOptions options; options.modules = rectangleExpandModelLimits(supplied);
        const auto result = rectangleCore(tiling,time,options);
        require(result.output.placements.size() == 20 && valid_layout(tiling,result.output),PN2S_INTERNAL,
                "Actual hole tiling models must place all twenty material-aware parts through Rectangle core");
        ++modelCases; models += supplied.size(); placements += result.output.placements.size();
    }
    std::cout << "{\"pass\":true,\"primitiveCases\":" << primitiveCases << ",\"amoCases\":" << amoCases
              << ",\"modelCases\":" << modelCases << ",\"models\":" << models << ",\"placements\":" << placements << "}\n";
    return 0;
} catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
