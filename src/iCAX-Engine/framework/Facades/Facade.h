#pragma once

#include "FacadeCall.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Application { class IApplicationContext; }
    namespace Product { class IProductContext; }
    namespace Project
    {
        class IProjectContext;
        class ISceneContext;
    }
}

namespace iCAX::Interaction
{
    /*
    * @brief 产品或宿主对外提供的一组方法。
    * @details Facade 只定义稳定交互面，不对应后端对象、Entity 或 Component。
    */
    class _FACADES_EXP IFacade
    {
    public:
        IFacade() = default;
        virtual ~IFacade() = default;

        IFacade(IN const IFacade&) = delete;
        IFacade& operator=(IN const IFacade&) = delete;

        virtual const std::string& GetName() const = 0;
        virtual uint32_t GetCode() const = 0;
        virtual bool HasMethod(IN uint32_t nMethodCode_) const = 0;
        virtual std::vector<CFacadeMethod> GetMethods() const = 0;

        virtual CFacadeResult Invoke(
            IN const CFacadeCall& Call_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) = 0;
    };

    class _FACADES_EXP CFacade : public IFacade
    {
    public:
        using MethodFunc = std::function<CFacadeResult(
            const CFacadeCall&,
            iCAX::Application::IApplicationContext&,
            iCAX::Product::IProductContext*,
            iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*)>;

    protected:
        explicit CFacade(IN std::string strName_);

    public:
        ~CFacade() override = default;

        CFacade(IN const CFacade&) = delete;
        CFacade& operator=(IN const CFacade&) = delete;

        const std::string& GetName() const override;
        uint32_t GetCode() const override;
        bool HasMethod(IN uint32_t nMethodCode_) const override;
        std::vector<CFacadeMethod> GetMethods() const override;

        CFacadeResult Invoke(
            IN const CFacadeCall& Call_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) override;

    protected:
        /*
        * @brief 声明一个对外方法。
        * @details Facade 构造完成后没有替换或移除方法的入口，保证交互协议稳定。
        */
        bool ExposeMethod(IN std::string strMethodName_, IN MethodFunc Func_);

    private:
        struct MethodRecord final
        {
            CFacadeMethod Method;
            MethodFunc Func;
        };

        static void ValidateMethodFunc(IN const MethodFunc& Func_);

    private:
        std::string m_strName;
        uint32_t m_nCode = 0;
        mutable std::mutex m_Mutex;
        std::map<uint32_t, MethodRecord> m_mapMethods;
    };
}
