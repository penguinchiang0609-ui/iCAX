#pragma once
#include "EditableToolpath.h"

namespace iCAX::CAM::Intent::Editable
{
IntentMap InitialToolpathLayers(IntentId document);
IntentMap ToolpathLayerCatalog(const IntentMap& document);
void ValidateToolpathLayers(const IntentMap& document,const std::vector<IntentMap>& plans);
IntentMap ToolpathLayerDefaults(const IntentMap& document);
IntentMap ResolveToolpathLayer(const IntentMap& document,const IntentMap& plan,
    const std::map<IntentId,IntentMap>& operations,const std::map<IntentId,IntentMap>& records,
    SPortRef port,const std::function<IntentId(SPortRef)>& pathId);
// Mutates only editable definition values in the command preflight overlay.
bool EditToolpathLayers(const IntentMap& request,IntentMap& document,IntentMap& plan,
    const std::vector<IntentId>& selectedPaths,std::map<IntentId,IntentMap>& otherPlans);
}
