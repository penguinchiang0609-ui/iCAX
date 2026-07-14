#include <gtest/gtest.h>

#include <Project/ProjectCatalog.h>
#include <Project/Project.h>
#include <Project/ProjectRuntime.h>
#include <ApplicationContext/ApplicationContext.h>
#include <Behaviour/BehaviourBase.h>
#include <Behaviour/IBehaviourRegistry.h>
#include <CommandTargets/CommandRoute.h>
#include <CommandTargets/CommandRegistry.h>
#include <Database/ComponentBase.h>
#include <Database/IMetaRegistry.h>
#include <PDO/PDOLease.h>
#include <PDO/SharedPDOArena.h>
#include <Resources/IResourceLoader.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <ProductContext/IProductContext.h>
#include <Mailbox/MailPayload.h>
#include <Mailbox/MailChannelRegistry.h>
#include <Services/ServiceProvider.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <utility>

using namespace iCAX::Project;

namespace
{
    constexpr const char* S_TestQuickSaveLogMagic = "ICAX_PROJECT_TEST_QUICK_SAVE_LOG";
    constexpr uint32_t S_TestQuickSaveLogVersion = 1;
    constexpr const char* S_FrameTimeComponentClass = "ProjectTest.FrameTimeComponent";
    constexpr const char* S_LifecycleComponentClass = "ProjectTest.LifecycleComponent";
    constexpr const char* S_QuickSaveComponentClass = "QuickSaveComponent";
    constexpr const char* S_ValueProperty = "Value";

    struct ProjectPDOPayload
    {
        int Value = 0;
    };

    struct TextResource
    {
        std::string Text;
    };

    class CProjectTestValueComponent final : public iCAX::Database::CComponentBase
    {
    public:
        CProjectTestValueComponent(
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

    void RegisterProjectTestValueComponent(
        IN iCAX::Database::IMetaRegistry& Registry_,
        IN const std::string& strClassName_)
    {
        Registry_.RegistType(strClassName_, iCAX::Database::CComponentBase::S_ClassName);
        Registry_.RegistCreatorByName(
            strClassName_,
            [strClassName_](IN std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
                return std::make_shared<CProjectTestValueComponent>(std::move(pEntity_), strClassName_);
            });
        Registry_.RegistPropertyByName(
            strClassName_,
            S_ValueProperty,
            [](IN const void* pObject_) {
                return static_cast<const CProjectTestValueComponent*>(pObject_)->GetProperty(S_ValueProperty);
            },
            [](IN void* pObject_, IN const iCAX::Data::PropertyValue& Value_) {
                static_cast<CProjectTestValueComponent*>(pObject_)->SetRawProperty(S_ValueProperty, Value_);
            });
    }

    std::shared_ptr<iCAX::Database::IMetaRegistry> CreateProjectTestMetaRegistry()
    {
        auto _pMeta = iCAX::Database::CreateMetaRegistry();
        RegisterProjectTestValueComponent(*_pMeta, S_FrameTimeComponentClass);
        RegisterProjectTestValueComponent(*_pMeta, S_LifecycleComponentClass);
        RegisterProjectTestValueComponent(*_pMeta, S_QuickSaveComponentClass);
        return _pMeta;
    }

    class FixedTextLoader final : public iCAX::Resource::IResourceLoader
    {
    public:
        explicit FixedTextLoader(std::string Text_)
            : m_Text(std::move(Text_))
        {
        }

        bool CanLoad(IN const iCAX::Resource::CResourceLoadContext& Context_) const override
        {
            return Context_.TargetResourceType == std::type_index(typeid(TextResource));
        }

        iCAX::Resource::CResourceLoadResult Load(IN const iCAX::Resource::CResourceLoadContext& Context_) override
        {
            auto _pText = std::make_shared<TextResource>();
            _pText->Text = m_Text;

            auto _Info = Context_.Info;
            _Info.Key = Context_.TargetKey;
            _Info.Source = Context_.Source;
            return iCAX::Resource::CResourceLoadResult::Succeeded(_Info, _pText);
        }

    private:
        std::string m_Text;
    };

    class CFrameTimeBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetBehaviourClass() const override
        {
            return "CFrameTimeBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_FrameTimeComponentClass;
        }

        int UpdateCount = 0;
        double LastDeltaTime = 0.0;
        double LastTotalTime = 0.0;

    protected:
        void OnUpdate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext&,
            IN const double& nDeltaTime_,
            IN const double& nTotalTime_) override
        {
            ++UpdateCount;
            LastDeltaTime = nDeltaTime_;
            LastTotalTime = nTotalTime_;
        }
    };

    class CProjectLifecycleBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetBehaviourClass() const override
        {
            return "CProjectLifecycleBehaviour";
        }

        std::string GetComponentClass() const override
        {
            return S_LifecycleComponentClass;
        }

        int AwakeCount = 0;
        int ModifyingCount = 0;
        int ModifiedCount = 0;
        int DisableCount = 0;
        int DestroyImmediateCount = 0;
        int DestroyCount = 0;
        int LastModifiedValue = 0;
        int LastDestroyValue = 0;

    protected:
        void OnAwake(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext&) override
        {
            ++AwakeCount;
        }

        void OnModifying(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Data::PropertySet&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext&) override
        {
            ++ModifyingCount;
        }

        void OnModified(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Data::PropertySet& NewValues_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext&) override
        {
            ++ModifiedCount;
            LastModifiedValue = NewValues_.at(S_ValueProperty).To<int>();
        }

        void OnDisable(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext&) override
        {
            ++DisableCount;
        }

        void OnDestroyImmediate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext&) override
        {
            ++DestroyImmediateCount;
        }

        void OnDestroy(
            IN const iCAX::Behaviour::CComponentDestroyInfo& DestroyInfo_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext&) override
        {
            ++DestroyCount;
            LastDestroyValue = DestroyInfo_.PreviousProperties.at(S_ValueProperty).To<int>();
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
            m_Definition.ProductID = "project-test-product";
            m_Definition.ProductName = "Project Test Product";
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

    std::shared_ptr<iCAX::Product::IProductContext> MakeProductContext(
        IN const std::shared_ptr<iCAX::Database::IMetaRegistry>& pMetaRegistry_,
        IN const std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry>& pBehaviourRegistry_,
        IN const std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry>& pResourceLoaderRegistry_,
        IN const std::shared_ptr<iCAX::Services::CServiceProvider>& pServiceProvider_)
    {
        return std::make_shared<CTestProductContext>(
            pMetaRegistry_,
            pBehaviourRegistry_,
            pResourceLoaderRegistry_,
            pServiceProvider_,
            std::make_shared<iCAX::Command::CCommandRegistry>());
    }

    bool WaitFor(std::condition_variable& Condition_, std::mutex& Mutex_, const std::function<bool()>& Predicate_)
    {
        std::unique_lock<std::mutex> _Lock(Mutex_);
        return Condition_.wait_for(_Lock, std::chrono::seconds(2), Predicate_);
    }

    std::shared_ptr<iCAX::Mail::CMailChannelRegistry> MakeMailChannelRegistry()
    {
        return std::make_shared<iCAX::Mail::CMailChannelRegistry>();
    }

    std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> MakeResourceLoaderRegistry()
    {
        return std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
    }

    CProjectCatalogCreateInfo MakeCatalogInfo()
    {
        CProjectCatalogCreateInfo _Info;
        _Info.pMetaRegistry = CreateProjectTestMetaRegistry();
        _Info.pBehaviourRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
        _Info.pMailChannelRegistry = MakeMailChannelRegistry();
        _Info.pApplicationContext = std::make_shared<iCAX::Application::CApplicationContext>();
        _Info.pServiceProvider = std::make_shared<iCAX::Services::CServiceProvider>();
        _Info.pProductContext = MakeProductContext(
            _Info.pMetaRegistry,
            _Info.pBehaviourRegistry,
            MakeResourceLoaderRegistry(),
            _Info.pServiceProvider);
        _Info.ResourceLoaderRegistryFactory = []() {
            return MakeResourceLoaderRegistry();
        };
        return _Info;
    }

    CProjectCreateInfo MakeProjectInfo()
    {
        CProjectCreateInfo _Info;
        _Info.pMetaRegistry = CreateProjectTestMetaRegistry();
        _Info.pBehaviourRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
        _Info.pResourceLoaderRegistry = MakeResourceLoaderRegistry();
        _Info.pMailChannelRegistry = MakeMailChannelRegistry();
        _Info.pApplicationContext = std::make_shared<iCAX::Application::CApplicationContext>();
        _Info.pServiceProvider = std::make_shared<iCAX::Services::CServiceProvider>();
        _Info.pProductContext = MakeProductContext(
            _Info.pMetaRegistry,
            _Info.pBehaviourRegistry,
            _Info.pResourceLoaderRegistry,
            _Info.pServiceProvider);
        return _Info;
    }

    iCAX::PDO::PDODecl MakeProjectPDODecl(
        IN const iCAX::PDO::PDOID& ID_,
        IN iCAX::PDO::PDODirection Direction_)
    {
        return iCAX::PDO::PDODecl{
            1,
            ID_,
            Direction_,
            static_cast<int>(sizeof(ProjectPDOPayload))
        };
    }

    void WriteProjectPDOPayload(IN iCAX::PDO::IPDOSlot& Slot_, IN int nValue_)
    {
        ProjectPDOPayload _Payload{ nValue_ };
        iCAX::PDO::CPDOWriteLease _WriteLease(Slot_, static_cast<uint64_t>(nValue_));
        std::memcpy(_WriteLease.Data(), &_Payload, sizeof(_Payload));
        _WriteLease.Commit();
    }

    int ReadProjectPDOPayload(IN iCAX::PDO::IPDOSlot& Slot_)
    {
        ProjectPDOPayload _Payload{};
        iCAX::PDO::CPDOReadLease _ReadLease(Slot_);
        std::memcpy(&_Payload, _ReadLease.Data(), sizeof(_Payload));
        return _Payload.Value;
    }

    std::filesystem::path MakeTempProjectPath()
    {
        auto _Path = std::filesystem::temp_directory_path()
            / ("icax-project-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()) + ".icax");
        std::ofstream(_Path.string(), std::ios::binary).close();
        return _Path;
    }

    void RemoveProjectFiles(IN const std::filesystem::path& ProjectPath_)
    {
        std::error_code _Error;
        std::filesystem::remove(ProjectPath_, _Error);
        std::filesystem::remove(ProjectPath_.string() + ".log", _Error);
    }
}

TEST(ProjectCatalogTest, AllowsOneMainProjectAndChildScenes)
{
    CProjectCatalog _Catalog(MakeCatalogInfo());
    EXPECT_FALSE(_Catalog.GetCatalogID().is_nil());
    EXPECT_EQ("Project Catalog", _Catalog.GetCatalogName());

    auto _pProject = _Catalog.OpenMainProject("Robot Cell");
    auto _pChildScene = _pProject->OpenChildScene(_pProject->GetMainSceneID(), CProjectSceneCreateInfo{});

    ASSERT_NE(nullptr, _pProject);
    ASSERT_NE(nullptr, _pChildScene);
    EXPECT_EQ(1u, _Catalog.Count());
    EXPECT_EQ(_pProject, _Catalog.GetMainProject());
    EXPECT_EQ(2u, _pProject->GetScenes().size());
    EXPECT_TRUE(_pProject->GetMainScene().IsMainScene());
    EXPECT_TRUE(_pChildScene->IsTransientScene());
    EXPECT_EQ(_pProject->GetMainSceneID(), _pChildScene->GetParentSceneID());

    EXPECT_THROW(_Catalog.OpenMainProject("Another Main"), std::logic_error);
    EXPECT_NE(_pProject->MainSceneDatabase().GetID(), _pChildScene->Database().GetID());
    EXPECT_EQ(_pProject->GetProjectID(), _pProject->MainSceneDatabase().GetID());
    EXPECT_EQ(_pChildScene->GetSceneID(), _pChildScene->Database().GetID());
}

TEST(ProjectCatalogTest, CatalogIdentityCanBeProvided)
{
    auto _Info = MakeCatalogInfo();
    _Info.CatalogID = iCAX::Data::GenerateNewUUID();
    _Info.CatalogName = "Catalog A";
    _Info.CatalogPath = "memory://catalog-a";

    CProjectCatalog _Catalog(_Info);
    EXPECT_EQ(_Info.CatalogID, _Catalog.GetCatalogID());
    EXPECT_EQ("Catalog A", _Catalog.GetCatalogName());
    EXPECT_EQ("memory://catalog-a", _Catalog.GetCatalogPath());

    _Catalog.SetCatalogName("");
    _Catalog.SetCatalogPath("memory://catalog-b");
    EXPECT_EQ("Project Catalog", _Catalog.GetCatalogName());
    EXPECT_EQ("memory://catalog-b", _Catalog.GetCatalogPath());
}

TEST(ProjectTest, SettingsAreInitializedFromCreateInfo)
{
    auto _Info = MakeProjectInfo();
    _Info.Settings.Set("unit.length", std::string("Millimeter"));
    _Info.Settings.Set("unit.angle", std::string("Degree"));
    _Info.Settings.Set("geometry.defaultTolerance", 0.001);
    _Info.Settings.Set("coordinate.project", std::string("LaserPartCS"));
    _Info.Settings.Set("defaultLayer", "laser3dcam.toolpath", std::string("outer-cut"));

    CProject _Project(_Info);

    EXPECT_EQ("Millimeter", _Project.Settings().Get("unit.length").To<std::string>());
    EXPECT_EQ("Degree", _Project.Settings().Get("unit.angle").To<std::string>());
    EXPECT_DOUBLE_EQ(0.001, _Project.Settings().Get("geometry.defaultTolerance").To<double>());
    EXPECT_EQ("LaserPartCS", _Project.Settings().Get("coordinate.project").To<std::string>());
    EXPECT_EQ(
        "outer-cut",
        _Project.Settings()
            .Get("defaultLayer", "laser3dcam.toolpath", iCAX::Data::Variant())
            .To<std::string>());
    EXPECT_EQ(
        "fallback",
        _Project.Settings()
            .Get("missing", iCAX::Data::Variant(std::string("fallback")))
            .To<std::string>());
}

TEST(ProjectCatalogTest, SceneResourcesAreIsolated)
{
    CProjectCatalog _Catalog(MakeCatalogInfo());

    auto _pProject = _Catalog.OpenMainProject("Weld A");
    auto _pChildScene = _pProject->OpenChildScene(_pProject->GetMainSceneID(), CProjectSceneCreateInfo{});

    auto _pTextA = std::make_shared<TextResource>();
    _pTextA->Text = "A";
    auto _pTextB = std::make_shared<TextResource>();
    _pTextB->Text = "B";

    _pProject->MainSceneResources().Set<TextResource>("memory://shared-id", _pTextA);
    _pChildScene->Resources().Set<TextResource>("memory://shared-id", _pTextB);

    ASSERT_NE(nullptr, _pProject->MainSceneResources().Get<TextResource>("memory://shared-id"));
    ASSERT_NE(nullptr, _pChildScene->Resources().Get<TextResource>("memory://shared-id"));
    EXPECT_EQ("A", _pProject->MainSceneResources().Get<TextResource>("memory://shared-id")->Text);
    EXPECT_EQ("B", _pChildScene->Resources().Get<TextResource>("memory://shared-id")->Text);
    EXPECT_NE(
        _pProject->MainSceneResources().Get<TextResource>("memory://shared-id").get(),
        _pChildScene->Resources().Get<TextResource>("memory://shared-id").get());
}

TEST(ProjectCatalogTest, ProjectUsesInjectedMetaRegistry)
{
    auto _pMeta = CreateProjectTestMetaRegistry();

    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Meta Isolated";
    _Info.pMetaRegistry = _pMeta;

    CProject _Project(_Info);

    EXPECT_EQ(_pMeta, _Project.MainSceneDatabase().GetMetaRegistry());
}

TEST(ProjectCatalogTest, MainSceneOwnsAndSwapsPDOHub)
{
    const auto _InboundID = iCAX::PDO::MakePDOID("ProjectTest.PDO", "Inbound");
    const auto _OutboundID = iCAX::PDO::MakePDOID("ProjectTest.PDO", "Outbound");

    auto _Info = MakeProjectInfo();
    _Info.bEnablePDOHub = true;
    _Info.PDOHubCreateInfo.InitialDeclarations = {
        MakeProjectPDODecl(_InboundID, iCAX::PDO::kDirection2Inner),
        MakeProjectPDODecl(_OutboundID, iCAX::PDO::kDirection2External)
    };

    CProject _Project(_Info);

    ASSERT_TRUE(_Project.MainSceneHasPDOHub());
    auto& _Hub = _Project.MainScenePDOHub();
    const auto _ArenaName = _Hub.GetSharedArenaName();
    ASSERT_FALSE(_ArenaName.empty());
    EXPECT_GT(_Hub.GetSharedArenaSize(), sizeof(iCAX::PDO::SharedPDOArenaHeader));

    auto _ExternalArena = iCAX::PDO::CSharedPDOArena::Open(_ArenaName);
    auto _ExternalInbound = _ExternalArena->GetSlot(_InboundID);
    WriteProjectPDOPayload(_ExternalInbound, 11);

    _Project.PreSwapMainScenePDO();
    EXPECT_EQ(11, ReadProjectPDOPayload(_Hub.GetSlot(_InboundID)));

    WriteProjectPDOPayload(_Hub.GetSlot(_OutboundID), 22);
    _Project.PostSwapMainScenePDO();
    auto _ExternalOutbound = _ExternalArena->GetSlot(_OutboundID);
    EXPECT_EQ(22, ReadProjectPDOPayload(_ExternalOutbound));

    _ExternalArena.reset();
    _Project.Close();

    EXPECT_FALSE(_Project.MainSceneHasPDOHub());
    EXPECT_THROW((void)_Project.MainScenePDOHub(), std::logic_error);
    EXPECT_THROW((void)iCAX::PDO::CSharedPDOArena::Open(_ArenaName), std::runtime_error);
}

TEST(ProjectCatalogTest, MainSceneWithoutPDOHubUsesDefaultContextBehavior)
{
    CProject _Project(MakeProjectInfo());

    EXPECT_FALSE(_Project.MainSceneHasPDOHub());
    EXPECT_THROW((void)_Project.MainScenePDOHub(), std::logic_error);
}

TEST(ProjectCatalogTest, ProjectCreationRequiresExplicitRegistries)
{
    auto _Info = MakeProjectInfo();
    _Info.pMetaRegistry.reset();
    EXPECT_THROW({
        CProject _Project(_Info);
    }, std::invalid_argument);

    _Info = MakeProjectInfo();
    _Info.pBehaviourRegistry.reset();
    EXPECT_THROW({
        CProject _Project(_Info);
    }, std::invalid_argument);

    _Info = MakeProjectInfo();
    _Info.pResourceLoaderRegistry.reset();
    EXPECT_THROW({
        CProject _Project(_Info);
    }, std::invalid_argument);

    _Info = MakeProjectInfo();
    _Info.pApplicationContext.reset();
    EXPECT_THROW({
        CProject _Project(_Info);
    }, std::invalid_argument);

    _Info = MakeProjectInfo();
    _Info.pProductContext.reset();
    EXPECT_THROW({
        CProject _Project(_Info);
    }, std::invalid_argument);

    _Info = MakeProjectInfo();
    _Info.pServiceProvider.reset();
    EXPECT_THROW({
        CProject _Project(_Info);
    }, std::invalid_argument);
}

TEST(ProjectCatalogTest, CatalogCreationRequiresExplicitDependencies)
{
    auto _Info = MakeCatalogInfo();
    _Info.pMetaRegistry.reset();
    EXPECT_THROW({ CProjectCatalog _Catalog(_Info); }, std::invalid_argument);

    _Info = MakeCatalogInfo();
    _Info.pBehaviourRegistry.reset();
    EXPECT_THROW({ CProjectCatalog _Catalog(_Info); }, std::invalid_argument);

    _Info = MakeCatalogInfo();
    _Info.ResourceLoaderRegistryFactory = nullptr;
    EXPECT_THROW({ CProjectCatalog _Catalog(_Info); }, std::invalid_argument);

    _Info = MakeCatalogInfo();
    _Info.pApplicationContext.reset();
    EXPECT_THROW({ CProjectCatalog _Catalog(_Info); }, std::invalid_argument);

    _Info = MakeCatalogInfo();
    _Info.pProductContext.reset();
    EXPECT_THROW({ CProjectCatalog _Catalog(_Info); }, std::invalid_argument);

    _Info = MakeCatalogInfo();
    _Info.pServiceProvider.reset();
    EXPECT_THROW({ CProjectCatalog _Catalog(_Info); }, std::invalid_argument);

    _Info = MakeCatalogInfo();
    _Info.pMailChannelRegistry.reset();
    EXPECT_THROW({ CProjectCatalog _Catalog(_Info); }, std::invalid_argument);
}

TEST(ProjectCatalogTest, ProjectResourceLoadersAreIsolated)
{
    auto _pRegistryA = std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
    auto _pRegistryB = std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
    _pRegistryA->RegisterLoader(typeid(TextResource), std::make_shared<FixedTextLoader>("A"));
    _pRegistryB->RegisterLoader(typeid(TextResource), std::make_shared<FixedTextLoader>("B"));

    auto _InfoA = MakeProjectInfo();
    _InfoA.ProjectName = "Resource A";
    _InfoA.pResourceLoaderRegistry = _pRegistryA;

    auto _InfoB = MakeProjectInfo();
    _InfoB.ProjectName = "Resource B";
    _InfoB.pResourceLoaderRegistry = _pRegistryB;

    CProject _ProjectA(_InfoA);
    CProject _ProjectB(_InfoB);

    auto _pTextA = _ProjectA.MainSceneResources().Load<TextResource>("memory://same-resource");
    auto _pTextB = _ProjectB.MainSceneResources().Load<TextResource>("memory://same-resource");

    ASSERT_NE(nullptr, _pTextA);
    ASSERT_NE(nullptr, _pTextB);
    EXPECT_EQ("A", _pTextA->Text);
    EXPECT_EQ("B", _pTextB->Text);
}

TEST(ProjectCatalogTest, CloseChildSceneKeepsMainSceneOpen)
{
    CProjectCatalog _Catalog(MakeCatalogInfo());

    auto _pProject = _Catalog.OpenMainProject("A");
    auto _pChildScene = _pProject->OpenChildScene(_pProject->GetMainSceneID(), CProjectSceneCreateInfo{});
    const auto _ProjectID = _pProject->GetProjectID();
    const auto _ChildSceneID = _pChildScene->GetSceneID();

    EXPECT_TRUE(_pProject->CloseScene(_ChildSceneID));
    EXPECT_EQ(nullptr, _pProject->GetScene(_ChildSceneID));
    ASSERT_NE(nullptr, _Catalog.FindProject(_ProjectID));
    EXPECT_TRUE(_Catalog.FindProject(_ProjectID)->IsOpen());
    EXPECT_TRUE(_Catalog.FindProject(_ProjectID)->GetMainScene().IsOpen());
    EXPECT_EQ(_pProject, _Catalog.GetMainProject());
    EXPECT_EQ(1u, _Catalog.Count());
}

TEST(ProjectTest, StartRunsSceneFrameHandlerOnSceneThread)
{
    std::mutex _Mutex;
    std::condition_variable _Condition;
    int _FrameCount = 0;
    bool _bPostOfficeValid = false;
    std::thread::id _HandlerThreadID;

    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Threaded";
    _Info.nFrameIntervalMilliseconds = 1;
    _Info.OnSceneFrame = [&](
        CProjectScene&,
        const iCAX::Mail::CMailPostOffice& PostOffice_) {
        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _HandlerThreadID = std::this_thread::get_id();
            _bPostOfficeValid = PostOffice_.IsValid();
            ++_FrameCount;
        }
        _Condition.notify_all();
    };

    CProject _Project(_Info);
    _Project.Start();

    EXPECT_TRUE(WaitFor(_Condition, _Mutex, [&]() { return _FrameCount >= 2; }));
    _Project.Stop();

    EXPECT_FALSE(_Project.IsRunning());
    EXPECT_TRUE(_bPostOfficeValid);
    EXPECT_NE(std::thread::id{}, _HandlerThreadID);
    EXPECT_NE(std::this_thread::get_id(), _HandlerThreadID);
}

TEST(ProjectTest, ChildSceneUsesProjectFrameHandlerWithOwnSceneContext)
{
    std::mutex _Mutex;
    std::condition_variable _Condition;
    iCAX::Data::uuid _ObservedSceneID;

    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Child Scene Handler";
    _Info.OnSceneFrame = [&](
        CProjectScene& Scene_,
        const iCAX::Mail::CMailPostOffice& PostOffice_) {
        if (!Scene_.IsTransientScene() || !PostOffice_.IsValid())
        {
            return;
        }

        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _ObservedSceneID = Scene_.GetSceneID();
        }
        _Condition.notify_all();
    };

    CProject _Project(_Info);
    auto _pChildScene = _Project.OpenChildScene(_Project.GetMainSceneID(), CProjectSceneCreateInfo{});
    const auto _ChildSceneID = _pChildScene->GetSceneID();

    _pChildScene->Start();
    EXPECT_TRUE(WaitFor(_Condition, _Mutex, [&]() { return _ObservedSceneID == _ChildSceneID; }));
    _pChildScene->Stop();
}

TEST(ProjectTest, TickUsesSceneRuntimeSchedulerFrameTime)
{
    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Frame Time";
    _Info.StartupComponent = S_FrameTimeComponentClass;
    _Info.pBehaviourRegistry->RegisterBehaviour<CFrameTimeBehaviour>();

    CProject _Project(_Info);
    _Project.BindStartup();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    _Project.TickMainScene();

    auto _Behaviours = _Project.MainSceneUniverse().GetAllBehaviours();
    ASSERT_EQ(1u, _Behaviours.size());
    auto _pBehaviour = std::dynamic_pointer_cast<CFrameTimeBehaviour>(_Behaviours.front());
    ASSERT_NE(nullptr, _pBehaviour);
    EXPECT_EQ(1, _pBehaviour->UpdateCount);
    EXPECT_GT(_pBehaviour->LastDeltaTime, 0.0);
    EXPECT_GE(_pBehaviour->LastTotalTime, _pBehaviour->LastDeltaTime);
}

TEST(ProjectTest, ForwardsRepositoryLifecycleEventsToUniverse)
{
    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Lifecycle Forwarding";
    _Info.pBehaviourRegistry->RegisterBehaviour<CProjectLifecycleBehaviour>();

    CProject _Project(_Info);
    ASSERT_TRUE(_Project.MainSceneUniverse().BindBehaviour<CProjectLifecycleBehaviour>());

    auto _Behaviours = _Project.MainSceneUniverse().GetAllBehaviours();
    ASSERT_EQ(1u, _Behaviours.size());
    auto _pBehaviour = std::dynamic_pointer_cast<CProjectLifecycleBehaviour>(_Behaviours.front());
    ASSERT_NE(nullptr, _pBehaviour);

    auto _pEntity = _Project.MainSceneDatabase().CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_LifecycleComponentClass);

    EXPECT_EQ(1, _pBehaviour->AwakeCount);

    std::string _strError;
    ASSERT_TRUE(_pComponent->SetProperty(S_ValueProperty, iCAX::Data::PropertyValue(42), _strError)) << _strError;
    EXPECT_EQ(1, _pBehaviour->ModifyingCount);
    EXPECT_EQ(1, _pBehaviour->ModifiedCount);
    EXPECT_EQ(42, _pBehaviour->LastModifiedValue);

    _pComponent->Disable();
    EXPECT_EQ(1, _pBehaviour->DisableCount);

    ASSERT_TRUE(_pEntity->RemoveComponent(S_LifecycleComponentClass, _strError)) << _strError;
    EXPECT_EQ(1, _pBehaviour->DestroyImmediateCount);
    EXPECT_EQ(0, _pBehaviour->DestroyCount);

    _Project.TickMainScene();

    EXPECT_EQ(1, _pBehaviour->DestroyCount);
    EXPECT_EQ(42, _pBehaviour->LastDestroyValue);
}

TEST(ProjectTest, ProjectsRunOnIndependentThreads)
{
    std::mutex _Mutex;
    std::condition_variable _Condition;
    std::atomic<int> _SlowFrames = 0;
    std::atomic<int> _FastFrames = 0;
    std::thread::id _SlowThreadID;
    std::thread::id _FastThreadID;

    auto _SlowInfo = MakeProjectInfo();
    _SlowInfo.ProjectName = "Slow";
    _SlowInfo.nFrameIntervalMilliseconds = 1;
    _SlowInfo.OnSceneFrame = [&](
        CProjectScene&,
        const iCAX::Mail::CMailPostOffice&) {
        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _SlowThreadID = std::this_thread::get_id();
            ++_SlowFrames;
        }
        _Condition.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    };

    auto _FastInfo = MakeProjectInfo();
    _FastInfo.ProjectName = "Fast";
    _FastInfo.nFrameIntervalMilliseconds = 1;
    _FastInfo.OnSceneFrame = [&](
        CProjectScene&,
        const iCAX::Mail::CMailPostOffice&) {
        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _FastThreadID = std::this_thread::get_id();
            ++_FastFrames;
        }
        _Condition.notify_all();
    };

    CProject _SlowProject(_SlowInfo);
    CProject _FastProject(_FastInfo);
    _SlowProject.Start();
    _FastProject.Start();

    EXPECT_TRUE(WaitFor(_Condition, _Mutex, [&]() {
        return _SlowFrames.load(std::memory_order_acquire) >= 1
            && _FastFrames.load(std::memory_order_acquire) >= 5;
    }));

    _SlowProject.Stop();
    _FastProject.Stop();

    EXPECT_GE(_SlowFrames.load(std::memory_order_acquire), 1);
    EXPECT_GE(_FastFrames.load(std::memory_order_acquire), 5);
    EXPECT_NE(std::thread::id{}, _SlowThreadID);
    EXPECT_NE(std::thread::id{}, _FastThreadID);
    EXPECT_NE(_SlowThreadID, _FastThreadID);
}

TEST(ProjectTest, FaultedProjectDoesNotStopOtherProjects)
{
    std::mutex _Mutex;
    std::condition_variable _Condition;
    bool _bFaultProjectEnteredFrame = false;
    std::atomic<int> _HealthyFrames = 0;

    auto _FaultInfo = MakeProjectInfo();
    _FaultInfo.ProjectName = "Fault";
    _FaultInfo.nFrameIntervalMilliseconds = 1;
    _FaultInfo.OnSceneFrame = [&](
        CProjectScene&,
        const iCAX::Mail::CMailPostOffice&) {
        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _bFaultProjectEnteredFrame = true;
        }
        _Condition.notify_all();
        throw std::runtime_error("project local failure");
    };

    auto _HealthyInfo = MakeProjectInfo();
    _HealthyInfo.ProjectName = "Healthy";
    _HealthyInfo.nFrameIntervalMilliseconds = 1;
    _HealthyInfo.OnSceneFrame = [&](
        CProjectScene&,
        const iCAX::Mail::CMailPostOffice&) {
        ++_HealthyFrames;
        _Condition.notify_all();
    };

    CProject _FaultProject(_FaultInfo);
    CProject _HealthyProject(_HealthyInfo);
    _FaultProject.Start();
    _HealthyProject.Start();

    EXPECT_TRUE(WaitFor(_Condition, _Mutex, [&]() {
        return _bFaultProjectEnteredFrame
            && _FaultProject.GetState() == EProjectState::Faulted
            && _HealthyFrames.load(std::memory_order_acquire) >= 5;
    }));

    EXPECT_EQ(EProjectState::Faulted, _FaultProject.GetState());
    ASSERT_TRUE(_FaultProject.GetLastFault().has_value());
    EXPECT_EQ("project local failure", _FaultProject.GetLastFault()->Message);
    EXPECT_TRUE(_HealthyProject.IsRunning());
    EXPECT_GE(_HealthyFrames.load(std::memory_order_acquire), 5);

    _HealthyProject.Stop();
}

TEST(ProjectTest, CloseInvalidatesMainSceneMailPostOffices)
{
    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Mail";

    CProject _Project(_Info);
    auto _FrontendPostOffice = _Project.GetMainSceneFrontendPostOffice();
    auto _BackendPostOffice = _Project.GetMainSceneBackendPostOffice();
    ASSERT_TRUE(_FrontendPostOffice.IsValid());
    ASSERT_TRUE(_BackendPostOffice.IsValid());

    _Project.Close();

    EXPECT_FALSE(_FrontendPostOffice.IsValid());
    EXPECT_FALSE(_BackendPostOffice.IsValid());
    EXPECT_THROW(_FrontendPostOffice.Send(iCAX::Mail::Mail{}), std::logic_error);
    EXPECT_THROW(_BackendPostOffice.Receive(), std::logic_error);
    EXPECT_THROW(_Project.GetMainSceneFrontendPostOffice(), std::logic_error);
}

TEST(ProjectTest, MainSceneCanSendFrontendEvent)
{
    constexpr uint64_t kRepositoryChangedEvent = iCAX::Command::MakeCommandCode("Project", "RepositoryChanged");

    CProject _Project(MakeProjectInfo());
    auto _FrontendPostOffice = _Project.GetMainSceneFrontendPostOffice();

    _Project.SendMainSceneFrontendEvent(kRepositoryChangedEvent, "project-event");

    auto _Events = _FrontendPostOffice.Receive();
    ASSERT_EQ(1u, _Events.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _Events[0].Header.nStamp);
    EXPECT_EQ(0u, _Events[0].Header.nOriginId);
    EXPECT_EQ(kRepositoryChangedEvent, _Events[0].Header.nTypeCode);
    EXPECT_EQ("project-event", iCAX::Mail::GetMailPayloadText(_Events[0]));

    iCAX::Mail::ReleaseMailPayload(_Events[0]);
}

TEST(ProjectQuickSaveTest, ReplaysExistingLogAndContinuesAppending)
{
    auto _ProjectPath = MakeTempProjectPath();
    const auto _EntityID = iCAX::Data::GenerateNewUUID();

    auto _Info = MakeProjectInfo();
    _Info.ProjectPath = _ProjectPath.string();

    {
        CProject _Project(_Info);
        EXPECT_EQ(_ProjectPath.string() + ".log", _Project.GetQuickSaveLogPath());

        _Project.OpenQuickSaveLog(true, S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);
        auto _pEntity = _Project.MainSceneDatabase().CreateEntity(_EntityID);
        auto _pComponent = _pEntity->AddComponent(S_QuickSaveComponentClass);
        ASSERT_TRUE(_pComponent->SetProperty("Value", iCAX::Data::PropertyValue(7)));
        _Project.CloseQuickSaveLog();
    }

    {
        CProject _Project(_Info);
        _Project.ReplayQuickSaveLog(S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);

        auto _pEntity = _Project.MainSceneDatabase().GetEntity(_EntityID);
        ASSERT_NE(nullptr, _pEntity);
        auto _pComponent = _pEntity->GetComponent(S_QuickSaveComponentClass);
        ASSERT_NE(nullptr, _pComponent);
        EXPECT_EQ(7, _pComponent->GetProperty("Value").To<int>());

        _Project.OpenQuickSaveLog(false, S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);
        ASSERT_TRUE(_pComponent->SetProperty("Value", iCAX::Data::PropertyValue(9)));
        _Project.CloseQuickSaveLog();
    }

    {
        CProject _Project(_Info);
        _Project.ReplayQuickSaveLog(S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);

        auto _pEntity = _Project.MainSceneDatabase().GetEntity(_EntityID);
        ASSERT_NE(nullptr, _pEntity);
        auto _pComponent = _pEntity->GetComponent(S_QuickSaveComponentClass);
        ASSERT_NE(nullptr, _pComponent);
        EXPECT_EQ(9, _pComponent->GetProperty("Value").To<int>());
    }

    RemoveProjectFiles(_ProjectPath);
}

TEST(ProjectQuickSaveTest, MarkProjectFileSavedTruncatesAndReopensLog)
{
    auto _ProjectPath = MakeTempProjectPath();

    auto _Info = MakeProjectInfo();
    _Info.ProjectPath = _ProjectPath.string();

    CProject _Project(_Info);
    _Project.OpenQuickSaveLog(true, S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);
    auto _pEntity = _Project.MainSceneDatabase().CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pComponent = _pEntity->AddComponent(S_QuickSaveComponentClass);
    ASSERT_TRUE(_pComponent->SetProperty("Value", iCAX::Data::PropertyValue(1)));
    ASSERT_GT(std::filesystem::file_size(_Project.GetQuickSaveLogPath()), 0u);

    _Project.MarkProjectFileSaved(std::string(), S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);

    EXPECT_TRUE(_Project.IsQuickSaveLogOpen());
    const auto _HeaderOnlyLogSize = std::filesystem::file_size(_Project.GetQuickSaveLogPath());
    EXPECT_GT(_HeaderOnlyLogSize, 0u);

    ASSERT_TRUE(_pComponent->SetProperty("Value", iCAX::Data::PropertyValue(2)));
    _Project.CloseQuickSaveLog();
    EXPECT_GT(std::filesystem::file_size(_Project.GetQuickSaveLogPath()), _HeaderOnlyLogSize);

    RemoveProjectFiles(_ProjectPath);
}

TEST(ProjectQuickSaveTest, CloseDoesNotAppendRepositoryCleanup)
{
    auto _ProjectPath = MakeTempProjectPath();
    const auto _EntityID = iCAX::Data::GenerateNewUUID();

    auto _Info = MakeProjectInfo();
    _Info.ProjectPath = _ProjectPath.string();

    {
        CProject _Project(_Info);
        _Project.OpenQuickSaveLog(true, S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);
        auto _pEntity = _Project.MainSceneDatabase().CreateEntity(_EntityID);
        auto _pComponent = _pEntity->AddComponent(S_QuickSaveComponentClass);
        ASSERT_TRUE(_pComponent->SetProperty("Value", iCAX::Data::PropertyValue(5)));
        _Project.Close();
    }

    {
        CProject _Project(_Info);
        _Project.ReplayQuickSaveLog(S_TestQuickSaveLogMagic, S_TestQuickSaveLogVersion);
        EXPECT_NE(nullptr, _Project.MainSceneDatabase().GetEntity(_EntityID));
    }

    RemoveProjectFiles(_ProjectPath);
}

TEST(ProjectRuntimeTest, CloseReleasesLocalProjectReference)
{
    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Runtime Local Project";
    auto _pProject = std::make_shared<CProject>(_Info);
    auto _pRuntime = CreateLocalProjectRuntime(_pProject);

    ASSERT_NE(nullptr, _pRuntime->GetLocalProject());
    EXPECT_TRUE(_pRuntime->IsOpen());

    _pRuntime->Close();

    EXPECT_EQ(nullptr, _pRuntime->GetLocalProject());
    EXPECT_FALSE(_pRuntime->IsOpen());
    EXPECT_FALSE(_pProject->IsOpen());
    EXPECT_THROW((void)_pRuntime->GetProjectName(), std::logic_error);
}
