#pragma once
#include "Database.h"
#include "ComponentBase.h"
#include "IEntity.h"
#include "IMetaRegistry.h"
#include "IRepository.h"
#include "IDomain.h"
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
        return iCAX::Database::GetGlobalMetaRegistry()->GetPropertyNames(S_ClassName); \
    } \
    virtual PropertyValue GetProperty(const std::string& strPropertyName_) const override \
    { \
        return iCAX::Database::GetGlobalMetaRegistry()->InvokeGetter(*this, S_ClassName, strPropertyName_); \
    } \
    virtual void OnSetProperty(const std::string& strPropertyName_, const PropertyValue& NewValue_) override \
    { \
        iCAX::Database::GetGlobalMetaRegistry()->InvokeSetter(*this, S_ClassName, strPropertyName_, NewValue_); \
    } \
private: \
    struct _AutoRegister_Type_##ClassName \
    { \
        _AutoRegister_Type_##ClassName() \
        { \
            auto registry = iCAX::Database::GetGlobalMetaRegistry(); \
            registry->RegistType( ClassName::S_ClassName, ParentClassName::S_ClassName); \
        } \
    }; \
    inline static _AutoRegister_Type_##ClassName s_autoRegister_Type_##ClassName{};

#define DECLARE_ICAX_COMPONENT_CREATOR(ClassName) \
private: \
    struct _AutoRegister_Component_Creator_##ClassName \
    { \
        _AutoRegister_Component_Creator_##ClassName() \
        { \
            auto registry = iCAX::Database::GetGlobalMetaRegistry(); \
            registry->RegistCreatorByName(ClassName::S_ClassName, [](std::shared_ptr<iCAX::Database::IEntity> entity) \
            { \
                return std::make_shared<ClassName>(entity); \
            }); \
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
            auto registry = iCAX::Database::GetGlobalMetaRegistry(); \
            registry->RegistPropertyByName(ownerType::S_ClassName, #name, \
                [](const void* obj) -> PropertyValue \
                { \
                    const auto* p = static_cast<const ownerType*>(obj); \
                    return (toVariantLambda)(p->m_##name); \
                }, \
                [](void* obj, const PropertyValue& val) \
                { \
                    auto* p = static_cast<ownerType*>(obj); \
                    p->Set##name(fromVariantLambda(val));\
                }); \
        } \
    }; \
    inline static _AutoRegister_Property_##name s_autoRegister_Property_##name{};

//! 声明运行时字段，该字段影响版本，但无需序列化
#define DECLARED_ICAX_VOLATILE_FIELD(type, name, defaultValue, equalLambda) \
private: \
    type m_##name = defaultValue; \
public:\
    static constexpr const char* PropertyName_##name = #name; /* 静态字符串变量 */ \
public: \
    const type& Get##name() const { return m_##name; } \
    void Set##name(const type& value) \
        { \
        if (!equalLambda(m_##name, value)) \
        { \
            m_##name = value; \
            if (auto pEntity = this->GetEntity()) \
            { \
                if (auto pDomain = pEntity->GetDomain()) \
                { \
                    if (auto pRepo = pDomain->GetRepository()) \
                    { \
                        pRepo->BumpComponentVersion(*this); \
                    } \
                } \
            } \
        } \
    }

//! 声明临时字段，不影响版本，也无需序列化
#define DECLARED_ICAX_NOVERSION_FIELD(type, name, defaultValue) \
public:\
    static constexpr const char* PropertyName_##name = #name; /* 静态字符串变量 */ \
public: \
    type m_##name = defaultValue;\
public:\
    const type& Get##name() const { return m_##name; } \
    void Set##name(const type& value) \
    { \
        m_##name = value; \
    }\




