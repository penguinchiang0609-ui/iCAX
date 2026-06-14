#include "pch.h"
#include "EntitiesView.h"
#include "IDomain.h"
#include "MaskRegistry.h"

//!< 构造函数
iCAX::Database::CEntitiesView::CEntitiesView(IN std::shared_ptr<IDomain> pDomain_)
    : m_pDomain(pDomain_)
    , m_Cache()
    , m_PreCache()
    , m_EntityMask()
{
}

//!< 析构函数
iCAX::Database::CEntitiesView::~CEntitiesView()
{
}

//! 修改前事件
void iCAX::Database::CEntitiesView::OnDomainChanging(IN void* pSender_, IN const DomainEventArgs& Args_)
{
}

//!< 更改后事件
void iCAX::Database::CEntitiesView::OnDomainChanged(IN void* pSender_, IN const DomainEventArgs& Args_)
{
    if (Args_.nType == DomainEventArgs::kAddComponent)
    {
        m_EntityMask[Args_.EntityID].Set(Args_.strClassName);
        size_t _nCode = CMaskRegistry::GetComponentIndex(Args_.strClassName);
        m_Cache[_nCode].insert(Args_.pComponent);
    }
    else if (Args_.nType == DomainEventArgs::kRemoveComponent)
    {
        m_EntityMask[Args_.EntityID].Reset(Args_.strClassName);

        size_t _nCode = CMaskRegistry::GetComponentIndex(Args_.strClassName);
        m_Cache[_nCode].erase(Args_.pComponent);
    }
    else if (Args_.nType == DomainEventArgs::kAddEntity)
    {
        m_EntityMask[Args_.EntityID] = CComponentMask();
    }
    else if (Args_.nType == DomainEventArgs::kDeleteEntity)
    {
        m_EntityMask.erase(Args_.EntityID);
        // 清理缓存中该实体的组件
        for (auto& [_Key, _setComponent] : m_Cache)
        {
            for (auto _Ite = _setComponent.begin(); _Ite != _setComponent.end(); )
            {
                if (_Ite->expired())
                {
                    _Ite = _setComponent.erase(_Ite);
                }
                else
                {
                    auto _pEntity = _Ite->lock()->GetEntity();
                    if (!_pEntity)
                    {
                        _Ite = _setComponent.erase(_Ite);
                    }
                    else
                    {
                        if (_pEntity->GetID() == Args_.EntityID)
                        {
                            _Ite = _setComponent.erase(_Ite);
                        }
                        else
                        {
                            ++_Ite;
                        }
                    }
                }
            }
        }
    }
}

//!< 构建缓存
void iCAX::Database::CEntitiesView::BuildCache(IN const std::string& strClassName_, IN const bool bForceReset_)
{
    if (m_pDomain.expired())
    {
        return;
    }
    auto _pDomain = m_pDomain.lock();
    size_t _nCode = CMaskRegistry::GetComponentIndex(strClassName_);
    if (!m_Cache.contains(_nCode) || bForceReset_)//!< 未构建过，或强制重构建
    {
        if (m_Cache.contains(_nCode) && bForceReset_)
        {
            m_Cache[_nCode].clear();//! 已存在，强制刷新，则先清空
        }
        auto _pEntities = _pDomain->FilterEntities([this, strClassName_](std::shared_ptr<IEntity> _pEntity)
        {
            const auto& _ID = _pEntity->GetID();
            // 如果没有 Mask，则构建一份新的
            if (!m_EntityMask.contains(_ID))
            {
                CComponentMask mask;
                auto _vecComponents = _pEntity->GetComponentClasses();
                for (auto& _Component : _vecComponents)
                {
                    mask.Set(_Component);
                }
                m_EntityMask[_ID] = mask;
            }

            return  m_EntityMask[_ID].Test(strClassName_);
        });

        auto& _CacheSet = m_Cache[_nCode];
        for (auto& _pEntity : _pEntities)
        {
            if (!_pEntity)
                continue;

            _CacheSet.insert(_pEntity->GetComponent(strClassName_));
        }

        // 清理掉失效
        for (auto _Ite = _CacheSet.begin(); _Ite != _CacheSet.end(); )
        {
            if (_Ite->expired())
                _Ite = _CacheSet.erase(_Ite);
            else
                ++_Ite;
        }
    }
}

//!< 获取缓存
std::vector<std::shared_ptr<iCAX::Database::CComponentBase>> iCAX::Database::CEntitiesView::GetEntities(IN const std::string& strClassName_)
{
    size_t _nCode = CMaskRegistry::GetComponentIndex(strClassName_);
    auto _Ite = m_Cache.find(_nCode);
    if (_Ite != m_Cache.end())
    {
        std::vector<std::shared_ptr<iCAX::Database::CComponentBase>> _Res;
        for (auto _Ite1 = _Ite->second.begin(); _Ite1 != _Ite->second.end(); _Ite1++)
        {
            if (auto _pComponent = _Ite1->lock())
            {
                _Res.push_back(_pComponent);
            }
        }
        return _Res;
    }
    else
    {
        return {};
    }
}

//!< 获取上一帧缓存
std::vector<std::shared_ptr<iCAX::Database::CComponentBase>> iCAX::Database::CEntitiesView::GetPreEntities(IN const std::string& strClassName_)
{
    size_t _nCode = CMaskRegistry::GetComponentIndex(strClassName_);

    auto _Ite = m_PreCache.find(_nCode);
    if (_Ite != m_PreCache.end())
    {
        std::vector<std::shared_ptr<iCAX::Database::CComponentBase>> _Res;
        for (auto _Ite1 = _Ite->second.begin(); _Ite1 != _Ite->second.end(); _Ite1++)
        {
            if (auto _pComponent = _Ite1->lock())
            {
                _Res.push_back(_pComponent);
            }
        }
        return _Res;
    }
    return {};
}

//!< 备份
void iCAX::Database::CEntitiesView::RefreshPreCache()
{
    m_PreCache = m_Cache;
}

