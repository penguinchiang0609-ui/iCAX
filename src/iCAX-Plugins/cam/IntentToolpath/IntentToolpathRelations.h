#pragma once
#include "IntentToolpathExport.h"
#include "Data/Variant.h"
#include <string>
#include <vector>
namespace iCAX::Database { class IRepository; }
namespace iCAX::CAM::Intent
{
[[nodiscard]] _INTENT_TOOLPATH_EXP bool SetIntentParent(iCAX::Database::IRepository&, const iCAX::Data::uuid&, const iCAX::Data::uuid&, std::string&);
[[nodiscard]] _INTENT_TOOLPATH_EXP std::vector<iCAX::Data::uuid> GetIntentChildEntityIDs(const iCAX::Database::IRepository&, const iCAX::Data::uuid&);
[[nodiscard]] _INTENT_TOOLPATH_EXP std::vector<iCAX::Data::uuid> GetActiveSupersedingDerivationIDs(const iCAX::Database::IRepository&, const iCAX::Data::uuid&);
[[nodiscard]] _INTENT_TOOLPATH_EXP bool IsIntentSuperseded(const iCAX::Database::IRepository&, const iCAX::Data::uuid&);
[[nodiscard]] _INTENT_TOOLPATH_EXP std::vector<iCAX::Data::uuid> CollectEffectiveLeafIntentIDs(const iCAX::Database::IRepository&, const iCAX::Data::uuid&);
}
