#pragma once

#include "TubeDesignerExport.h"
#include "ApplicationContext/UserDataStore.h"
#include "TemplateRuntime/TemplateContracts.h"
#include <filesystem>
#include <TopoDS_Shape.hxx>

namespace iCAX::TubeDesigner
{
    inline constexpr const char* kComponentModelFeature = "component-model";
    inline constexpr const char* kComponentModelRecordType = "solid-model";
    inline constexpr const char* kResolvedComponentModels = "tubeDesigner.componentSnapshots";

    // No reference retains a dependency on the original imported file. All
    // snapshots contain validated, millimetre BRep geometry and immutable metadata.
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap ImportComponentModelFile(
        const std::filesystem::path& Source_, const iCAX::Data::ObjectMap& Metadata_);
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap LoadSystemComponentModel(
        const std::filesystem::path& Root_, const std::string& ID_);
    _TUBE_DESIGNER_EXP iCAX::Data::VariantArray ListComponentModelSummaries(
        const std::filesystem::path& Root_, iCAX::Application::IProductUserDataStore* Store_);
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap ResolveComponentModelSnapshot(
        const std::filesystem::path& SystemRoot_, iCAX::Application::IProductUserDataStore* Store_,
        const std::string& Scope_, const std::string& ID_);
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap ComponentModelSummary(
        const iCAX::Data::ObjectMap& Snapshot_, const std::string& Scope_, const std::string& ID_,
        std::uint64_t Revision_ = 0);
    _TUBE_DESIGNER_EXP void UpdateComponentModelMetadata(
        iCAX::Data::ObjectMap& Snapshot_, const iCAX::Data::ObjectMap& Metadata_);
    _TUBE_DESIGNER_EXP TopoDS_Shape ComponentModelShape(const iCAX::Data::ObjectMap& Snapshot_);

    // File access is confined to declared template resources or the model
    // library. The generic geometry evaluator never reads an arbitrary path.
    // Frozen_ != nullptr means manufacturing must use the committed snapshots,
    // even if a library item was subsequently edited or removed.
    _TUBE_DESIGNER_EXP void ResolveTemplateComponentResources(
        iCAX::Data::ObjectMap& Document_, const std::filesystem::path& TemplateDirectory_,
        const iCAX::Data::ObjectMap& DescriptorExtensions_, const std::filesystem::path& SystemRoot_,
        iCAX::Application::IProductUserDataStore* Store_, const iCAX::Data::ObjectMap* Frozen_ = nullptr);
}
