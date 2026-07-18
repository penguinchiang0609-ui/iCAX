#pragma once

#include "Facade.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace iCAX::Interaction
{
    /*
    * @brief 当前运行范围内可访问的 Facade 集合。
    */
    class _FACADES_EXP CFacadeRegistry final
    {
    public:
        CFacadeRegistry() = default;
        ~CFacadeRegistry() = default;

        CFacadeRegistry(IN const CFacadeRegistry&) = delete;
        CFacadeRegistry& operator=(IN const CFacadeRegistry&) = delete;

        bool Register(IN std::shared_ptr<IFacade> pFacade_);
        bool Has(IN uint32_t nFacadeCode_) const;
        std::shared_ptr<IFacade> Find(IN uint32_t nFacadeCode_) const;
        std::vector<uint32_t> GetCodes() const;
        std::vector<CFacadeMethod> GetMethods() const;

    private:
        static void Validate(IN const std::shared_ptr<IFacade>& pFacade_);
        static void ValidateCodeCollision(
            IN const IFacade& ExistingFacade_,
            IN const IFacade& NewFacade_);

    private:
        mutable std::mutex m_Mutex;
        std::map<uint32_t, std::shared_ptr<IFacade>> m_mapFacades;
    };
}
