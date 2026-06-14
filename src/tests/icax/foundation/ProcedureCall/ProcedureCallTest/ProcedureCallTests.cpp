#include <gtest/gtest.h>

#include <Data/StableId.h>
#include <ProcedureCall/IPCRegistry.h>

#include <stdexcept>
#include <string>

using namespace iCAX::PC;

namespace
{
    int EchoInt(IN OUT void* Context_, IN const void* IParam_, OUT void* OParam_)
    {
        if (Context_ == nullptr || IParam_ == nullptr || OParam_ == nullptr)
        {
            return PC_FAILED;
        }

        const int _Base = *static_cast<int*>(Context_);
        const int _Input = *static_cast<const int*>(IParam_);
        *static_cast<int*>(OParam_) = _Base + _Input;
        return PC_SUCCESS;
    }

    int MacroRegisteredHandler(IN OUT void* Context_, IN const void* IParam_, OUT void* OParam_)
    {
        return EchoInt(Context_, IParam_, OParam_);
    }

    AUTO_REGIST_PC(ProcedureCallTestModule, MacroRegisteredHandler)
}

TEST(ProcedureCallTest, MakePCIDUsesStableDataId)
{
    const std::string _Module = "Renderer.Camera";
    const std::string _Name = "SetPosition";

    const auto _Id = MakePCID(_Module, _Name);

    EXPECT_EQ(iCAX::Data::MakeStableId(_Module, _Name), _Id);
    EXPECT_EQ(_Id, MakePCID(_Module, _Name));
    EXPECT_NE(_Id, MakePCID(_Module, "SetRotation"));
}

TEST(ProcedureCallTest, RegistryRegistersFindsAndRejectsDuplicate)
{
    auto _Registry = GetGlobalPCRegistry();
    const auto _Id = MakePCID("ProcedureCallTest", "DuplicateRegistration");

    EXPECT_TRUE(_Registry->Regist(_Id, &EchoInt));
    EXPECT_FALSE(_Registry->Regist(_Id, &EchoInt));

    auto _Fn = _Registry->Find(_Id);
    ASSERT_NE(nullptr, _Fn);

    int _Context = 40;
    int _Input = 2;
    int _Output = 0;
    EXPECT_EQ(PC_SUCCESS, _Fn(&_Context, &_Input, &_Output));
    EXPECT_EQ(42, _Output);
}

TEST(ProcedureCallTest, FindUnknownThrowsOutOfRange)
{
    const auto _MissingId = MakePCID("ProcedureCallTest", "MissingEntry");

    EXPECT_THROW((void)GetGlobalPCRegistry()->Find(_MissingId), std::out_of_range);
}

TEST(ProcedureCallTest, GlobalRegistryReturnsSameInstance)
{
    auto _First = GetGlobalPCRegistry();
    auto _Second = GetGlobalPCRegistry();

    EXPECT_EQ(_First.get(), _Second.get());
}

TEST(ProcedureCallTest, AutoRegistPCRegistersFunction)
{
    const auto _Id = MakePCID("ProcedureCallTestModule", "MacroRegisteredHandler");
    auto _Fn = GetGlobalPCRegistry()->Find(_Id);
    ASSERT_NE(nullptr, _Fn);

    int _Context = 10;
    int _Input = 5;
    int _Output = 0;
    EXPECT_EQ(PC_SUCCESS, _Fn(&_Context, &_Input, &_Output));
    EXPECT_EQ(15, _Output);
}
