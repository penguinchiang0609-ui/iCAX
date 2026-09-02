#include "pch.h"
#include "LayerComponents.h"
#include "SceneComponents.h"
#include "SelectionComponents.h"
#include "ToolpathComponents.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "SDOSupport.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "ProjectContext/ISceneContext.h"
#include "Laser3DCAM.h"


namespace
{
    template <typename TComponent>
    std::shared_ptr<TComponent> _GetComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            return nullptr;
        }
        return std::dynamic_pointer_cast<TComponent>(pEntity_->GetComponent(TComponent::S_ClassName));
    }

    template <typename TComponent>
    std::shared_ptr<TComponent> _AddComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            throw std::invalid_argument("Scene bootstrap add component requires entity");
        }

        std::shared_ptr<iCAX::Database::CComponentBase> _pComponent;
        std::string _strError;
        if (!pEntity_->AddComponent(TComponent::S_ClassName, _pComponent, _strError) || !_pComponent)
        {
            throw std::runtime_error(_strError.empty()
                ? "Scene bootstrap add component failed: " + std::string(TComponent::S_ClassName)
                : _strError);
        }

        auto _pTyped = std::dynamic_pointer_cast<TComponent>(_pComponent);
        if (!_pTyped)
        {
            throw std::runtime_error("Scene bootstrap add component returned unexpected component type");
        }
        return _pTyped;
    }

    template <typename TComponent>
    std::shared_ptr<TComponent> _GetOrAddComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        auto _pComponent = _GetComponent<TComponent>(pEntity_);
        if (_pComponent)
        {
            return _pComponent;
        }
        return _AddComponent<TComponent>(pEntity_);
    }

    /*
    * @brief Laser3DCAM 场景启动行为。
    * @details
    *   该行为绑定独立的启动 marker component，避免把启动职责塞进 CRootComponent，
    *   也避免破坏“一种 ComponentClass 只绑定一个 Behaviour”的调度规则。
    */
    class CSceneBootstrapBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CSceneBootstrapBehaviour)

    public:
        std::string GetComponentClass() const override
        {
            return iCAX::CAM::CSceneBootstrapComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = -10000;
            return _Schedule;
        }

    protected:
        void OnAwake(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext&,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            auto _pMetaEntity = SceneContext_.Database().GetMetaEntity();
            if (!_pMetaEntity)
            {
                throw std::runtime_error("LaserCam scene bootstrap requires repository meta entity");
            }

            (void)_GetOrAddComponent<iCAX::CAM::CRootComponent>(_pMetaEntity);
            (void)_GetOrAddComponent<iCAX::CAM::CSelectionComponent>(_pMetaEntity);
        }
    };
}
