#pragma once
#include "Database.h"
#include "ComponentBase.h"
#include "IEntity.h"
#include "IMetaRegistry.h"
#include "MetaRegistrationCatalog.h"
#include "IRepository.h"
#include "Data/Variant.h"

//! 声明类
#define DECLARE_ICAX_COMPONENT(ClassName, ParentClassName)\
public: \
    static constexpr const char* S_ClassName = #ClassName;\
public: \
    ClassName(std::shared_ptr<iCAX::Database::IEntity> pEntity_) : ParentClassName(pEntity_) {} \
    virtual ~ClassName() = default; \
public: \
    virtual std::string GetComponentClass() const override { return S_ClassName; } \
    virtual std::vector<std::string> GetPropertyNameArray() const override \
    { \
        return iCAX::Database::ResolveMetaRegistryForComponent(*this)->GetPropertyNames(S_ClassName); \
    } \
    virtual PropertyValue GetProperty(const std::string& strPropertyName_) const override \
    { \
        return iCAX::Database::ResolveMetaRegistryForComponent(*this)->InvokeGetter(*this, S_ClassName, strPropertyName_); \
    } \
    virtual void OnSetProperty(const std::string& strPropertyName_, const PropertyValue& NewValue_) override \
    { \
        iCAX::Database::ResolveMetaRegistryForComponent(*this)->InvokeSetter(*this, S_ClassName, strPropertyName_, NewValue_); \
    } \
private: \
    struct _AutoRegister_Type_##ClassName \
    { \
        _AutoRegister_Type_##ClassName() \
        { \
            iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry) \
            { \
                registry.RegistType( ClassName::S_ClassName, ParentClassName::S_ClassName); \
            }, this); \
        } \
    }; \
    inline static _AutoRegister_Type_##ClassName s_autoRegister_Type_##ClassName{};

#define DECLARE_ICAX_COMPONENT_CREATOR(ClassName) \
private: \
    struct _AutoRegister_Component_Creator_##ClassName \
    { \
        _AutoRegister_Component_Creator_##ClassName() \
        { \
            iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry) \
            { \
                registry.RegistCreatorByName(ClassName::S_ClassName, [](std::shared_ptr<iCAX::Database::IEntity> entity) \
                { \
                    return std::make_shared<ClassName>(entity); \
                }); \
            }, this); \
        } \
    }; \
    inline static _AutoRegister_Component_Creator_##ClassName s_autoRegister_Component_Creator_##ClassName{};

//! 声明序列化字段
#define DECLARED_ICAX_FIELD(ownerType, type, name, defaultValue, equalLambda, toVariantLambda, fromVariantLambda) \
private: \
    type m_##name = defaultValue; \
public:\
    static constexpr const char* PropertyName_##name = #name; /* 静态字符串变量 */ \
public: \
    const type& Get##name() const { return m_##name; } \
    void Set##name(const type& value) \
    { \
        if (!(equalLambda)(m_##name, value)) \
        { \
            iCAX::Data::PropertySet _pre = { {#name, (toVariantLambda)(m_##name)}};\
            iCAX::Data::PropertySet _new = { {#name, (toVariantLambda)(value)} };    \
            ComponentChangeNotifier _(this, iCAX::Database::ComponentEventArgs::kModifyComponent,_pre, _new); \
            m_##name = value; \
        } \
    } \
private: \
    struct _AutoRegister_Property_##name \
    { \
        _AutoRegister_Property_##name() \
        { \
            iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry) \
            { \
                registry.RegistPropertyByName(ownerType::S_ClassName, #name, \
                    [](const void* obj) -> PropertyValue \
                    { \
                        const auto* p = static_cast<const ownerType*>(obj); \
                        return (toVariantLambda)(p->m_##name); \
                    }, \
                    [](void* obj, const PropertyValue& val) \
                    { \
                        auto* p = static_cast<ownerType*>(obj); \
                        p->Set##name(fromVariantLambda(val));\
                    }, \
                    iCAX::Database::EPropertyPersistence::Persistent, \
                    iCAX::Database::EPropertyChangePolicy::Transactional); \
            }, this); \
        } \
    }; \
    inline static _AutoRegister_Property_##name s_autoRegister_Property_##name{};

//! 声明非持久化可观察字段，该字段影响版本并发布事件，但不参与持久化和撤销还原
#define DECLARED_ICAX_OBSERVABLE_FIELD(ownerType, type, name, defaultValue, equalLambda, toVariantLambda, fromVariantLambda) \
private: \
    type m_##name = defaultValue; \
public:\
    static constexpr const char* PropertyName_##name = #name; /* 静态字符串变量 */ \
public: \
    const type& Get##name() const { return m_##name; } \
    void Set##name(const type& value) \
    { \
        if (!(equalLambda)(m_##name, value)) \
        { \
            iCAX::Data::PropertySet _pre = { {#name, (toVariantLambda)(m_##name)}};\
            iCAX::Data::PropertySet _new = { {#name, (toVariantLambda)(value)} };    \
            ComponentChangeNotifier _(this, iCAX::Database::ComponentEventArgs::kModifyComponent, _pre, _new); \
            m_##name = value; \
        } \
    } \
private: \
    struct _AutoRegister_Property_##name \
    { \
        _AutoRegister_Property_##name() \
        { \
            iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry) \
            { \
                registry.RegistPropertyByName(ownerType::S_ClassName, #name, \
                    [](const void* obj) -> PropertyValue \
                    { \
                        const auto* p = static_cast<const ownerType*>(obj); \
                        return (toVariantLambda)(p->m_##name); \
                    }, \
                    [](void* obj, const PropertyValue& val) \
                    { \
                        auto* p = static_cast<ownerType*>(obj); \
                        p->Set##name(fromVariantLambda(val));\
                    }, \
                    iCAX::Database::EPropertyPersistence::NonPersistent, \
                    iCAX::Database::EPropertyChangePolicy::Observable); \
            }, this); \
        } \
    }; \
    inline static _AutoRegister_Property_##name s_autoRegister_Property_##name{};

//! 旧名保留：运行时字段应通过meta声明为非持久化可观察字段
#define DECLARED_ICAX_VOLATILE_FIELD(ownerType, type, name, defaultValue, equalLambda, toVariantLambda, fromVariantLambda) \
    DECLARED_ICAX_OBSERVABLE_FIELD(ownerType, type, name, defaultValue, equalLambda, toVariantLambda, fromVariantLambda)

//! 声明非持久化静默字段，该字段正常发布事件，但订阅者可按meta忽略版本和撤销还原
#define DECLARED_ICAX_SILENT_FIELD(ownerType, type, name, defaultValue, equalLambda, toVariantLambda, fromVariantLambda) \
private: \
    type m_##name = defaultValue; \
public:\
    static constexpr const char* PropertyName_##name = #name; /* 静态字符串变量 */ \
public: \
    const type& Get##name() const { return m_##name; } \
    void Set##name(const type& value) \
    { \
        if (!(equalLambda)(m_##name, value)) \
        { \
            iCAX::Data::PropertySet _pre = { {#name, (toVariantLambda)(m_##name)}};\
            iCAX::Data::PropertySet _new = { {#name, (toVariantLambda)(value)} };    \
            ComponentChangeNotifier _(this, iCAX::Database::ComponentEventArgs::kModifyComponent, _pre, _new); \
            m_##name = value; \
        } \
    } \
private: \
    struct _AutoRegister_Property_##name \
    { \
        _AutoRegister_Property_##name() \
        { \
            iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry) \
            { \
                registry.RegistPropertyByName(ownerType::S_ClassName, #name, \
                    [](const void* obj) -> PropertyValue \
                    { \
                        const auto* p = static_cast<const ownerType*>(obj); \
                        return (toVariantLambda)(p->m_##name); \
                    }, \
                    [](void* obj, const PropertyValue& val) \
                    { \
                        auto* p = static_cast<ownerType*>(obj); \
                        p->Set##name(fromVariantLambda(val));\
                    }, \
                    iCAX::Database::EPropertyPersistence::NonPersistent, \
                    iCAX::Database::EPropertyChangePolicy::Silent); \
            }, this); \
        } \
    }; \
    inline static _AutoRegister_Property_##name s_autoRegister_Property_##name{};

//! 声明运行时裸字段，不进入Property/Meta系统，不参与事件、版本、持久化和撤销还原
#define DECLARED_ICAX_RUNTIME_FIELD(type, name, defaultValue) \
public: \
    type m_##name = defaultValue;\
public:\
    const type& Get##name() const { return m_##name; } \
    void Set##name(const type& value) \
    { \
        m_##name = value; \
    }\

//! 旧名兼容：运行时裸字段
#define DECLARED_ICAX_NOVERSION_FIELD(type, name, defaultValue) \
    DECLARED_ICAX_RUNTIME_FIELD(type, name, defaultValue)

//! 声明派生字段，该字段只读，按需计算并由Database维护过期状态
#define DECLARED_ICAX_DERIVED_FIELD(ownerType, type, name, toVariantLambda, evaluatorLambda) \
public:\
    static constexpr const char* PropertyName_##name = #name; /* 静态字符串变量 */ \
public: \
    type Get##name() const \
    { \
        return GetProperty(#name).To<type>(); \
    } \
private: \
    struct _AutoRegister_Derived_Property_##name \
    { \
        _AutoRegister_Derived_Property_##name() \
        { \
            iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry) \
            { \
                registry.RegistDerivedPropertyByName(ownerType::S_ClassName, #name, \
                    [](iCAX::Database::CDerivedPropertyContext& context, const iCAX::Database::CComponentBase& component) -> PropertyValue \
                    { \
                        const auto* p = static_cast<const ownerType*>(&component); \
                        return (toVariantLambda)((evaluatorLambda)(*p, context)); \
                    }, \
                    iCAX::Database::EPropertyPersistence::NonPersistent, \
                    iCAX::Database::EPropertyChangePolicy::Silent); \
            }, this); \
        } \
    }; \
    inline static _AutoRegister_Derived_Property_##name s_autoRegister_Derived_Property_##name{};




