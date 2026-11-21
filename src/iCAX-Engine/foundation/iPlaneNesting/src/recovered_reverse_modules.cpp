#include "recovered_reverse_modules.hpp"
namespace study::recovered {
std::vector<PlacementOrientation> reversePartOrientationCache(std::span<const PlacementOrientation> source,
    bool symmetryX,bool symmetryY){std::vector<PlacementOrientation> result;for(const auto orientation:source)insertOrientation(result,canonicalOrientation(composeOrientation(orientation,18000000,true),symmetryX,symmetryY));return result;}
std::optional<uintptr_t> currentPartModule(std::span<const CurrentPartModule> modules,uintptr_t part,
    PlacementOrientation requested,bool symmetryX,bool symmetryY){requested=canonicalOrientation(requested,symmetryX,symmetryY);for(const auto &candidate:modules)if(candidate.part==part&&candidate.orientation.angle==requested.angle)return candidate.module;return {};}
}
