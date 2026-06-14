#include "SampleBehaviour.h"
#include "pch.h"
#include "SampleBehaviour.h"
#include "Behaviour/IWorld.h"
#include <iostream>
#include "SampleComponent/SampleComponent.h"
#include "SampleService/ISampleService.h"

//! 构造函数
iCAX::Sample::SampleBehaviour::SampleBehaviour()
{
}

//! 析构函数
iCAX::Sample::SampleBehaviour::~SampleBehaviour()
{
}

//! 获取组件类型名称
std::string iCAX::Sample::SampleBehaviour::GetComponentClass() const
{
    return SampleComponent::S_ClassName;
}

//! 创建之后触发
void iCAX::Sample::SampleBehaviour::OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnAwake" << std::endl;
}

//! 创建之后下一帧触发
void iCAX::Sample::SampleBehaviour::OnStart(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnStart" << std::endl;
}

//! 修改Enable状态时触发
void iCAX::Sample::SampleBehaviour::OnEnable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnEnable" << std::endl;
}

//! 每一帧预触发
void iCAX::Sample::SampleBehaviour::OnPreUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnPreUpdate" << std::endl;
}

//! 每一帧触发
void iCAX::Sample::SampleBehaviour::OnUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnUpdate" << std::endl;
}

//! 每一帧后触发
void iCAX::Sample::SampleBehaviour::OnPostUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnPostUpdate" << std::endl;
}

//! 禁用时触发
void iCAX::Sample::SampleBehaviour::OnDisable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnDisable" << std::endl;
}

//! 销毁时触发
void iCAX::Sample::SampleBehaviour::OnDestroy(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnDestroy" << std::endl;
}

//! 组件数据修改前触发
void iCAX::Sample::SampleBehaviour::OnModifing(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnModifing" << std::endl;
}

//! 组件数据修改后触发
void iCAX::Sample::SampleBehaviour::OnModified(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const iCAX::Behaviour::IUniverseContext& Context_)
{
    std::cout << "OnModified" << std::endl;
}


int iCAX::Sample::TestIDSet(IN OUT void* pContext_, IN const void* IParam_, OUT void* OParam_)
{
    auto _pComponent = reinterpret_cast<iCAX::Sample::SampleComponent*>(pContext_);
    auto _pN = reinterpret_cast<const int*>(IParam_);
    if (_pComponent == nullptr || _pN == nullptr)
    {
        return PC_FAILED;
    }


    auto& _Component = dynamic_cast<iCAX::Sample::SampleComponent&>(*_pComponent);
    int _nID = _Component.GetTestID() + *_pN;
    _Component.SetTestID(_nID);

    auto _pR = reinterpret_cast<int*>(OParam_);
    *_pR = _nID;
    
    return PC_SUCCESS;
}
