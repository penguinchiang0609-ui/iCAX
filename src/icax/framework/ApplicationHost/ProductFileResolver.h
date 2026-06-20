#pragma once

#include "ApplicationHostExport.h"
#include "Product/ProductDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace ApplicationHost
    {
        enum class EProductFileResolveStatus : uint8_t
        {
            NotFound = 0,
            Resolved = 1,
        };

        struct _APPLICATION_HOST_EXP CProductFileResolveResult final
        {
            std::string ProjectPath;
            EProductFileResolveStatus Status = EProductFileResolveStatus::NotFound;
            std::string ProductID;
            std::vector<std::string> CandidateProductIDs;
            bool bMatchedByMagic = false;
        };

        class _APPLICATION_HOST_EXP CProductFileResolver final
        {
        public:
            static CProductFileResolveResult Resolve(
                IN const std::string& strProjectPath_,
                IN const std::vector<iCAX::Product::CProductDefinition>& Products_);
        };
    }
}
