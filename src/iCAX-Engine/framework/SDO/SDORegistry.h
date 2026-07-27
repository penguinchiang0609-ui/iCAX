#pragma once

#include "SDO.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace iCAX::Interaction
{
    /*
    * @brief 当前运行范围内可访问的 SDO 集合。
    */
    class _SDO_EXP CSDORegistry final
    {
    public:
        CSDORegistry() = default;
        ~CSDORegistry() = default;

        CSDORegistry(IN const CSDORegistry&) = delete;
        CSDORegistry& operator=(IN const CSDORegistry&) = delete;

        bool Register(IN std::shared_ptr<ISDO> pSDO_);
        bool Has(IN uint32_t nSDOCode_) const;
        std::shared_ptr<ISDO> Find(IN uint32_t nSDOCode_) const;
        std::vector<uint32_t> GetCodes() const;
        std::vector<CSDOMethod> GetMethods() const;

    private:
        static void Validate(IN const std::shared_ptr<ISDO>& pSDO_);
        static void ValidateCodeCollision(
            IN const ISDO& ExistingSDO_,
            IN const ISDO& NewSDO_);

    private:
        mutable std::mutex m_Mutex;
        std::map<uint32_t, std::shared_ptr<ISDO>> m_mapSDO;
    };
}
