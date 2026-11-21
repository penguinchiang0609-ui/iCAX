#include <gtest/gtest.h>
#include <GeometryServices/PlaneNestingService.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <future>
#include <limits>
#include <set>
#include <thread>

namespace PN=iCAX::GeometryServices::PlaneNesting;
using iCAX::GeometryServices::IPlaneNestingService;
namespace
{
    std::string loadProvider()
    {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr,executable,MAX_PATH);
        const auto path=std::filesystem::path(executable).parent_path()/L"PlaneNestingServices.dll";
        // Registration callbacks reside in the DLL for the process lifetime.
        static const auto module=LoadLibraryW(path.c_str());
        if (!module) throw std::runtime_error("Cannot load PlaneNestingServices.dll: "+std::to_string(GetLastError()));
        return path.string();
    }
    PN::Ring rectangle(double width,double height,double x=0,double y=0)
    {
        return {{x,y},{x+width,y},{x+width,y+height},{x,y+height}};
    }
    PN::Request simple(int quantity=1,int boards=1)
    {
        PN::Request request;request.InputKey="native-service-input";request.TimeLimitSeconds=.4;
        PN::Part part;part.Id="part";part.Outer=rectangle(10,10);part.Quantity=quantity;
        request.Parts.push_back(part);
        for (int k=0;k<boards;++k) request.Stocks.push_back({"physical:"+std::to_string(k+1),rectangle(10,10),{}});
        return request;
    }
    class PlaneNestingService : public ::testing::Test
    {
    protected:
        iCAX::Services::CServiceProvider provider;
        std::shared_ptr<IPlaneNestingService> service;
        void SetUp() override
        {
            iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(provider,{loadProvider()});
            service=provider.Resolve<IPlaneNestingService>();
        }
        void valid(const PN::Result& result)
        {
            ASSERT_TRUE(result.Valid)<<result.Diagnostic;
            EXPECT_TRUE(result.Checks.InputAccepted);EXPECT_TRUE(result.Checks.GeometryValid);
            EXPECT_TRUE(result.Checks.DemandConserved);EXPECT_TRUE(result.Checks.StockConserved);EXPECT_TRUE(result.Checks.OrientationsValid);
            EXPECT_EQ("PolygonMaterial",result.Checks.Scope);
            EXPECT_EQ(result.Statistics.RequiredCount,result.Placements.size()+result.UnplacedParts.size());
            std::set<std::pair<std::string,std::size_t>> instances;
            for (const auto& placement : result.Placements) EXPECT_TRUE(instances.emplace(placement.PartId,placement.InstanceIndex).second);
            for (const auto& unplaced : result.UnplacedParts) EXPECT_TRUE(instances.emplace(unplaced.PartId,unplaced.InstanceIndex).second);
        }
    };
}

TEST_F(PlaneNestingService, RegistersInProviderWithoutProductOrSceneDependencies)
{
    EXPECT_EQ(service,provider.Resolve<IPlaneNestingService>());
    iCAX::Services::CServiceProvider another;
    iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(another,{loadProvider()});
    EXPECT_NE(service,another.Resolve<IPlaneNestingService>());
    EXPECT_EQ(nullptr,GetModuleHandleW(L"iPlaneCAM.dll"));
    EXPECT_EQ(nullptr,GetModuleHandleW(L"FiveAxisCAM.dll"));
}

TEST_F(PlaneNestingService, ConservesQuantityAndDistinctPhysicalStockIdentities)
{
    const auto result=service->Solve(simple(3,2));valid(result);
    ASSERT_EQ(2u,result.Placements.size());ASSERT_EQ(1u,result.UnplacedParts.size());
    EXPECT_EQ(2u,result.Statistics.UsedStockCount);EXPECT_NE(PN::Status::Completed,result.State);
    std::set<std::string> physical;
    for (const auto& placement : result.Placements) physical.insert(placement.PhysicalStockId);
    EXPECT_EQ((std::set<std::string>{"physical:1","physical:2"}),physical);
    EXPECT_DOUBLE_EQ(200,result.Statistics.StockNetArea);EXPECT_DOUBLE_EQ(200,result.Statistics.PlacedMaterialArea);
    EXPECT_DOUBLE_EQ(1,result.Statistics.Utilization);EXPECT_EQ("native-service-input",result.InputKey);
}

TEST_F(PlaneNestingService, RestoresOriginalStockFrameAndTransformedPivot)
{
    auto request=simple();request.Parts[0].Outer=rectangle(20,10);request.Parts[0].Pivot={5,2};
    request.Stocks[0].Outline=rectangle(20,10,1000,-300);
    const auto result=service->Solve(request);valid(result);ASSERT_EQ(1u,result.Placements.size());
    EXPECT_NEAR(1005,result.Placements[0].X,1e-6);EXPECT_NEAR(-298,result.Placements[0].Y,1e-6);
    EXPECT_EQ("physical:1",result.Placements[0].PhysicalStockId);
    EXPECT_DOUBLE_EQ(1000,request.Stocks[0].Outline[0].X);
}

TEST_F(PlaneNestingService, UsesActualConcaveStockInsteadOfItsBoundingBox)
{
    auto request=simple();request.Parts[0].Outer=rectangle(7,7);
    request.Stocks[0].Outline={{0,0},{10,0},{10,4},{4,4},{4,10},{0,10}};
    const auto result=service->Solve(request);valid(result);
    EXPECT_TRUE(result.Placements.empty());EXPECT_EQ(1u,result.UnplacedParts.size());
}

TEST_F(PlaneNestingService, HonorsStockHolePartGapAndStockMargin)
{
    auto request=simple(4);request.PartGap=2;request.StockMargin=3;request.TimeLimitSeconds=.7;
    request.Parts[0].Outer=rectangle(25,25);request.Stocks[0].Outline=rectangle(100,100);
    request.Stocks[0].Holes={rectangle(20,20,40,40)};
    const auto result=service->Solve(request);valid(result);ASSERT_FALSE(result.Placements.empty());
    for (const auto& p : result.Placements)
    {
        EXPECT_GE(p.X,3-1e-6);EXPECT_GE(p.Y,3-1e-6);EXPECT_LE(p.X+25,97+1e-6);EXPECT_LE(p.Y+25,97+1e-6);
        const double dx=std::max({40-(p.X+25),p.X-60,0.}),dy=std::max({40-(p.Y+25),p.Y-60,0.});
        EXPECT_GE(std::hypot(dx,dy),3-1e-6);
    }
    for (std::size_t i=0;i<result.Placements.size();++i) for (std::size_t j=0;j<i;++j)
    {
        const auto& a=result.Placements[i];const auto& b=result.Placements[j];
        const double dx=std::max(std::abs(a.X-b.X)-25,0.),dy=std::max(std::abs(a.Y-b.Y)-25,0.);
        EXPECT_GE(std::hypot(dx,dy),2-1e-6);
    }
    EXPECT_NEAR(9600,result.Statistics.StockNetArea,1e-6);
}

TEST_F(PlaneNestingService, KeepsPartHoleAvailableForAnotherPart)
{
    auto request=simple();request.TimeLimitSeconds=.8;
    request.Parts[0].Outer=rectangle(60,60);request.Parts[0].Holes={rectangle(20,20,20,20)};
    PN::Part inner;inner.Id="inner";inner.Outer=rectangle(10,10);request.Parts.push_back(inner);
    request.Stocks[0].Outline=rectangle(60,60);
    const auto result=service->Solve(request);valid(result);ASSERT_EQ(2u,result.Placements.size());
    const auto small=std::find_if(result.Placements.begin(),result.Placements.end(),[](const auto& p){return p.PartId=="inner";});
    ASSERT_NE(result.Placements.end(),small);
    EXPECT_GE(small->X,20-1e-6);EXPECT_LE(small->X+10,40+1e-6);
    EXPECT_GE(small->Y,20-1e-6);EXPECT_LE(small->Y+10,40+1e-6);
    EXPECT_NEAR(3300,result.Statistics.PlacedMaterialArea,1e-6);
}

TEST_F(PlaneNestingService, SolvesRequiredMirrorAndRespectsDisabledMirror)
{
    auto request=simple();request.TimeLimitSeconds=.7;
    request.Parts[0].Outer={{0,0},{20,0},{0,10}};
    request.Stocks[0].Outline={{0,10},{0,0},{20,10}};
    const auto forbidden=service->Solve(request);valid(forbidden);EXPECT_TRUE(forbidden.Placements.empty());
    request.Parts[0].AllowMirror=true;
    const auto allowed=service->Solve(request);valid(allowed);ASSERT_EQ(1u,allowed.Placements.size());EXPECT_TRUE(allowed.Placements[0].Flip);
}

TEST_F(PlaneNestingService, SupportsDiscreteAndContinuousRotationConstraints)
{
    auto request=simple();request.TimeLimitSeconds=.6;request.Parts[0].Outer=rectangle(30,5);request.Stocks[0].Outline=rectangle(10,35);
    request.Parts[0].AnglesDeg={90};
    const auto discrete=service->Solve(request);valid(discrete);ASSERT_EQ(1u,discrete.Placements.size());EXPECT_NEAR(90,discrete.Placements[0].AngleDeg,1e-8);
    request.Parts[0].AnglesDeg.clear();request.Parts[0].RotationIntervals={{80,100}};
    const auto continuous=service->Solve(request);valid(continuous);ASSERT_EQ(1u,continuous.Placements.size());
    EXPECT_GE(continuous.Placements[0].AngleDeg,80-1e-8);EXPECT_LE(continuous.Placements[0].AngleDeg,100+1e-8);
}

TEST_F(PlaneNestingService, RejectsDuplicatePhysicalIdsAndInvalidMaterialWithoutPartialOutput)
{
    auto request=simple(2,2);request.Stocks[1].Id=request.Stocks[0].Id;
    auto result=service->Solve(request);EXPECT_EQ(PN::Status::InvalidInput,result.State);EXPECT_FALSE(result.Valid);EXPECT_TRUE(result.Placements.empty());
    request=simple();request.Stocks[0].Holes={rectangle(3,3,20,20)};
    result=service->Solve(request);EXPECT_EQ(PN::Status::InvalidInput,result.State);EXPECT_FALSE(result.Valid);EXPECT_TRUE(result.Placements.empty());
    request=simple();request.Parts[0].Outer[0].X=std::numeric_limits<double>::quiet_NaN();
    result=service->Solve(request);EXPECT_EQ(PN::Status::InvalidInput,result.State);EXPECT_FALSE(result.Valid);
    request=simple(501);result=service->Solve(request);EXPECT_EQ(PN::Status::ResourceLimit,result.State);EXPECT_TRUE(result.Placements.empty());
    const auto recovered=service->Solve(simple());valid(recovered);EXPECT_EQ(1u,recovered.Placements.size());
}

TEST_F(PlaneNestingService, ReportsNoStockAndEmptyDemandWithoutInventingPlacements)
{
    auto request=simple();request.Stocks.clear();
    const auto missing=service->Solve(request);valid(missing);EXPECT_EQ("NoStock",missing.StopReason);EXPECT_EQ(1u,missing.UnplacedParts.size());
    request.Parts.clear();const auto empty=service->Solve(request);valid(empty);EXPECT_EQ(PN::Status::Completed,empty.State);EXPECT_TRUE(empty.Placements.empty());
}

TEST_F(PlaneNestingService, ObservesCancellationAndTinyBudgetsAndReleasesSession)
{
    const auto cancelled=service->Solve(simple(),[]{return true;});
    EXPECT_EQ(PN::Status::Cancelled,cancelled.State);EXPECT_FALSE(cancelled.Valid);EXPECT_TRUE(cancelled.Placements.empty());
    auto request=simple();request.TimeLimitSeconds=1e-12;
    const auto exhausted=service->Solve(request);EXPECT_EQ(PN::Status::BudgetExhausted,exhausted.State);EXPECT_TRUE(exhausted.Placements.empty());
    const auto next=service->Solve(simple());valid(next);EXPECT_EQ(1u,next.Placements.size());
}

TEST_F(PlaneNestingService, ConcurrentRequestsKeepNeutralIdsAndResultsIndependent)
{
    std::vector<std::future<PN::Result>> futures;
    for (int i=0;i<3;++i) futures.push_back(std::async(std::launch::async,[&,i]
    {
        auto request=simple();request.TimeLimitSeconds=3;request.InputKey="request:"+std::to_string(i);
        request.Parts[0].Id="part:"+std::to_string(i);request.Stocks[0].Id="stock:"+std::to_string(i);
        return service->Solve(request);
    }));
    for (int i=0;i<3;++i)
    {
        const auto result=futures[static_cast<std::size_t>(i)].get();valid(result);ASSERT_EQ(1u,result.Placements.size());
        EXPECT_EQ("request:"+std::to_string(i),result.InputKey);EXPECT_EQ("part:"+std::to_string(i),result.Placements[0].PartId);
        EXPECT_EQ("stock:"+std::to_string(i),result.Placements[0].PhysicalStockId);
    }
}
