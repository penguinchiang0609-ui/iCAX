#pragma once
#include "recovered_shaker.hpp"

namespace study::recovered {
struct ShakerRelationValue {
    int32_t tag=0,category=0;
    double value=0;
};
using ShakerRelationMap=std::map<int32_t,ShakerRelationValue>;
struct ShakerRelationTypes {
    std::vector<std::vector<uint32_t>> rows;
    std::map<int32_t,int32_t> types;
    int32_t append(); // b744d0: preserve old bits, append a zero row/column
    int32_t index(int32_t shapeKey); // 71f570: inserts previously unseen types
    bool related(int32_t first,int32_t second) const;
    void set(int32_t first,int32_t second,bool value);
};
struct ShakerMovement {
    std::map<uintptr_t,int32_t> indices;
    std::vector<int32_t> parents;
    std::vector<int32_t> ranks; // 71ac90 initializes a zero rank per new member
    int32_t root(int32_t index); // 7157c0, path compression
    // 71ae10/3ba720: missing keys are inserted with index zero; the scene
    // builder must supply a valid parent array. Lookup order is second, first.
    bool common(uintptr_t first,uintptr_t second);
};
bool shakerRelationDominated(const ShakerRelationTypes &,const ShakerRelationMap &,
    const ShakerRelationMap &,int32_t firstType,int32_t secondType); // b74ac0
struct ShakerRelationPart {
    int32_t id=0;
    uintptr_t identity=0;
    std::array<uint8_t,3> locked{};
    bool force143=false;
    ShakerRelationMap values;
};
struct ShakerRelations {
    ShakerRelationTypes types;
    ShakerMovement x,y;
    // 71a730. Type lookup and movement lookup mutate the original scene.
    bool excluded(bool rotation,const ShakerRelationPart &,const ShakerRelationPart &,
        int32_t firstShapeKey,int32_t secondShapeKey);
};
}
