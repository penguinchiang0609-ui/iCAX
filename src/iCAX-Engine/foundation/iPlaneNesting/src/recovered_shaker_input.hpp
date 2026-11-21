#pragma once
#include "recovered_shaker_configuration.hpp"
#include "recovered_shaker_relations.hpp"

namespace study::recovered {
// Input after SDK geometry normalization. The original geometry/catalog and
// offset polygon production are separate from this scene-input layer.
struct ShakerInputComponent {
    int32_t type=-1;
    std::vector<ShakerPoint> points;
    bool offsetPresent=false;
    double offset=0;
    int32_t sequence=0;
};
struct ShakerInputGeometry {
    ShakerPoint reference{};
    std::vector<ShakerInputComponent> components;
    int32_t sequence=0,label=-1;
    double area=0;
};
struct ShakerInputPart {
    int32_t sequence=0,label=0;
    std::optional<size_t> shape;
    ShakerPoint initialPosition{};
    double initialAngle=0,referenceAngle=0;
};
struct ShakerInputScene {
    uint32_t nextSequence=0;
    uint32_t nextGeometry=0,nextComponent=0;
    std::vector<ShakerInputGeometry> shapes;
    std::vector<ShakerPart> parts;
    std::vector<ShakerInputPart> inputParts;
    std::vector<ShakerRelationPart> relationParts;
    ShakerSceneGeometry geometry;
    ShakerRelations relations;
    std::map<int32_t,size_t> labels;
    std::array<std::optional<size_t>,2> boundaryShapes;
    ShakerBoundaryConfiguration boundaries;
    double maximumX=0,maximumY=0;
};
// 713d60 / 7162b0. Handles are indexes; identity is stable across vector growth.
size_t shakerCreatePart(ShakerInputScene &,std::optional<size_t> geometry,
    ShakerPoint position,double angle,std::array<uint8_t,2> mirror={},uint8_t category=0);
// 70e660 copies bounds verbatim, normalizes only the three lock flags.
int32_t shakerSetMobility(ShakerPart &,std::array<int32_t,3> locked,
    std::array<double,4> bounds,std::array<double,2> angleBounds);
int32_t shakerSetPartLabel(ShakerInputScene &,size_t part,int32_t label); // 70e260
void shakerConfigureOrdinaryRelations(ShakerRelations &); // 70d780
std::vector<ShakerPoint> shakerRectanglePoints(double width,double height); // 715810
bool shakerCreateBoundaryGeometry(ShakerInputScene &,double maximumX,double maximumY,double allowance); // 71a550
int32_t shakerSetBounds(ShakerInputScene &,double xmin,double xmax,double ymin,double ymax); // 70c630
}
