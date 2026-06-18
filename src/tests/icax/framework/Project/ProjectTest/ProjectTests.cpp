#include <gtest/gtest.h>

#include <Project/Project.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace iCAX::Project;

namespace
{
    struct TextResource
    {
        std::string Text;
    };

    CProductDefinition MakeProduct(const std::string& ProductID_, const std::string& DisplayName_)
    {
        CProductDefinition _Product;
        _Product.ProductID = ProductID_;
        _Product.DisplayName = DisplayName_;
        return _Product;
    }

    std::filesystem::path PrepareTempDirectory()
    {
        auto _Path = std::filesystem::temp_directory_path() / "icax_project_catalog_test";
        std::filesystem::remove_all(_Path);
        std::filesystem::create_directories(_Path);
        return _Path;
    }

    void WriteTextFile(const std::filesystem::path& Path_, const std::string& Text_)
    {
        std::filesystem::create_directories(Path_.parent_path());
        std::ofstream _Output(Path_, std::ios::binary);
        _Output << Text_;
    }

    bool WaitFor(std::condition_variable& Condition_, std::mutex& Mutex_, const std::function<bool()>& Predicate_)
    {
        std::unique_lock<std::mutex> _Lock(Mutex_);
        return Condition_.wait_for(_Lock, std::chrono::seconds(2), Predicate_);
    }
}

TEST(ProductCatalogTest, RegistersMultipleProductDefinitions)
{
    CProductCatalog _Catalog;

    EXPECT_TRUE(_Catalog.Register(MakeProduct("icax.product.five-axis", "Five Axis")));
    EXPECT_TRUE(_Catalog.Register(MakeProduct("icax.product.flat-cut", "Flat Cut")));
    EXPECT_FALSE(_Catalog.Register(MakeProduct("icax.product.flat-cut", "Flat Cut Duplicate")));

    EXPECT_TRUE(_Catalog.Has("icax.product.five-axis"));
    EXPECT_TRUE(_Catalog.Has("icax.product.flat-cut"));
    EXPECT_EQ(2u, _Catalog.Count());

    auto _FiveAxis = _Catalog.Require("icax.product.five-axis");
    EXPECT_EQ("Five Axis", _FiveAxis.GetDisplayNameOrID());
}

TEST(ProductCatalogTest, LoadsProductIdManifestDirectoryAndResolvesPaths)
{
    auto _Root = PrepareTempDirectory();
    auto _ProductA = _Root / "five-axis";
    auto _ProductB = _Root / "flat-cut";

    WriteTextFile(
        _ProductA / "manifest.json",
        R"json({
  "productId": "icax.product.five-axis",
  "displayName": "Five Axis",
  "backend": {
    "components": [ "bin/FiveComponent.dll" ],
    "behaviours": [ "bin/FiveBehaviour.dll" ],
    "services": [ "bin/FiveService.dll" ],
    "commands": [ "bin/FiveCommand.dll" ]
  },
  "frontend": { "entry": "frontend/index.html" },
  "protocol": { "path": "protocol" },
  "startup": { "firstComponent": "FiveStartup" },
  "file": { "extensions": [ ".icax5" ] }
})json");

    WriteTextFile(
        _ProductB / "manifest.json",
        R"json({
  "productId": "icax.product.flat-cut",
  "displayName": "Flat Cut"
})json");

    CProductCatalog _Catalog;
    auto _Products = _Catalog.LoadManifestDirectory(_Root.string());

    ASSERT_EQ(2u, _Products.size());
    CProductDefinition _FiveAxis;
    for (const auto& _Product : _Products)
    {
        if (_Product.ProductID == "icax.product.five-axis")
        {
            _FiveAxis = _Product;
        }
    }

    EXPECT_EQ("Five Axis", _FiveAxis.DisplayName);
    ASSERT_EQ(1u, _FiveAxis.ComponentModules.size());
    EXPECT_EQ((_ProductA / "bin/FiveComponent.dll").lexically_normal().string(), _FiveAxis.ComponentModules[0]);
    ASSERT_EQ(1u, _FiveAxis.BehaviourModules.size());
    EXPECT_EQ((_ProductA / "bin/FiveBehaviour.dll").lexically_normal().string(), _FiveAxis.BehaviourModules[0]);
    ASSERT_EQ(1u, _FiveAxis.ServiceModules.size());
    EXPECT_EQ((_ProductA / "bin/FiveService.dll").lexically_normal().string(), _FiveAxis.ServiceModules[0]);
    ASSERT_EQ(1u, _FiveAxis.CommandModules.size());
    EXPECT_EQ((_ProductA / "bin/FiveCommand.dll").lexically_normal().string(), _FiveAxis.CommandModules[0]);
    EXPECT_EQ((_ProductA / "frontend/index.html").lexically_normal().string(), _FiveAxis.FrontendEntry);
    EXPECT_EQ((_ProductA / "protocol").lexically_normal().string(), _FiveAxis.ProtocolPath);
    EXPECT_EQ("FiveStartup", _FiveAxis.StartupComponent);
    ASSERT_EQ(1u, _FiveAxis.FileExtensions.size());
    EXPECT_EQ(".icax5", _FiveAxis.FileExtensions[0]);

    std::filesystem::remove_all(_Root);
}

TEST(ProductCatalogTest, ManifestRequiresProductID)
{
    auto _Root = PrepareTempDirectory();
    auto _Manifest = _Root / "invalid" / "manifest.json";
    WriteTextFile(_Manifest, R"json({ "displayName": "Missing ID" })json");

    CProductCatalog _Catalog;
    EXPECT_THROW(_Catalog.LoadManifestFile(_Manifest.string()), std::runtime_error);

    std::filesystem::remove_all(_Root);
}

TEST(ProjectManagerTest, OpensMultipleProductsAsIndependentProjects)
{
    CProjectManager _Manager;
    _Manager.Products().Set(MakeProduct("icax.product.five-axis", "Five Axis"));
    _Manager.Products().Set(MakeProduct("icax.product.flat-cut", "Flat Cut"));

    auto _pFiveAxis = _Manager.OpenProject("icax.product.five-axis", "Robot Cell");
    auto _pFlatCut = _Manager.OpenProject("icax.product.flat-cut", "Sheet Nesting");

    ASSERT_NE(nullptr, _pFiveAxis);
    ASSERT_NE(nullptr, _pFlatCut);
    EXPECT_NE(_pFiveAxis->GetProjectID(), _pFlatCut->GetProjectID());
    EXPECT_EQ("icax.product.five-axis", _pFiveAxis->GetProductID());
    EXPECT_EQ("icax.product.flat-cut", _pFlatCut->GetProductID());
    EXPECT_EQ(2u, _Manager.Count());

    EXPECT_NE(_pFiveAxis->Database().GetID(), _pFlatCut->Database().GetID());
    EXPECT_EQ(_pFiveAxis->GetProjectID(), _pFiveAxis->Database().GetID());
    EXPECT_EQ(_pFlatCut->GetProjectID(), _pFlatCut->Database().GetID());
}

TEST(ProjectManagerTest, ProjectResourcesAreIsolated)
{
    CProjectManager _Manager;
    _Manager.Products().Set(MakeProduct("icax.product.welding", "Welding"));

    auto _pProjectA = _Manager.OpenProject("icax.product.welding", "Weld A");
    auto _pProjectB = _Manager.OpenProject("icax.product.welding", "Weld B");

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

TEST(ProjectManagerTest, CloseOneProjectKeepsOthersOpen)
{
    CProjectManager _Manager;
    _Manager.Products().Set(MakeProduct("icax.product.flat-cut", "Flat Cut"));

    auto _pProjectA = _Manager.OpenProject("icax.product.flat-cut", "A");
    auto _pProjectB = _Manager.OpenProject("icax.product.flat-cut", "B");
    const auto _ProjectAID = _pProjectA->GetProjectID();
    const auto _ProjectBID = _pProjectB->GetProjectID();

    EXPECT_TRUE(_Manager.CloseProject(_ProjectAID));
    EXPECT_EQ(nullptr, _Manager.FindProject(_ProjectAID));
    ASSERT_NE(nullptr, _Manager.FindProject(_ProjectBID));
    EXPECT_TRUE(_Manager.FindProject(_ProjectBID)->IsOpen());
    EXPECT_EQ(1u, _Manager.Count());
}

TEST(ProjectSessionTest, StartRunsFrameHandlerOnProjectThread)
{
    std::mutex _Mutex;
    std::condition_variable _Condition;
    int _FrameCount = 0;
    bool _bPostOfficeValid = false;
    std::thread::id _HandlerThreadID;

    CProjectSessionCreateInfo _Info;
    _Info.Product = MakeProduct("icax.product.threaded", "Threaded");
    _Info.nFrameIntervalMilliseconds = 1;
    _Info.FrameHandler = [&](
        CProjectSession&,
        const iCAX::Mail::CMailPostOffice& PostOffice_) {
        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _HandlerThreadID = std::this_thread::get_id();
            _bPostOfficeValid = PostOffice_.IsValid();
            ++_FrameCount;
        }
        _Condition.notify_all();
    };

    CProjectSession _Session(_Info);
    _Session.Start();

    EXPECT_TRUE(WaitFor(_Condition, _Mutex, [&]() { return _FrameCount >= 2; }));
    _Session.Stop();

    EXPECT_FALSE(_Session.IsRunning());
    EXPECT_TRUE(_bPostOfficeValid);
    EXPECT_NE(std::thread::id{}, _HandlerThreadID);
    EXPECT_NE(std::this_thread::get_id(), _HandlerThreadID);
}

TEST(ProjectSessionTest, SessionsRunOnIndependentThreads)
{
    std::mutex _Mutex;
    std::condition_variable _Condition;
    std::atomic<int> _SlowFrames = 0;
    std::atomic<int> _FastFrames = 0;
    std::thread::id _SlowThreadID;
    std::thread::id _FastThreadID;

    CProjectSessionCreateInfo _SlowInfo;
    _SlowInfo.Product = MakeProduct("icax.product.slow", "Slow");
    _SlowInfo.nFrameIntervalMilliseconds = 1;
    _SlowInfo.FrameHandler = [&](
        CProjectSession&,
        const iCAX::Mail::CMailPostOffice&) {
        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _SlowThreadID = std::this_thread::get_id();
            ++_SlowFrames;
        }
        _Condition.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    };

    CProjectSessionCreateInfo _FastInfo;
    _FastInfo.Product = MakeProduct("icax.product.fast", "Fast");
    _FastInfo.nFrameIntervalMilliseconds = 1;
    _FastInfo.FrameHandler = [&](
        CProjectSession&,
        const iCAX::Mail::CMailPostOffice&) {
        {
            std::lock_guard<std::mutex> _Lock(_Mutex);
            _FastThreadID = std::this_thread::get_id();
            ++_FastFrames;
        }
        _Condition.notify_all();
    };

    CProjectSession _SlowSession(_SlowInfo);
    CProjectSession _FastSession(_FastInfo);
    _SlowSession.Start();
    _FastSession.Start();

    EXPECT_TRUE(WaitFor(_Condition, _Mutex, [&]() {
        return _SlowFrames.load(std::memory_order_acquire) >= 1
            && _FastFrames.load(std::memory_order_acquire) >= 5;
    }));

    _SlowSession.Stop();
    _FastSession.Stop();

    EXPECT_GE(_SlowFrames.load(std::memory_order_acquire), 1);
    EXPECT_GE(_FastFrames.load(std::memory_order_acquire), 5);
    EXPECT_NE(std::thread::id{}, _SlowThreadID);
    EXPECT_NE(std::thread::id{}, _FastThreadID);
    EXPECT_NE(_SlowThreadID, _FastThreadID);
}

TEST(ProjectSessionTest, CloseInvalidatesProjectMailPostOffices)
{
    CProjectSessionCreateInfo _Info;
    _Info.Product = MakeProduct("icax.product.mail", "Mail");

    CProjectSession _Session(_Info);
    auto _FrontendPostOffice = _Session.GetFrontendPostOffice();
    auto _BackendPostOffice = _Session.GetBackendPostOffice();
    ASSERT_TRUE(_FrontendPostOffice.IsValid());
    ASSERT_TRUE(_BackendPostOffice.IsValid());

    _Session.Close();

    EXPECT_FALSE(_FrontendPostOffice.IsValid());
    EXPECT_FALSE(_BackendPostOffice.IsValid());
    EXPECT_THROW(_FrontendPostOffice.Send(iCAX::Mail::Mail{}), std::logic_error);
    EXPECT_THROW(_BackendPostOffice.Receive(), std::logic_error);
    EXPECT_THROW(_Session.GetFrontendPostOffice(), std::logic_error);
}
