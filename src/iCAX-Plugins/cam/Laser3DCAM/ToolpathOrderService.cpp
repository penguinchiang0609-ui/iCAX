#include "pch.h"
#include "ToolpathOrderService.h"

#include "ToolpathComponents.h"

#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "ProjectContext/ISceneContext.h"
#include "Services/ServicesHelper.h"

#include <optional>
#include <stdexcept>
#include <utility>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    constexpr const char* kProgramChildKindBlock = "block";
    constexpr const char* kProgramChildKindPath = "path";

    template <typename TComponent>
    std::shared_ptr<TComponent> _GetComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            return nullptr;
        }
        return pEntity_->GetComponent<TComponent>();
    }

    std::optional<std::pair<std::string, iCAX::Data::uuid>> _TryReadProgramChildReference(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return std::nullopt;
        }

        const auto _Object = Value_.To<ObjectMap>();
        const auto _KindIter = _Object.find("kind");
        const auto _IDIter = _Object.find("entityId");
        if (_KindIter == _Object.end() || _IDIter == _Object.end()
            || !_KindIter->second.Is<std::string>() || !_IDIter->second.Is<std::string>())
        {
            return std::nullopt;
        }

        const auto _ParsedID = iCAX::Data::uuid::from_string(_IDIter->second.To<std::string>());
        if (!_ParsedID.has_value() || _ParsedID->is_nil())
        {
            return std::nullopt;
        }
        return std::make_pair(_KindIter->second.To<std::string>(), *_ParsedID);
    }

    ObjectMap _MakeProgramChildReference(IN const iCAX::CAM::SProgramChildRef& Child_)
    {
        ObjectMap _Child;
        _Child["kind"] = Child_.Kind;
        _Child["entityId"] = iCAX::Data::to_string(Child_.EntityID);
        return _Child;
    }

    std::shared_ptr<iCAX::CAM::CLaserCamRootComponent> _GetRoot(IN iCAX::Database::IRepository& Repository_)
    {
        auto _pMetaEntity = Repository_.GetMetaEntity();
        return _GetComponent<iCAX::CAM::CLaserCamRootComponent>(_pMetaEntity);
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CCAMBlockComponent>> _RequireBlock(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& BlockEntityID_)
    {
        iCAX::Data::uuid _BlockID = BlockEntityID_;
        if (_BlockID.is_nil())
        {
            auto _pRoot = _GetRoot(Repository_);
            if (!_pRoot || _pRoot->GetProgramRootBlockID().is_nil())
            {
                throw std::invalid_argument("Toolpath order requires program root block");
            }
            _BlockID = _pRoot->GetProgramRootBlockID();
        }

        auto _pBlockEntity = Repository_.GetEntity(_BlockID);
        auto _pBlock = _GetComponent<iCAX::CAM::CCAMBlockComponent>(_pBlockEntity);
        if (!_pBlockEntity || !_pBlock)
        {
            throw std::invalid_argument("Toolpath order target block does not exist");
        }
        return { _pBlockEntity, _pBlock };
    }

    bool _ValidateChildExists(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::CAM::SProgramChildRef& Child_,
        OUT std::string& strError_)
    {
        auto _pEntity = Repository_.GetEntity(Child_.EntityID);
        if (!_pEntity)
        {
            strError_ = "Toolpath order child entity does not exist";
            return false;
        }
        if (Child_.Kind == kProgramChildKindPath)
        {
            if (!_GetComponent<iCAX::CAM::CCAMPathComponent>(_pEntity))
            {
                strError_ = "Toolpath order path child has no path component";
                return false;
            }
            return true;
        }
        if (Child_.Kind == kProgramChildKindBlock)
        {
            if (!_GetComponent<iCAX::CAM::CCAMBlockComponent>(_pEntity))
            {
                strError_ = "Toolpath order block child has no block component";
                return false;
            }
            return true;
        }

        strError_ = "Toolpath order child kind is invalid";
        return false;
    }

    bool _SetBlockChildren(
        IN const std::shared_ptr<iCAX::CAM::CCAMBlockComponent>& pBlock_,
        IN const VariantArray& Children_,
        OUT std::string& strError_)
    {
        if (!pBlock_->SetProperty(
            iCAX::CAM::CCAMBlockComponent::PropertyName_Children,
            iCAX::Data::PropertyValue(Children_),
            strError_))
        {
            if (strError_.empty())
            {
                strError_ = "Toolpath order failed to update block children";
            }
            return false;
        }
        return true;
    }
}

namespace iCAX
{
    namespace CAM
    {
        class CToolpathOrderService final : public IToolpathOrderService
        {
            AUTO_REGIST_SERVICE(iCAX::CAM::IToolpathOrderService, CToolpathOrderService)

        public:
            CToolpathOrderService() = default;
            ~CToolpathOrderService() override = default;

            void OnLoad() override
            {
            }

            void OnUnload() override
            {
            }

            SToolpathOrderPlan BuildOrderPlan(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SToolpathOrderRequest& Request_) override
            {
                auto& _Repository = Scene_.Database();
                auto [_pBlockEntity, _pBlock] = _RequireBlock(_Repository, Request_.BlockEntityID);

                SToolpathOrderPlan _Plan;
                _Plan.BlockEntityID = _pBlockEntity->GetID();
                _Plan.Strategy = Request_.Strategy.empty() ? std::string("NearestNeighbor") : Request_.Strategy;

                for (const auto& _Child : _pBlock->GetChildren())
                {
                    auto _Reference = _TryReadProgramChildReference(_Child);
                    if (!_Reference.has_value())
                    {
                        continue;
                    }

                    SProgramChildRef _ChildRef;
                    _ChildRef.Kind = _Reference->first;
                    _ChildRef.EntityID = _Reference->second;
                    _Plan.OrderedChildren.emplace_back(std::move(_ChildRef));
                }

                ObjectMap _Diagnostic;
                _Diagnostic["level"] = std::string("info");
                _Diagnostic["message"] = std::string("Toolpath order algorithm is not implemented yet; current order is preserved");
                _Plan.Diagnostics.emplace_back(_Diagnostic);
                return _Plan;
            }

            bool ApplyOrderPlan(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SToolpathOrderPlan& Plan_,
                OUT std::string& strError_) override
            {
                auto& _Repository = Scene_.Database();
                auto [_pBlockEntity, _pBlock] = _RequireBlock(_Repository, Plan_.BlockEntityID);

                VariantArray _Children;
                _Children.reserve(Plan_.OrderedChildren.size());
                for (const auto& _Child : Plan_.OrderedChildren)
                {
                    if (!_ValidateChildExists(_Repository, _Child, strError_))
                    {
                        return false;
                    }
                    _Children.emplace_back(_MakeProgramChildReference(_Child));
                }

                return _SetBlockChildren(_pBlock, _Children, strError_);
            }
        };
    }
}
