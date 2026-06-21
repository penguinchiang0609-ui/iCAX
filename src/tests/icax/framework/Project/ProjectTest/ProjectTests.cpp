#include <gtest/gtest.h>

#include <Project/ProjectCatalog.h>
#include <Project/Project.h>
#include <Project/ProjectRuntime.h>
#include <Behaviour/IBehaviourRegistry.h>
#include <Database/IMetaRegistry.h>
#include <Resources/IResourceLoader.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <Services/MailChannelService.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
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
    struct TextResource
    {
        std::string Text;
    };

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

    bool WaitFor(std::condition_variable& Condition_, std::mutex& Mutex_, const std::function<bool()>& Predicate_)
    {
        std::unique_lock<std::mutex> _Lock(Mutex_);
        return Condition_.wait_for(_Lock, std::chrono::seconds(2), Predicate_);
    }

    std::shared_ptr<iCAX::Services::IMailChannelService> MakeMailChannelService()
    {
        auto _pService = std::make_shared<iCAX::Services::CMailChannelService>();
        _pService->OnLoad();
        return _pService;
    }

    std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> MakeResourceLoaderRegistry()
    {
        return std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
    }

    CProjectCatalogCreateInfo MakeCatalogInfo()
    {
        CProjectCatalogCreateInfo _Info;
        _Info.pMetaRegistry = iCAX::Database::CreateMetaRegistry();
        _Info.pBehaviourRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
        _Info.pMailChannelService = MakeMailChannelService();
        _Info.ResourceLoaderRegistryFactory = []() {
            return MakeResourceLoaderRegistry();
        };
        return _Info;
    }

    CProjectCreateInfo MakeProjectInfo()
    {
        CProjectCreateInfo _Info;
        _Info.pMetaRegistry = iCAX::Database::CreateMetaRegistry();
        _Info.pBehaviourRegistry = iCAX::Behaviour::CreateBehaviourRegistry();
        _Info.pResourceLoaderRegistry = MakeResourceLoaderRegistry();
        _Info.pMailChannelService = MakeMailChannelService();
        return _Info;
    }
}

TEST(ProjectCatalogTest, AllowsOneMainProjectAndTransientProjects)
{
    CProjectCatalog _Catalog(MakeCatalogInfo());
    EXPECT_FALSE(_Catalog.GetCatalogID().is_nil());
    EXPECT_EQ("Project Catalog", _Catalog.GetCatalogName());

    auto _pMain = _Catalog.OpenMainProject("Robot Cell");
    auto _pTransient = _Catalog.OpenTransientProject("Sheet Nesting Preview");

    ASSERT_NE(nullptr, _pMain);
    ASSERT_NE(nullptr, _pTransient);
    EXPECT_TRUE(_pMain->IsMainProject());
    EXPECT_TRUE(_pTransient->IsTransientProject());
    EXPECT_NE(_pMain->GetProjectID(), _pTransient->GetProjectID());
    EXPECT_EQ(2u, _Catalog.Count());
    EXPECT_EQ(1u, _Catalog.TransientCount());
    EXPECT_EQ(_pMain, _Catalog.GetMainProject());

    EXPECT_THROW(_Catalog.OpenMainProject("Another Main"), std::logic_error);
    EXPECT_NE(_pMain->Database().GetID(), _pTransient->Database().GetID());
    EXPECT_EQ(_pMain->GetProjectID(), _pMain->Database().GetID());
    EXPECT_EQ(_pTransient->GetProjectID(), _pTransient->Database().GetID());
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

TEST(ProjectCatalogTest, ProjectResourcesAreIsolated)
{
    CProjectCatalog _Catalog(MakeCatalogInfo());

    auto _pProjectA = _Catalog.OpenMainProject("Weld A");
    auto _pProjectB = _Catalog.OpenTransientProject("Weld B Preview");

    auto _pTextA = std::make_shared<TextResource>();
    _pTextA->Text = "A";
    auto _pTextB = std::make_shared<TextResource>();
    _pTextB->Text = "B";

    _pProjectA->Resources().Set<TextResource>("memory://shared-id", _pTextA);
    _pProjectB->Resources().Set<TextResource>("memory://shared-id", _pTextB);

    ASSERT_NE(nullptr, _pProjectA->Resources().Get<TextResource>("memory://shared-id"));
    ASSERT_NE(nullptr, _pProjectB->Resources().Get<TextResource>("memory://shared-id"));
    EXPECT_EQ("A", _pProjectA->Resources().Get<TextResource>("memory://shared-id")->Text);
    EXPECT_EQ("B", _pProjectB->Resources().Get<TextResource>("memory://shared-id")->Text);
    EXPECT_NE(
        _pProjectA->Resources().Get<TextResource>("memory://shared-id").get(),
        _pProjectB->Resources().Get<TextResource>("memory://shared-id").get());
}

TEST(ProjectCatalogTest, ProjectUsesInjectedMetaRegistry)
{
    auto _pMeta = iCAX::Database::CreateMetaRegistry();

    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Meta Isolated";
    _Info.pMetaRegistry = _pMeta;

    CProject _Project(_Info);

    EXPECT_EQ(_pMeta, _Project.Database().GetMetaRegistry());
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

    auto _pTextA = _ProjectA.Resources().Load<TextResource>("memory://same-resource");
    auto _pTextB = _ProjectB.Resources().Load<TextResource>("memory://same-resource");

    ASSERT_NE(nullptr, _pTextA);
    ASSERT_NE(nullptr, _pTextB);
    EXPECT_EQ("A", _pTextA->Text);
    EXPECT_EQ("B", _pTextB->Text);
}

TEST(ProjectCatalogTest, CloseTransientProjectKeepsMainProjectOpen)
{
    CProjectCatalog _Catalog(MakeCatalogInfo());

    auto _pMainProject = _Catalog.OpenMainProject("A");
    auto _pTransientProject = _Catalog.OpenTransientProject("B Preview");
    const auto _MainProjectID = _pMainProject->GetProjectID();
    const auto _TransientProjectID = _pTransientProject->GetProjectID();

    EXPECT_TRUE(_Catalog.CloseProject(_TransientProjectID));
    EXPECT_EQ(nullptr, _Catalog.FindProject(_TransientProjectID));
    ASSERT_NE(nullptr, _Catalog.FindProject(_MainProjectID));
    EXPECT_TRUE(_Catalog.FindProject(_MainProjectID)->IsOpen());
    EXPECT_EQ(_pMainProject, _Catalog.GetMainProject());
    EXPECT_EQ(1u, _Catalog.Count());
}

TEST(ProjectTest, StartRunsFrameHandlerOnProjectThread)
{
    std::mutex _Mutex;
    std::condition_variable _Condition;
    int _FrameCount = 0;
    bool _bPostOfficeValid = false;
    std::thread::id _HandlerThreadID;

    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Threaded";
    _Info.nFrameIntervalMilliseconds = 1;
    _Info.FrameHandler = [&](
        CProject&,
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
    _SlowInfo.FrameHandler = [&](
        CProject&,
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
    _FastInfo.FrameHandler = [&](
        CProject&,
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
    _FaultInfo.FrameHandler = [&](
        CProject&,
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
    _HealthyInfo.FrameHandler = [&](
        CProject&,
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

TEST(ProjectTest, CloseInvalidatesProjectMailPostOffices)
{
    auto _Info = MakeProjectInfo();
    _Info.ProjectName = "Mail";

    CProject _Project(_Info);
    auto _FrontendPostOffice = _Project.GetFrontendPostOffice();
    auto _BackendPostOffice = _Project.GetBackendPostOffice();
    ASSERT_TRUE(_FrontendPostOffice.IsValid());
    ASSERT_TRUE(_BackendPostOffice.IsValid());

    _Project.Close();

    EXPECT_FALSE(_FrontendPostOffice.IsValid());
    EXPECT_FALSE(_BackendPostOffice.IsValid());
    EXPECT_THROW(_FrontendPostOffice.Send(iCAX::Mail::Mail{}), std::logic_error);
    EXPECT_THROW(_BackendPostOffice.Receive(), std::logic_error);
    EXPECT_THROW(_Project.GetFrontendPostOffice(), std::logic_error);
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
