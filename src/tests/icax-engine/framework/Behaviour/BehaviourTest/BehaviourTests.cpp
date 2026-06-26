#include <gtest/gtest.h>

#include <Behaviour/BehaviourBase.h>
#include <Behaviour/BehaviourRegistrationCatalog.h>
#include <Behaviour/IBehaviourRegistry.h>
#include <Behaviour/IUniverse.h>
#include <ApplicationContext/ApplicationContext.h>
#include <CommandHandler/CommandRegistry.h>
#include <Data/PropertyBag.h>
#include <Data/uuid.h>
#include <Database/ComponentBase.h>
#include <Database/IEntity.h>
#include <Database/IMetaRegistry.h>
#include <Database/IRepository.h>
#include <Database/IRepositoryEvent.h>
#include <Resources/ResourceLibrary.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <ProductContext/IProductContext.h>
#include <ProjectContext/IProjectContext.h>
#include <Services/ServiceProvider.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Windows.h>

namespace
{
    constexpr const char* S_TestComponentClass = "BehaviourTest.Component";
    constexpr const char* S_ValueProperty = "Value";
    constexpr const char* S_OrderEarlyComponentClass = "BehaviourTest.OrderEarlyComponent";
    constexpr const char* S_OrderMiddleComponentClass = "BehaviourTest.OrderMiddleComponent";
    constexpr const char* S_OrderLateComponentClass = "BehaviourTest.OrderLateComponent";
    constexpr const char* S_BindingFirstComponentClass = "BehaviourTest.BindingFirstComponent";
    constexpr const char* S_BindingSecondComponentClass = "BehaviourTest.BindingSecondComponent";
    constexpr const char* S_CatalogPathComponentClass = "BehaviourTest.CatalogPathComponent";
    constexpr const char* S_CycleAComponentClass = "BehaviourTest.CycleAComponent";
    constexpr const char* S_CycleBComponentClass = "BehaviourTest.CycleBComponent";
    constexpr const char* S_CascadeAComponentClass = "BehaviourTest.CascadeAComponent";
    constexpr const char* S_CascadeBComponentClass = "BehaviourTest.CascadeBComponent";

    class CBehaviourTestValueComponent final : public iCAX::Database::CComponentBase
    {
    public:
        CBehaviourTestValueComponent(
            IN std::shared_ptr<iCAX::Database::IEntity> pEntity_,
            IN std::string strClassName_)
            : CComponentBase(std::move(pEntity_))
            , m_strClassName(std::move(strClassName_))
        {
        }

        std::string GetComponentClass() const override
        {
            return m_strClassName;
        }

        std::vector<std::string> GetPropertyNameArray() const override
        {
            std::vector<std::string> _Names;
            _Names.reserve(m_Properties.size());
            for (const auto& [_Name, _] : m_Properties)
            {
                _Names.push_back(_Name);
            }
            return _Names;
        }

        iCAX::Data::PropertyValue GetProperty(IN const std::string& strPropertyName_) const override
        {
            auto _Iter = m_Properties.find(strPropertyName_);
            if (_Iter == m_Properties.end())
            {
                return iCAX::Data::PropertyValue::Nil;
            }
            return _Iter->second;
        }

        void SetRawProperty(IN const std::string& strPropertyName_, IN const iCAX::Data::PropertyValue& NewValue_)
        {
            OnSetProperty(strPropertyName_, NewValue_);
        }

    protected:
        void OnSetProperty(IN const std::string& strPropertyName_, IN const iCAX::Data::PropertyValue& NewValue_) override
        {
            m_Properties[strPropertyName_] = NewValue_;
        }

    private:
        std::string m_strClassName;
        iCAX::Data::PropertySet m_Properties;
    };

    void RegisterBehaviourTestValueComponent(
        IN iCAX::Database::IMetaRegistry& Registry_,
        IN const std::string& strClassName_)
    {
        Registry_.RegistType(strClassName_, iCAX::Database::CComponentBase::S_ClassName);
        Registry_.RegistCreatorByName(
            strClassName_,
            [strClassName_](IN std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
                return std::make_shared<CBehaviourTestValueComponent>(std::move(pEntity_), strClassName_);
            });
        Registry_.RegistPropertyByName(
            strClassName_,
            S_ValueProperty,
            [](IN const void* pObject_) {
                return static_cast<const CBehaviourTestValueComponent*>(pObject_)->GetProperty(S_ValueProperty);
            },
            [](IN void* pObject_, IN const iCAX::Data::PropertyValue& Value_) {
                static_cast<CBehaviourTestValueComponent*>(pObject_)->SetRawProperty(S_ValueProperty, Value_);
            });
    }

    std::shared_ptr<iCAX::Database::IMetaRegistry> CreateBehaviourTestMetaRegistry()
    {
        auto _pMeta = iCAX::Database::CreateMetaRegistry();
        RegisterBehaviourTestValueComponent(*_pMeta, S_TestComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_OrderEarlyComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_OrderMiddleComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_OrderLateComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_BindingFirstComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_BindingSecondComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_CatalogPathComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_CycleAComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_CycleBComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_CascadeAComponentClass);
        RegisterBehaviourTestValueComponent(*_pMeta, S_CascadeBComponentClass);
        return _pMeta;
    }

    class CCountingBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetBehaviourClass() const override
        {
            return "CCountingBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_TestComponentClass;
        }

        int AwakeCount = 0;
        int StartCount = 0;
        int EnableCount = 0;
        int DisableCount = 0;
        int DestroyCount = 0;
        int DestroyImmediateCount = 0;
        int ModifyingCount = 0;
        int ModifiedCount = 0;
        int PreUpdateCount = 0;
        int UpdateCount = 0;
        int PostUpdateCount = 0;
        int DetachCount = 0;
        int ObservedValueDuringModifying = 0;
        double LastDeltaTime = 0.0;
        double LastTotalTime = 0.0;
        iCAX::Data::PropertySet LastAwakeProperties;
        iCAX::Data::PropertySet LastDestroyProperties;
        iCAX::Data::PropertySet LastDestroyImmediateProperties;
        iCAX::Data::PropertySet LastModifyingProperties;
        iCAX::Data::PropertySet LastModifiedProperties;
        iCAX::Data::uuid LastDestroyEntityID;
        std::string LastDestroyComponentClass;

    protected:
        void OnAwake(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++AwakeCount;
            LastAwakeProperties = Component_.GetProperties();
        }

        void OnStart(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++StartCount;
        }

        void OnEnable(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++EnableCount;
        }

        void OnDisable(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++DisableCount;
        }

        void OnDestroy(
            IN const iCAX::Behaviour::CComponentDestroyInfo& DestroyInfo_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++DestroyCount;
            LastDestroyEntityID = DestroyInfo_.EntityID;
            LastDestroyComponentClass = DestroyInfo_.ComponentClass;
            LastDestroyProperties = DestroyInfo_.PreviousProperties;
        }

        void OnDestroyImmediate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++DestroyImmediateCount;
            LastDestroyImmediateProperties = Component_.GetProperties();
        }

        void OnModifying(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Data::PropertySet& NewValues_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++ModifyingCount;
            LastModifyingProperties = NewValues_;

            const auto _CurrentProperties = Component_.GetProperties();
            const auto _Ite = _CurrentProperties.find(S_ValueProperty);
            if (_Ite != _CurrentProperties.end())
            {
                ObservedValueDuringModifying = _Ite->second.To<int>();
            }
        }

        void OnModified(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Data::PropertySet& NewValues_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++ModifiedCount;
            LastModifiedProperties = NewValues_;
        }

        void OnUpdate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN const double& nDeltaTime_,
            IN const double& nTotalTime_) override
        {
            ++UpdateCount;
            LastDeltaTime = nDeltaTime_;
            LastTotalTime = nTotalTime_;
        }

        void OnPreUpdate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN const double&,
            IN const double&) override
        {
            ++PreUpdateCount;
        }

        void OnPostUpdate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN const double&,
            IN const double&) override
        {
            ++PostUpdateCount;
        }

        void OnDetach() override
        {
            ++DetachCount;
        }
    };

    class CTestProductContext final : public iCAX::Product::IProductContext
    {
    public:
        CTestProductContext(
            IN std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry_,
            IN std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry_,
            IN std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry_,
            IN std::shared_ptr<iCAX::Services::CServiceProvider> pServiceProvider_,
            IN std::shared_ptr<iCAX::Command::CCommandRegistry> pCommandRegistry_)
            : m_pMetaRegistry(std::move(pMetaRegistry_))
            , m_pBehaviourRegistry(std::move(pBehaviourRegistry_))
            , m_pResourceLoaderRegistry(std::move(pResourceLoaderRegistry_))
            , m_pServiceProvider(std::move(pServiceProvider_))
            , m_pCommandRegistry(std::move(pCommandRegistry_))
        {
            m_Definition.ProductID = "behaviour-test-product";
            m_Definition.ProductName = "Behaviour Test Product";
        }

        const iCAX::Product::CProductDefinition& GetDefinition() const override
        {
            return m_Definition;
        }

        const std::string& GetProductID() const override
        {
            return m_Definition.ProductID;
        }

        iCAX::Product::CProductData GetProductData() const override
        {
            return m_ProductData;
        }

        iCAX::Services::CServiceProvider& GetServiceProvider() const override
        {
            return *m_pServiceProvider;
        }

        iCAX::Database::IMetaRegistry& GetMetaRegistry() const override
        {
            return *m_pMetaRegistry;
        }

        iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override
        {
            return *m_pBehaviourRegistry;
        }

        iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override
        {
            return *m_pResourceLoaderRegistry;
        }

        iCAX::Command::CCommandRegistry& GetCommandRegistry() const override
        {
            return *m_pCommandRegistry;
        }

    private:
        iCAX::Product::CProductDefinition m_Definition;
        iCAX::Product::CProductData m_ProductData;
        std::shared_ptr<iCAX::Database::IMetaRegistry> m_pMetaRegistry;
        std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pBehaviourRegistry;
        std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pResourceLoaderRegistry;
        std::shared_ptr<iCAX::Services::CServiceProvider> m_pServiceProvider;
        std::shared_ptr<iCAX::Command::CCommandRegistry> m_pCommandRegistry;
    };

    class CTestProjectContext final : public iCAX::Project::IProjectContext
    {
    public:
        CTestProjectContext(
            IN std::shared_ptr<iCAX::Database::IRepository> pRepository_,
            IN std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry_,
            IN std::shared_ptr<iCAX::Services::CServiceProvider> pServiceProvider_)
            : m_ProjectID(pRepository_->GetID())
            , m_ProjectChannelID(iCAX::Data::GenerateNewUUID())
            , m_strProjectName("Behaviour Test Project")
            , m_pRepository(std::move(pRepository_))
            , m_Resources(std::move(pResourceLoaderRegistry_))
            , m_pServiceProvider(std::move(pServiceProvider_))
        {
        }

        const iCAX::Data::uuid& GetProjectID() const override
        {
            return m_ProjectID;
        }

        const iCAX::Data::uuid& GetProjectChannelID() const override
        {
            return m_ProjectChannelID;
        }

        const std::string& GetProjectName() const override
        {
            return m_strProjectName;
        }

        const std::string& GetProjectPath() const override
        {
            return m_strProjectPath;
        }

        iCAX::Database::IRepository& Database() override
        {
            return *m_pRepository;
        }

        const iCAX::Database::IRepository& Database() const override
        {
            return *m_pRepository;
        }

        iCAX::Resource::CResourceLibrary& Resources() override
        {
            return m_Resources;
        }

        const iCAX::Resource::CResourceLibrary& Resources() const override
        {
            return m_Resources;
        }

        iCAX::Services::CServiceProvider& Services() const override
        {
            return *m_pServiceProvider;
        }

    private:
        iCAX::Data::uuid m_ProjectID;
        iCAX::Data::uuid m_ProjectChannelID;
        std::string m_strProjectName;
        std::string m_strProjectPath;
        std::shared_ptr<iCAX::Database::IRepository> m_pRepository;
        iCAX::Resource::CResourceLibrary m_Resources;
        std::shared_ptr<iCAX::Services::CServiceProvider> m_pServiceProvider;
    };

    struct CTestContextBundle final
    {
        CTestContextBundle(
            IN std::shared_ptr<iCAX::Database::IRepository> pRepository_,
            IN std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry_)
            : pRepository(std::move(pRepository_))
            , pBehaviourRegistry(std::move(pBehaviourRegistry_))
            , pMetaRegistry(pRepository->GetMetaRegistry())
            , pResourceLoaderRegistry(std::make_shared<iCAX::Resource::CResourceLoaderRegistry>())
            , pServiceProvider(std::make_shared<iCAX::Services::CServiceProvider>())
            , pCommandRegistry(std::make_shared<iCAX::Command::CCommandRegistry>())
            , Product(pMetaRegistry, pBehaviourRegistry, pResourceLoaderRegistry, pServiceProvider, pCommandRegistry)
            , Project(pRepository, pResourceLoaderRegistry, pServiceProvider)
        {
        }

        std::shared_ptr<iCAX::Database::IRepository> pRepository;
        std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry;
        std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry;
        std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry;
        std::shared_ptr<iCAX::Services::CServiceProvider> pServiceProvider;
        std::shared_ptr<iCAX::Command::CCommandRegistry> pCommandRegistry;
        iCAX::Application::CApplicationContext Application;
        CTestProductContext Product;
        CTestProjectContext Project;
    };

    class CSecondComponentBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetBehaviourClass() const override
        {
            return "CSecondComponentBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_TestComponentClass;
        }
    };

    class COrderedBehaviourBase : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        explicit COrderedBehaviourBase(IN std::shared_ptr<std::vector<std::string>> pLog_)
            : m_pLog(std::move(pLog_))
        {
        }

    protected:
        void PushLog(IN const std::string& strEvent_)
        {
            if (m_pLog)
            {
                m_pLog->push_back(strEvent_);
            }
        }

    private:
        std::shared_ptr<std::vector<std::string>> m_pLog;
    };

    class COrderEarlyBehaviour final : public COrderedBehaviourBase
    {
    public:
        using COrderedBehaviourBase::COrderedBehaviourBase;

        std::string GetBehaviourClass() const override
        {
            return "COrderEarlyBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_OrderEarlyComponentClass;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = -100;
            return _Schedule;
        }

    protected:
        void OnAwake(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("Early.Awake");
        }

        void OnModified(IN iCAX::Database::CComponentBase&, IN const iCAX::Data::PropertySet&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("Early.Modified");
        }

        void OnUpdate(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&, IN const double&, IN const double&) override
        {
            PushLog("Early.Update");
        }
    };

    class COrderMiddleBehaviour final : public COrderedBehaviourBase
    {
    public:
        using COrderedBehaviourBase::COrderedBehaviourBase;

        std::string GetBehaviourClass() const override
        {
            return "COrderMiddleBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_OrderMiddleComponentClass;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.RunAfter = { "COrderEarlyBehaviour" };
            _Schedule.RunBefore = { "COrderLateBehaviour" };
            return _Schedule;
        }

    protected:
        void OnAwake(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("Middle.Awake");
        }

        void OnModified(IN iCAX::Database::CComponentBase&, IN const iCAX::Data::PropertySet&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("Middle.Modified");
        }

        void OnUpdate(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&, IN const double&, IN const double&) override
        {
            PushLog("Middle.Update");
        }
    };

    class COrderLateBehaviour final : public COrderedBehaviourBase
    {
    public:
        using COrderedBehaviourBase::COrderedBehaviourBase;

        std::string GetBehaviourClass() const override
        {
            return "COrderLateBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_OrderLateComponentClass;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = 100;
            return _Schedule;
        }

    protected:
        void OnAwake(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("Late.Awake");
        }

        void OnModified(IN iCAX::Database::CComponentBase&, IN const iCAX::Data::PropertySet&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("Late.Modified");
        }

        void OnUpdate(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&, IN const double&, IN const double&) override
        {
            PushLog("Late.Update");
        }
    };

    class CBindingFirstBehaviour final : public COrderedBehaviourBase
    {
    public:
        using COrderedBehaviourBase::COrderedBehaviourBase;

        std::string GetBehaviourClass() const override
        {
            return "CBindingFirstBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_BindingFirstComponentClass;
        }

    protected:
        void OnAwake(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("BindingFirst.Awake");
        }

        void OnUpdate(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&, IN const double&, IN const double&) override
        {
            PushLog("BindingFirst.Update");
        }
    };

    class CBindingSecondBehaviour final : public COrderedBehaviourBase
    {
    public:
        using COrderedBehaviourBase::COrderedBehaviourBase;

        std::string GetBehaviourClass() const override
        {
            return "CBindingSecondBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_BindingSecondComponentClass;
        }

    protected:
        void OnAwake(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&) override
        {
            PushLog("BindingSecond.Awake");
        }

        void OnUpdate(IN iCAX::Database::CComponentBase&, IN const iCAX::Application::IApplicationContext&, IN const iCAX::Product::IProductContext&, IN iCAX::Project::IProjectContext&, IN const double&, IN const double&) override
        {
            PushLog("BindingSecond.Update");
        }
    };

    class CCatalogPathBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetBehaviourClass() const override
        {
            return "CCatalogPathBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_CatalogPathComponentClass;
        }
    };

    class CCycleABehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetBehaviourClass() const override
        {
            return "CCycleABehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_CycleAComponentClass;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.RunAfter = { "CCycleBBehaviour" };
            return _Schedule;
        }
    };

    class CCycleBBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetBehaviourClass() const override
        {
            return "CCycleBBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_CycleBComponentClass;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.RunAfter = { "CCycleABehaviour" };
            return _Schedule;
        }
    };

    struct CCascadeDestroyState final
    {
        iCAX::Data::uuid BEntityID;
        int ADestroyImmediateCount = 0;
        int ADestroyCount = 0;
        int BDestroyImmediateCount = 0;
        int BDestroyCount = 0;
        bool ARemovedB = false;
        std::string RemoveError;
    };

    class CCascadeDestroyABehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        explicit CCascadeDestroyABehaviour(IN std::shared_ptr<CCascadeDestroyState> pState_)
            : m_pState(std::move(pState_))
        {
        }

        std::string GetBehaviourClass() const override
        {
            return "CCascadeDestroyABehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_CascadeAComponentClass;
        }

    protected:
        void OnDestroy(
            IN const iCAX::Behaviour::CComponentDestroyInfo&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_) override
        {
            ++m_pState->ADestroyCount;

            auto _pEntity = ProjectContext_.Database().GetEntity(m_pState->BEntityID);
            if (_pEntity && _pEntity->HasComponent(S_CascadeBComponentClass))
            {
                m_pState->ARemovedB = _pEntity->RemoveComponent(S_CascadeBComponentClass, m_pState->RemoveError);
            }
        }

        void OnDestroyImmediate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++m_pState->ADestroyImmediateCount;
        }

    private:
        std::shared_ptr<CCascadeDestroyState> m_pState;
    };

    class CCascadeDestroyBBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        explicit CCascadeDestroyBBehaviour(IN std::shared_ptr<CCascadeDestroyState> pState_)
            : m_pState(std::move(pState_))
        {
        }

        std::string GetBehaviourClass() const override
        {
            return "CCascadeDestroyBBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_CascadeBComponentClass;
        }

    protected:
        void OnDestroy(
            IN const iCAX::Behaviour::CComponentDestroyInfo&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++m_pState->BDestroyCount;
        }

        void OnDestroyImmediate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&) override
        {
            ++m_pState->BDestroyImmediateCount;
        }

    private:
        std::shared_ptr<CCascadeDestroyState> m_pState;
    };

    class CUniverseForwarder final : public iCAX::Database::IRepositoryEventListener
    {
    public:
        CUniverseForwarder(
            IN iCAX::Behaviour::IUniverse& Universe_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN const iCAX::Product::IProductContext& ProductContext_,
            IN iCAX::Project::IProjectContext& ProjectContext_)
            : m_Universe(Universe_)
            , m_ApplicationContext(ApplicationContext_)
            , m_ProductContext(ProductContext_)
            , m_ProjectContext(ProjectContext_)
        {
        }

        void OnRepositoryChanging(
            IN void*,
            IN const iCAX::Database::RepositoryEventArgs& Args_) override
        {
            m_Universe.OnRepositoryChanging(m_ApplicationContext, m_ProductContext, m_ProjectContext, Args_);
        }

        void OnRepositoryChanged(
            IN void*,
            IN const iCAX::Database::RepositoryEventArgs& Args_) override
        {
            m_Universe.OnRepositoryChanged(m_ApplicationContext, m_ProductContext, m_ProjectContext, Args_);
        }

    private:
        iCAX::Behaviour::IUniverse& m_Universe;
        const iCAX::Application::IApplicationContext& m_ApplicationContext;
        const iCAX::Product::IProductContext& m_ProductContext;
        iCAX::Project::IProjectContext& m_ProjectContext;
    };

    std::shared_ptr<iCAX::Database::IRepository> CreateRepository()
    {
        return iCAX::Database::GenerateRepository(
            iCAX::Data::GenerateNewUUID(),
            CreateBehaviourTestMetaRegistry());
    }

    std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> CreateRegistryWithCountingBehaviour()
    {
        auto _pRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
        _pRegistry->RegisterBehaviour<CCountingBehaviour>();
        return _pRegistry;
    }

    std::shared_ptr<CCountingBehaviour> GetCountingBehaviour(
        IN const std::shared_ptr<iCAX::Behaviour::IUniverse>& pUniverse_)
    {
        auto _Behaviours = pUniverse_->GetAllBehaviours();
        if (_Behaviours.empty())
        {
            return nullptr;
        }
        return std::dynamic_pointer_cast<CCountingBehaviour>(_Behaviours.front());
    }

    std::string GetCurrentModulePathWithToggledCase()
    {
        std::vector<char> _Buffer(MAX_PATH);
        while (true)
        {
            const auto _Length = GetModuleFileNameA(nullptr, _Buffer.data(), static_cast<DWORD>(_Buffer.size()));
            if (_Length == 0)
            {
                return {};
            }
            if (_Length < _Buffer.size() - 1)
            {
                std::string _Path(_Buffer.data(), _Length);
                for (auto& _Char : _Path)
                {
                    if (_Char >= 'a' && _Char <= 'z')
                    {
                        _Char = static_cast<char>(_Char - 'a' + 'A');
                    }
                    else if (_Char >= 'A' && _Char <= 'Z')
                    {
                        _Char = static_cast<char>(_Char - 'A' + 'a');
                    }
                }
                return _Path;
            }
            _Buffer.resize(_Buffer.size() * 2);
        }
    }
}

TEST(BehaviourRegistryTest, CreatesIndependentBehaviourInstancesPerUniverse)
{
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    auto _pUniverseA = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    auto _pUniverseB = iCAX::Behaviour::GenerateUniverse(_pRegistry);

    ASSERT_TRUE(_pUniverseA->BindBehaviour<CCountingBehaviour>());
    ASSERT_TRUE(_pUniverseB->BindBehaviour<CCountingBehaviour>());

    auto _pBehaviourA = GetCountingBehaviour(_pUniverseA);
    auto _pBehaviourB = GetCountingBehaviour(_pUniverseB);
    ASSERT_NE(nullptr, _pBehaviourA);
    ASSERT_NE(nullptr, _pBehaviourB);
    EXPECT_NE(_pBehaviourA.get(), _pBehaviourB.get());

    _pBehaviourA->AwakeCount = 10;
    EXPECT_EQ(0, _pBehaviourB->AwakeCount);
}

TEST(BehaviourRegistrationCatalogTest, ReplaysModulePathCaseInsensitively)
{
    static int s_ModuleMarker = 0;
    iCAX::Behaviour::CBehaviourRegistrationCatalog::Register(
        [](IN iCAX::Behaviour::IBehaviourRegistry& Registry_)
        {
            Registry_.RegisterBehaviour<CCatalogPathBehaviour>();
        },
        &s_ModuleMarker);

    auto _pRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
    const auto _ModulePath = GetCurrentModulePathWithToggledCase();
    ASSERT_FALSE(_ModulePath.empty());

    iCAX::Behaviour::CBehaviourRegistrationCatalog::ReplayByModulePaths(*_pRegistry, { _ModulePath });

    EXPECT_TRUE(_pRegistry->HasBehaviour<CCatalogPathBehaviour>());
}

TEST(BehaviourDispatcherTest, RejectsUnregisteredBehaviourBinding)
{
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(iCAX::Behaviour::CreateBehaviourRegistry());

    EXPECT_THROW(
        _pUniverse->BindBehaviour<CCountingBehaviour>(),
        std::runtime_error);
}

TEST(BehaviourDispatcherTest, RejectsTwoBehavioursForSameComponent)
{
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    EXPECT_THROW(
        _pRegistry->RegisterBehaviour<CSecondComponentBehaviour>(),
        std::runtime_error);

    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);

    EXPECT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    EXPECT_FALSE(_pUniverse->BindBehaviour<CCountingBehaviour>());
}

TEST(BehaviourDispatcherTest, TickUsesBehaviourScheduleOrder)
{
    auto _pLog = std::make_shared<std::vector<std::string>>();
    auto _pRepository = CreateRepository();
    auto _pRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
    _pRegistry->RegisterBehaviour<COrderLateBehaviour>(_pLog);
    _pRegistry->RegisterBehaviour<COrderMiddleBehaviour>(_pLog);
    _pRegistry->RegisterBehaviour<COrderEarlyBehaviour>(_pLog);
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);

    ASSERT_TRUE(_pUniverse->BindBehaviour<COrderLateBehaviour>());
    ASSERT_TRUE(_pUniverse->BindBehaviour<COrderMiddleBehaviour>());
    ASSERT_TRUE(_pUniverse->BindBehaviour<COrderEarlyBehaviour>());

    auto _Behaviours = _pUniverse->GetAllBehaviours();
    ASSERT_EQ(3u, _Behaviours.size());
    EXPECT_EQ("COrderEarlyBehaviour", _Behaviours[0]->GetBehaviourClass());
    EXPECT_EQ("COrderMiddleBehaviour", _Behaviours[1]->GetBehaviourClass());
    EXPECT_EQ("COrderLateBehaviour", _Behaviours[2]->GetBehaviourClass());

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    (void)_pEntity->AddComponent(S_OrderLateComponentClass);
    (void)_pEntity->AddComponent(S_OrderMiddleComponentClass);
    (void)_pEntity->AddComponent(S_OrderEarlyComponentClass);

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    const std::vector<std::string> _Expected = {
        "Early.Update",
        "Middle.Update",
        "Late.Update",
    };
    EXPECT_EQ(_Expected, *_pLog);
}

TEST(BehaviourDispatcherTest, BatchAwakeAndModifiedUseBehaviourScheduleOrder)
{
    auto _pLog = std::make_shared<std::vector<std::string>>();
    auto _pRepository = CreateRepository();
    auto _pRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
    _pRegistry->RegisterBehaviour<COrderLateBehaviour>(_pLog);
    _pRegistry->RegisterBehaviour<COrderMiddleBehaviour>(_pLog);
    _pRegistry->RegisterBehaviour<COrderEarlyBehaviour>(_pLog);
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<COrderLateBehaviour>());
    ASSERT_TRUE(_pUniverse->BindBehaviour<COrderMiddleBehaviour>());
    ASSERT_TRUE(_pUniverse->BindBehaviour<COrderEarlyBehaviour>());

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    _pRepository->BeginBatch();
    auto _pLate = _pEntity->AddComponent(S_OrderLateComponentClass);
    auto _pMiddle = _pEntity->AddComponent(S_OrderMiddleComponentClass);
    auto _pEarly = _pEntity->AddComponent(S_OrderEarlyComponentClass);
    _pRepository->EndBatch();

    const std::vector<std::string> _ExpectedAwake = {
        "Early.Awake",
        "Middle.Awake",
        "Late.Awake",
    };
    EXPECT_EQ(_ExpectedAwake, *_pLog);

    _pLog->clear();
    std::string _strError;
    _pRepository->BeginBatch();
    ASSERT_TRUE(_pLate->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(1), _strError)) << _strError;
    ASSERT_TRUE(_pMiddle->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(1), _strError)) << _strError;
    ASSERT_TRUE(_pEarly->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(1), _strError)) << _strError;
    _pRepository->EndBatch();

    const std::vector<std::string> _ExpectedModified = {
        "Early.Modified",
        "Middle.Modified",
        "Late.Modified",
    };
    EXPECT_EQ(_ExpectedModified, *_pLog);
}

TEST(BehaviourDispatcherTest, EqualScheduleUsesBindingOrder)
{
    auto _pLog = std::make_shared<std::vector<std::string>>();
    auto _pRepository = CreateRepository();
    auto _pRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
    _pRegistry->RegisterBehaviour<CBindingFirstBehaviour>(_pLog);
    _pRegistry->RegisterBehaviour<CBindingSecondBehaviour>(_pLog);
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);

    ASSERT_TRUE(_pUniverse->BindBehaviour<CBindingSecondBehaviour>());
    ASSERT_TRUE(_pUniverse->BindBehaviour<CBindingFirstBehaviour>());

    auto _Behaviours = _pUniverse->GetAllBehaviours();
    ASSERT_EQ(2u, _Behaviours.size());
    EXPECT_EQ("CBindingSecondBehaviour", _Behaviours[0]->GetBehaviourClass());
    EXPECT_EQ("CBindingFirstBehaviour", _Behaviours[1]->GetBehaviourClass());

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    _pRepository->BeginBatch();
    (void)_pEntity->AddComponent(S_BindingFirstComponentClass);
    (void)_pEntity->AddComponent(S_BindingSecondComponentClass);
    _pRepository->EndBatch();

    const std::vector<std::string> _ExpectedAwake = {
        "BindingSecond.Awake",
        "BindingFirst.Awake",
    };
    EXPECT_EQ(_ExpectedAwake, *_pLog);

    _pLog->clear();
    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    const std::vector<std::string> _ExpectedUpdate = {
        "BindingSecond.Update",
        "BindingFirst.Update",
    };
    EXPECT_EQ(_ExpectedUpdate, *_pLog);
}

TEST(BehaviourDispatcherTest, RejectsBehaviourScheduleCycle)
{
    auto _pRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
    _pRegistry->RegisterBehaviour<CCycleABehaviour>();
    _pRegistry->RegisterBehaviour<CCycleBBehaviour>();
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);

    ASSERT_TRUE(_pUniverse->BindBehaviour<CCycleABehaviour>());
    EXPECT_THROW(
        _pUniverse->BindBehaviour<CCycleBBehaviour>(),
        std::runtime_error);
    EXPECT_FALSE(_pUniverse->HasBindBehaviour<CCycleBBehaviour>());
    EXPECT_EQ(1u, _pUniverse->GetAllBehaviours().size());
}

TEST(BehaviourUniverseTest, BatchChangesDispatchLifecycleAndRefreshView)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());

    _pRepository->BeginBatch();
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(7), _strError)) << _strError;
    _pRepository->EndBatch();

    EXPECT_EQ(1, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifyingCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
    EXPECT_EQ(7, _pBehaviour->LastAwakeProperties[S_ValueProperty].To<int>());
    EXPECT_EQ(1u, _pRepository->GetView().GetEntities(S_TestComponentClass).size());

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);
    EXPECT_EQ(1, _pBehaviour->StartCount);

    _pRepository->BeginBatch();
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(9), _strError)) << _strError;
    _pRepository->EndBatch();

    EXPECT_EQ(1, _pBehaviour->ModifiedCount);
    EXPECT_EQ(9, _pBehaviour->LastModifiedProperties[S_ValueProperty].To<int>());

    _pRepository->BeginBatch();
    _pComponent->Disable();
    _pRepository->EndBatch();
    EXPECT_EQ(1, _pBehaviour->DisableCount);

    _pRepository->BeginBatch();
    _pComponent->Enable();
    _pRepository->EndBatch();
    EXPECT_EQ(1, _pBehaviour->EnableCount);

    _pRepository->BeginBatch();
    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;
    _pRepository->EndBatch();

    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);
    EXPECT_EQ(9, _pBehaviour->LastDestroyImmediateProperties[S_ValueProperty].To<int>());
    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.032);
    EXPECT_EQ(1, _pBehaviour->DestroyCount);
    EXPECT_EQ(9, _pBehaviour->LastDestroyProperties[S_ValueProperty].To<int>());
    EXPECT_TRUE(_pRepository->GetView().GetEntities(S_TestComponentClass).empty());
}

TEST(BehaviourUniverseTest, SingleModifyDispatchesModifyingBeforeModified)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(1), _strError)) << _strError;

    _pBehaviour->ModifyingCount = 0;
    _pBehaviour->ModifiedCount = 0;
    _pBehaviour->ObservedValueDuringModifying = 0;
    _pBehaviour->LastModifyingProperties.clear();
    _pBehaviour->LastModifiedProperties.clear();

    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(2), _strError)) << _strError;

    EXPECT_EQ(1, _pBehaviour->ModifyingCount);
    EXPECT_EQ(1, _pBehaviour->ModifiedCount);
    EXPECT_EQ(1, _pBehaviour->ObservedValueDuringModifying);
    EXPECT_EQ(2, _pBehaviour->LastModifyingProperties[S_ValueProperty].To<int>());
    EXPECT_EQ(2, _pBehaviour->LastModifiedProperties[S_ValueProperty].To<int>());
    EXPECT_EQ(2, _pComponent->GetProperty(S_ValueProperty).To<int>());
}

TEST(BehaviourUniverseTest, SingleRemoveDispatchesDestroyImmediateThenDestroyOnNextTick)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(31), _strError)) << _strError;

    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;

    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);
    EXPECT_EQ(31, _pBehaviour->LastDestroyImmediateProperties[S_ValueProperty].To<int>());

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(1, _pBehaviour->DestroyCount);
    EXPECT_EQ(31, _pBehaviour->LastDestroyProperties[S_ValueProperty].To<int>());
    EXPECT_EQ(_pEntity->GetID(), _pBehaviour->LastDestroyEntityID);
    EXPECT_EQ(S_TestComponentClass, _pBehaviour->LastDestroyComponentClass);
}

TEST(BehaviourUniverseTest, DeleteEntityDispatchesComponentDestroyLifecycle)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _EntityID = iCAX::Data::GenerateNewUUID();
    auto _pEntity = _pRepository->CreateEntity(_EntityID);
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(71), _strError)) << _strError;

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    ASSERT_TRUE(_pRepository->DeleteEntity(_EntityID, _strError)) << _strError;

    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);
    EXPECT_EQ(71, _pBehaviour->LastDestroyImmediateProperties[S_ValueProperty].To<int>());

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    EXPECT_EQ(1, _pBehaviour->DestroyCount);
    EXPECT_EQ(_EntityID, _pBehaviour->LastDestroyEntityID);
    EXPECT_EQ(S_TestComponentClass, _pBehaviour->LastDestroyComponentClass);
    EXPECT_EQ(71, _pBehaviour->LastDestroyProperties[S_ValueProperty].To<int>());
}

TEST(BehaviourUniverseTest, BatchRemoveThenAddDispatchesDestroyAndAwakeInOrder)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(41), _strError)) << _strError;

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    _pRepository->BeginBatch();
    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;
    auto _pNewComponent = _pEntity->AddComponent(S_TestComponentClass);
    _pRepository->EndBatch();

    ASSERT_NE(nullptr, _pNewComponent);
    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);
    EXPECT_EQ(1, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
    EXPECT_EQ(41, _pBehaviour->LastDestroyImmediateProperties[S_ValueProperty].To<int>());
    EXPECT_FALSE(_pBehaviour->LastAwakeProperties.contains(S_ValueProperty));

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    EXPECT_EQ(1, _pBehaviour->DestroyCount);
    EXPECT_EQ(41, _pBehaviour->LastDestroyProperties[S_ValueProperty].To<int>());
}

TEST(BehaviourUniverseTest, BatchModifyThenRemoveDispatchesOnlyDestroyWithRemoveTimeSnapshot)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(51), _strError)) << _strError;

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    _pRepository->BeginBatch();
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(52), _strError)) << _strError;
    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;
    _pRepository->EndBatch();

    EXPECT_EQ(0, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(52, _pBehaviour->LastDestroyImmediateProperties[S_ValueProperty].To<int>());

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    EXPECT_EQ(1, _pBehaviour->DestroyCount);
    EXPECT_EQ(52, _pBehaviour->LastDestroyProperties[S_ValueProperty].To<int>());
}

TEST(BehaviourUniverseTest, BatchRemoveAddModifyDispatchesDestroyAndAwakeWithoutModified)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(61), _strError)) << _strError;

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    _pRepository->BeginBatch();
    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;
    auto _pNewComponent = _pEntity->AddComponent(S_TestComponentClass);
    ASSERT_NE(nullptr, _pNewComponent);
    ASSERT_TRUE(_pNewComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(62), _strError)) << _strError;
    _pRepository->EndBatch();

    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(1, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
    EXPECT_EQ(61, _pBehaviour->LastDestroyImmediateProperties[S_ValueProperty].To<int>());
    EXPECT_EQ(62, _pBehaviour->LastAwakeProperties[S_ValueProperty].To<int>());

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    EXPECT_EQ(1, _pBehaviour->DestroyCount);
    EXPECT_EQ(61, _pBehaviour->LastDestroyProperties[S_ValueProperty].To<int>());
}

TEST(BehaviourUniverseTest, DestroyFlushProcessesCascadeRemovalInSameTick)
{
    auto _pRepository = CreateRepository();
    auto _pState = std::make_shared<CCascadeDestroyState>();
    auto _pRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
    _pRegistry->RegisterBehaviour<CCascadeDestroyABehaviour>(_pState);
    _pRegistry->RegisterBehaviour<CCascadeDestroyBBehaviour>(_pState);
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCascadeDestroyABehaviour>());
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCascadeDestroyBBehaviour>());

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntityA = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pEntityB = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    (void)_pEntityA->AddComponent(S_CascadeAComponentClass);
    (void)_pEntityB->AddComponent(S_CascadeBComponentClass);
    _pState->BEntityID = _pEntityB->GetID();

    std::string _strError;
    ASSERT_TRUE(_pEntityA->RemoveComponent(S_CascadeAComponentClass, _strError)) << _strError;

    EXPECT_EQ(1, _pState->ADestroyImmediateCount);
    EXPECT_EQ(0, _pState->ADestroyCount);
    EXPECT_EQ(0, _pState->BDestroyImmediateCount);
    EXPECT_EQ(0, _pState->BDestroyCount);

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    EXPECT_EQ(1, _pState->ADestroyImmediateCount);
    EXPECT_EQ(1, _pState->ADestroyCount);
    EXPECT_TRUE(_pState->ARemovedB) << _pState->RemoveError;
    EXPECT_EQ(1, _pState->BDestroyImmediateCount);
    EXPECT_EQ(1, _pState->BDestroyCount);
    EXPECT_FALSE(_pEntityB->HasComponent(S_CascadeBComponentClass));

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.032);

    EXPECT_EQ(1, _pState->ADestroyCount);
    EXPECT_EQ(1, _pState->BDestroyCount);
}

TEST(BehaviourUniverseTest, NetEmptyBatchDoesNotDispatchLifecycle)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    _pRepository->BeginBatch();
    (void)_pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;
    _pRepository->EndBatch();

    EXPECT_EQ(0, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);
}

TEST(BehaviourUniverseTest, TickUsesProvidedFrameTime)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    (void)_pEntity->AddComponent(S_TestComponentClass);

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.25, 1.5);

    EXPECT_EQ(1, _pBehaviour->UpdateCount);
    EXPECT_DOUBLE_EQ(0.25, _pBehaviour->LastDeltaTime);
    EXPECT_DOUBLE_EQ(1.5, _pBehaviour->LastTotalTime);
}

TEST(BehaviourUniverseTest, DisabledComponentSkipsFrameUpdateUntilEnabled)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    ASSERT_TRUE(_pComponent->IsEnable());

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);
    EXPECT_EQ(1, _pBehaviour->StartCount);
    EXPECT_EQ(1, _pBehaviour->PreUpdateCount);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);
    EXPECT_EQ(1, _pBehaviour->PostUpdateCount);

    _pComponent->Disable();
    EXPECT_FALSE(_pComponent->IsEnable());
    EXPECT_EQ(1, _pBehaviour->DisableCount);

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.032);
    EXPECT_EQ(1, _pBehaviour->StartCount);
    EXPECT_EQ(1, _pBehaviour->PreUpdateCount);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);
    EXPECT_EQ(1, _pBehaviour->PostUpdateCount);

    _pComponent->Enable();
    EXPECT_TRUE(_pComponent->IsEnable());
    EXPECT_EQ(1, _pBehaviour->EnableCount);

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.048);
    EXPECT_EQ(1, _pBehaviour->StartCount);
    EXPECT_EQ(2, _pBehaviour->PreUpdateCount);
    EXPECT_EQ(2, _pBehaviour->UpdateCount);
    EXPECT_EQ(2, _pBehaviour->PostUpdateCount);
}

TEST(BehaviourUniverseTest, PauseFrameUpdateSkipsOnlyFrameCallbacks)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    EXPECT_EQ(1, _pBehaviour->AwakeCount);

    _pUniverse->PauseFrameUpdate<CCountingBehaviour>();
    EXPECT_TRUE(_pUniverse->IsFrameUpdatePaused<CCountingBehaviour>());
    EXPECT_EQ(1u, _pUniverse->GetFrameUpdatePausedBehaviours().size());

    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(21), _strError)) << _strError;
    _pComponent->Disable();
    _pComponent->Enable();
    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);

    EXPECT_EQ(1, _pBehaviour->ModifyingCount);
    EXPECT_EQ(1, _pBehaviour->ModifiedCount);
    EXPECT_EQ(1, _pBehaviour->DisableCount);
    EXPECT_EQ(1, _pBehaviour->EnableCount);
    EXPECT_EQ(1, _pBehaviour->StartCount);
    EXPECT_EQ(0, _pBehaviour->PreUpdateCount);
    EXPECT_EQ(0, _pBehaviour->UpdateCount);
    EXPECT_EQ(0, _pBehaviour->PostUpdateCount);

    _pUniverse->ResumeFrameUpdate<CCountingBehaviour>();
    EXPECT_FALSE(_pUniverse->IsFrameUpdatePaused<CCountingBehaviour>());
    EXPECT_TRUE(_pUniverse->GetFrameUpdatePausedBehaviours().empty());

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.032);

    EXPECT_EQ(1, _pBehaviour->StartCount);
    EXPECT_EQ(1, _pBehaviour->PreUpdateCount);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);
    EXPECT_EQ(1, _pBehaviour->PostUpdateCount);
}

TEST(BehaviourUniverseTest, BatchEnableDisableDispatchesOnlyNetStateChange)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    _pComponent->Enable();
    EXPECT_TRUE(_pComponent->IsEnable());
    _pBehaviour->EnableCount = 0;
    _pBehaviour->DisableCount = 0;

    _pRepository->BeginBatch();
    _pComponent->Disable();
    _pComponent->Enable();
    _pRepository->EndBatch();

    EXPECT_EQ(0, _pBehaviour->DisableCount);
    EXPECT_EQ(0, _pBehaviour->EnableCount);

    _pRepository->BeginBatch();
    _pComponent->Disable();
    _pComponent->Enable();
    _pComponent->Disable();
    _pRepository->EndBatch();

    EXPECT_FALSE(_pComponent->IsEnable());
    EXPECT_EQ(1, _pBehaviour->DisableCount);
    EXPECT_EQ(0, _pBehaviour->EnableCount);

    _pRepository->BeginBatch();
    _pComponent->Enable();
    _pComponent->Disable();
    _pComponent->Enable();
    _pRepository->EndBatch();

    EXPECT_TRUE(_pComponent->IsEnable());
    EXPECT_EQ(1, _pBehaviour->DisableCount);
    EXPECT_EQ(1, _pBehaviour->EnableCount);
}

TEST(BehaviourUniverseShutdownTest, RemovingRepositoryForwarderStopsEventDispatch)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    EXPECT_EQ(1, _pBehaviour->AwakeCount);

    _pRepository->RemoveObserver(_pForwarder);

    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(11), _strError)) << _strError;
    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;

    EXPECT_EQ(1, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifyingCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);
}

TEST(BehaviourUniverseShutdownTest, ForcedCleanupInvalidatesUniverseAndStopsDispatch)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    EXPECT_EQ(1, _pBehaviour->AwakeCount);

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);
    EXPECT_EQ(1, _pBehaviour->StartCount);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);

    _pUniverse->Cleanup(true);

    EXPECT_FALSE(_pUniverse->IsValid());
    EXPECT_FALSE(_pUniverse->HasBindBehaviour<CCountingBehaviour>());
    EXPECT_TRUE(_pUniverse->GetAllBehaviours().empty());
    EXPECT_FALSE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    EXPECT_EQ(1, _pBehaviour->DetachCount);

    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(12), _strError)) << _strError;
    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.032);

    EXPECT_EQ(1, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifyingCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
    EXPECT_EQ(1, _pBehaviour->StartCount);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);
}

TEST(BehaviourUniverseShutdownTest, UnbindBehaviourStopsEventAndTickDispatch)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pForwarder = std::make_shared<CUniverseForwarder>(
        *_pUniverse,
        _Runtime.Application,
        _Runtime.Product,
        _Runtime.Project);
    _pRepository->AddObserver(_pForwarder);

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    EXPECT_EQ(1, _pBehaviour->AwakeCount);

    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.016);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);

    _pUniverse->UnbindBehaviour<CCountingBehaviour>();

    EXPECT_FALSE(_pUniverse->HasBindBehaviour<CCountingBehaviour>());
    EXPECT_TRUE(_pUniverse->GetAllBehaviours().empty());
    EXPECT_EQ(1, _pBehaviour->DetachCount);

    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(13), _strError)) << _strError;
    _pComponent->Enable();
    _pComponent->Disable();
    ASSERT_TRUE(_pEntity->RemoveComponent(S_TestComponentClass, _strError)) << _strError;
    _pUniverse->Tick(_Runtime.Application, _Runtime.Product, _Runtime.Project, 0.016, 0.032);

    EXPECT_EQ(1, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifyingCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
    EXPECT_EQ(0, _pBehaviour->EnableCount);
    EXPECT_EQ(0, _pBehaviour->DisableCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);
}

TEST(BehaviourUniverseShutdownTest, ExpiredRepositoryForwarderDoesNotKeepUniverseCallbackAlive)
{
    auto _pRepository = CreateRepository();
    auto _pRegistry = CreateRegistryWithCountingBehaviour();
    CTestContextBundle _Runtime(_pRepository, _pRegistry);
    auto _pUniverse = iCAX::Behaviour::GenerateUniverse(_pRegistry);
    ASSERT_TRUE(_pUniverse->BindBehaviour<CCountingBehaviour>());
    auto _pBehaviour = GetCountingBehaviour(_pUniverse);
    ASSERT_NE(nullptr, _pBehaviour);

    {
        auto _pForwarder = std::make_shared<CUniverseForwarder>(
            *_pUniverse,
            _Runtime.Application,
            _Runtime.Product,
            _Runtime.Project);
        _pRepository->AddObserver(_pForwarder);
    }

    auto _pEntity = _pRepository->CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_TestComponentClass);
    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(14), _strError)) << _strError;

    EXPECT_EQ(0, _pBehaviour->AwakeCount);
    EXPECT_EQ(0, _pBehaviour->ModifyingCount);
    EXPECT_EQ(0, _pBehaviour->ModifiedCount);
}
