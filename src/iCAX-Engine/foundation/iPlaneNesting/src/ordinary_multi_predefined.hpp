#pragma once
#include "ordinary_predefined.hpp"
#include "recovered_multi_predefined.hpp"

namespace study {
struct OrdinaryMultiPredefinedSheet {
    uintptr_t sheetIdentity=0;
    double width=0,height=0;
    OrdinaryPredefinedProgram predefined;
};
struct OrdinaryMultiPredefinedTrace {
    size_t globalCatalogBuilds=0,globalGeometryBuilds=0,submittedGroups=0,submittedSheets=0;
    size_t committed=0,geometryOwners=0;
    std::vector<OrdinaryPredefinedTrace> sheets;
};
// Each board has a real native nesting and strong placement owners. Its
// lifecycle temporarily receives the scene's one shared catalog/instance
// table while committing; no per-board concrete-instance copies are made.
struct OrdinaryMultiPredefinedBoard {
    OrdinaryMultiPredefinedSheet source;
    recovered::WarmNesting nesting;
    recovered::PlacementLifecycle lifecycle;
    Output output;
    explicit OrdinaryMultiPredefinedBoard(OrdinaryMultiPredefinedSheet);
};
struct OrdinaryMultiPredefinedScene {
    Input input;
    std::optional<recovered::Catalog> catalog;
    recovered::PlacementStateCatalog placementCatalog{std::span<const size_t>{}};
    std::vector<recovered::PlacementInstanceState> instances;
    std::vector<std::vector<recovered::WarmPlacement>> modules;
    std::vector<uintptr_t> sdkIdentities;
    std::map<uintptr_t,size_t> instanceBySdk;
    std::vector<std::unique_ptr<OrdinaryMultiPredefinedBoard>> boards;
    std::vector<uintptr_t> unplaced;
    int64_t pitchX=0;
};
// Real multi-board manual geometry entry. The global input quantities must
// describe the exact unique concrete demand present in the sheet records.
// Supported: finite exact-grid single rings, finite poses, no spacing or
// cross-part point groups. Manual overlaps/outside-sheet positions are allowed.
// Repeated SDK IDs or (group,ordinal) records are explicitly unsupported.
std::unique_ptr<OrdinaryMultiPredefinedScene> ordinaryMultiPreNest(const Input &,
    std::span<const OrdinaryMultiPredefinedSheet>,Budget &,OrdinaryMultiPredefinedTrace * = nullptr);
}
