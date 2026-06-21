#include <gtest/gtest.h>

#include <Database/ComponentHelper.h>
#include <Database/IEntity.h>
#include <Database/IRepository.h>
#include <Data/uuid.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::Database;

    PropertyValue ToIntVariant(const int& Value_)
    {
        return PropertyValue(Value_);
    }

    int FromIntVariant(const PropertyValue& Value_)
    {
        return Value_.To<int>();
    }

    PropertyValue ToUuidVariant(const uuid& Value_)
    {
        return PropertyValue(Value_);
    }

    uuid FromUuidVariant(const PropertyValue& Value_)
    {
        return Value_.To<uuid>();
    }

    bool IntEqual(const int& Lhs_, const int& Rhs_)
    {
        return Lhs_ == Rhs_;
    }

    bool UuidEqual(const uuid& Lhs_, const uuid& Rhs_)
    {
        return Lhs_ == Rhs_;
    }

    class CSumComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CSumComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CSumComponent)

    public:
        inline static int EvaluationCount = 0;

        static void ResetEvaluationCount()
        {
            EvaluationCount = 0;
        }

        DECLARED_ICAX_FIELD(CSumComponent, int, A, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_FIELD(CSumComponent, int, B, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_DERIVED_FIELD(CSumComponent, int, Sum, ToIntVariant,
            [](const CSumComponent& Self_, CDerivedPropertyContext& Context_) -> int
            {
                ++CSumComponent::EvaluationCount;
                return Context_.GetProperty(Self_, CSumComponent::PropertyName_A).To<int>()
                    + Context_.GetProperty(Self_, CSumComponent::PropertyName_B).To<int>();
            })
    };

    class CChainComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CChainComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CChainComponent)

    public:
        inline static int EvaluationCount = 0;

        static void ResetEvaluationCount()
        {
            EvaluationCount = 0;
        }

        DECLARED_ICAX_FIELD(CChainComponent, int, Local, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_FIELD(CChainComponent, uuid, ParentID, uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_DERIVED_FIELD(CChainComponent, int, Total, ToIntVariant,
            [](const CChainComponent& Self_, CDerivedPropertyContext& Context_) -> int
            {
                ++CChainComponent::EvaluationCount;

                int _Total = Context_.GetProperty(Self_, CChainComponent::PropertyName_Local).To<int>();
                auto _ParentIDValue = Context_.GetProperty(Self_, CChainComponent::PropertyName_ParentID);
                if (!_ParentIDValue.Is<uuid>())
                {
                    return _Total;
                }

                auto _ParentID = _ParentIDValue.To<uuid>();
                if (_ParentID.is_nil())
                {
                    return _Total;
                }

                auto _pEntity = Self_.GetEntity();
                if (!_pEntity)
                {
                    return _Total;
                }

                auto _ParentTotal = Context_.GetProperty(_ParentID, CChainComponent::S_ClassName, CChainComponent::PropertyName_Total);
                if (!_ParentTotal.Is<int>())
                {
                    return _Total;
                }
                return _Total + _ParentTotal.To<int>();
            })
    };

    class CCycleComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CCycleComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CCycleComponent)

        DECLARED_ICAX_DERIVED_FIELD(CCycleComponent, int, A, ToIntVariant,
            [](const CCycleComponent& Self_, CDerivedPropertyContext& Context_) -> int
            {
                return Context_.GetProperty(Self_, CCycleComponent::PropertyName_B).To<int>();
            })
        DECLARED_ICAX_DERIVED_FIELD(CCycleComponent, int, B, ToIntVariant,
            [](const CCycleComponent& Self_, CDerivedPropertyContext& Context_) -> int
            {
                return Context_.GetProperty(Self_, CCycleComponent::PropertyName_A).To<int>();
            })
    };

    class CPolicyComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CPolicyComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CPolicyComponent)

        DECLARED_ICAX_FIELD(CPolicyComponent, int, PersistentValue, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_OBSERVABLE_FIELD(CPolicyComponent, int, RuntimeValue, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_SILENT_FIELD(CPolicyComponent, int, CacheValue, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_DERIVED_FIELD(CPolicyComponent, int, DerivedValue, ToIntVariant,
            [](const CPolicyComponent& Self_, CDerivedPropertyContext& Context_) -> int
            {
                return Context_.GetProperty(Self_, CPolicyComponent::PropertyName_PersistentValue).To<int>() * 2;
            })
    };

    class COrderedSizeComponent final : public CComponentBase
    {
    public:
        static constexpr const char* S_ClassName = "COrderedSizeComponent";
        static constexpr const char* PropertyName_Length = "Length";
        static constexpr const char* PropertyName_Width = "Width";

        explicit COrderedSizeComponent(std::shared_ptr<IEntity> pEntity_)
            : CComponentBase(pEntity_)
        {
        }

        std::string GetComponentClass() const override
        {
            return S_ClassName;
        }

        std::vector<std::string> GetPropertyNameArray() const override
        {
            return { PropertyName_Length, PropertyName_Width };
        }

        PropertyValue GetProperty(const std::string& strPropertyName_) const override
        {
            if (strPropertyName_ == PropertyName_Length)
            {
                return PropertyValue(m_nLength);
            }
            if (strPropertyName_ == PropertyName_Width)
            {
                return PropertyValue(m_nWidth);
            }
            return PropertyValue::Nil;
        }

        int GetLength() const
        {
            return m_nLength;
        }

        int GetWidth() const
        {
            return m_nWidth;
        }

        void SetLength(int nLength_)
        {
            SetProperty(PropertyName_Length, PropertyValue(nLength_));
        }

        void SetWidth(int nWidth_)
        {
            SetProperty(PropertyName_Width, PropertyValue(nWidth_));
        }

    protected:
        void OnSetProperty(const std::string& strPropertyName_, const PropertyValue& NewValue_) override
        {
            auto _nNextLength = m_nLength;
            auto _nNextWidth = m_nWidth;
            if (strPropertyName_ == PropertyName_Length)
            {
                _nNextLength = NewValue_.To<int>();
            }
            else if (strPropertyName_ == PropertyName_Width)
            {
                _nNextWidth = NewValue_.To<int>();
            }
            else
            {
                throw std::runtime_error("Unknown ordered size property");
            }

            if (_nNextLength <= _nNextWidth)
            {
                throw std::runtime_error("Length must be greater than width");
            }

            m_nLength = _nNextLength;
            m_nWidth = _nNextWidth;
        }

    private:
        struct _AutoRegister_Type
        {
            _AutoRegister_Type()
            {
                CMetaRegistrationCatalog::Register([](IMetaRegistry& Registry_)
                {
                    Registry_.RegistType(COrderedSizeComponent::S_ClassName, CComponentBase::S_ClassName);
                }, this);
            }
        };

        struct _AutoRegister_Creator
        {
            _AutoRegister_Creator()
            {
                CMetaRegistrationCatalog::Register([](IMetaRegistry& Registry_)
                {
                    Registry_.RegistCreatorByName(COrderedSizeComponent::S_ClassName, [](std::shared_ptr<IEntity> pEntity_)
                    {
                        return std::make_shared<COrderedSizeComponent>(pEntity_);
                    });
                }, this);
            }
        };

        struct _AutoRegister_Properties
        {
            _AutoRegister_Properties()
            {
                CMetaRegistrationCatalog::Register([](IMetaRegistry& Registry_)
                {
                    Registry_.RegistPropertyByName(
                        COrderedSizeComponent::S_ClassName,
                        COrderedSizeComponent::PropertyName_Length,
                        [](const void* pObject_) -> PropertyValue
                        {
                            return PropertyValue(static_cast<const COrderedSizeComponent*>(pObject_)->GetLength());
                        },
                        [](void* pObject_, const PropertyValue& Value_)
                        {
                            static_cast<COrderedSizeComponent*>(pObject_)->SetLength(Value_.To<int>());
                        });

                    Registry_.RegistPropertyByName(
                        COrderedSizeComponent::S_ClassName,
                        COrderedSizeComponent::PropertyName_Width,
                        [](const void* pObject_) -> PropertyValue
                        {
                            return PropertyValue(static_cast<const COrderedSizeComponent*>(pObject_)->GetWidth());
                        },
                        [](void* pObject_, const PropertyValue& Value_)
                        {
                            static_cast<COrderedSizeComponent*>(pObject_)->SetWidth(Value_.To<int>());
                        });
                }, this);
            }
        };

        inline static _AutoRegister_Type s_TypeRegistration{};
        inline static _AutoRegister_Creator s_CreatorRegistration{};
        inline static _AutoRegister_Properties s_PropertyRegistration{};

        int m_nLength = 10;
        int m_nWidth = 8;
    };

    class CMissingDependencyComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CMissingDependencyComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CMissingDependencyComponent)

        DECLARED_ICAX_FIELD(CMissingDependencyComponent, int, Local, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_DERIVED_FIELD(CMissingDependencyComponent, int, OptionalValue, ToIntVariant,
            [](const CMissingDependencyComponent& Self_, CDerivedPropertyContext& Context_) -> int
            {
                auto _Value = Context_.GetProperty(Self_, "Missing");
                return _Value.Is<int>() ? _Value.To<int>() : 42;
            })
    };

    class CSilentDependencyComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CSilentDependencyComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CSilentDependencyComponent)

        DECLARED_ICAX_SILENT_FIELD(CSilentDependencyComponent, int, Cache, 0, IntEqual, ToIntVariant, FromIntVariant)
        DECLARED_ICAX_DERIVED_FIELD(CSilentDependencyComponent, int, CachedValue, ToIntVariant,
            [](const CSilentDependencyComponent& Self_, CDerivedPropertyContext& Context_) -> int
            {
                return Context_.GetProperty(Self_, CSilentDependencyComponent::PropertyName_Cache).To<int>();
            })
    };

    class CBaseAssignableComponent : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CBaseAssignableComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CBaseAssignableComponent)

        DECLARED_ICAX_FIELD(CBaseAssignableComponent, int, Value, 0, IntEqual, ToIntVariant, FromIntVariant)
    };

    class CDerivedAssignableComponent final : public CBaseAssignableComponent
    {
        DECLARE_ICAX_COMPONENT(CDerivedAssignableComponent, CBaseAssignableComponent)
        DECLARE_ICAX_COMPONENT_CREATOR(CDerivedAssignableComponent)
    };

    class CManualCreatorComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CManualCreatorComponent, CComponentBase)
    };

    class CUnwindMutationComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CUnwindMutationComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CUnwindMutationComponent)

        DECLARED_ICAX_FIELD(CUnwindMutationComponent, int, Value, 0, IntEqual, ToIntVariant, FromIntVariant)
    };

    class CThrowingNotifierComponent final : public CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CThrowingNotifierComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CThrowingNotifierComponent)

    public:
        void SetValueAndThrow(int Value_)
        {
            iCAX::Data::PropertySet _Previous = { { "Value", iCAX::Data::PropertyValue(m_Value) } };
            iCAX::Data::PropertySet _New = { { "Value", iCAX::Data::PropertyValue(Value_) } };
            ComponentChangeNotifier _Notifier(this, iCAX::Database::ComponentEventArgs::kModifyComponent, _Previous, _New);
            throw std::runtime_error("setter failed");
        }

    private:
        int m_Value = 0;
    };

    class CPartialThrowingComponent final : public CComponentBase
    {
    public:
        static constexpr const char* S_ClassName = "CPartialThrowingComponent";

        CPartialThrowingComponent()
            : CComponentBase(nullptr)
        {
        }

        std::string GetComponentClass() const override
        {
            return S_ClassName;
        }

        std::vector<std::string> GetPropertyNameArray() const override
        {
            return { "A", "B" };
        }

        PropertyValue GetProperty(const std::string& strPropertyName_) const override
        {
            if (strPropertyName_ == "A")
            {
                return PropertyValue(A);
            }
            if (strPropertyName_ == "B")
            {
                return PropertyValue(B);
            }
            return PropertyValue::Nil;
        }

        void OnSetProperty(const std::string& strPropertyName_, const PropertyValue& NewValue_) override
        {
            if (strPropertyName_ == "A")
            {
                A = NewValue_.To<int>();
                return;
            }
            if (strPropertyName_ == "B")
            {
                throw std::runtime_error("B failed");
            }
        }

        int A = 0;
        int B = 0;
    };

    class CTestAttribute final : public IAttribute
    {
    };

    class CGlobalAttribute final : public IAttribute
    {
    };

    class CAlwaysAllowChecker final : public IChecker
    {
    public:
        bool AllowAttachByName(const IEntity& Entity_, const std::string& strComponent_, std::string& strError_) override
        {
            return true;
        }

        bool AllowRemoveByName(const IEntity& Entity_, const std::string& strComponent_, std::string& strError_) override
        {
            return true;
        }

        bool AllowModify(const CComponentBase& Component_, const PropertySet& Properties_, std::string& strError_) override
        {
            return true;
        }
    };

    class CGlobalAllowChecker final : public IChecker
    {
    public:
        bool AllowAttachByName(const IEntity& Entity_, const std::string& strComponent_, std::string& strError_) override
        {
            return true;
        }

        bool AllowRemoveByName(const IEntity& Entity_, const std::string& strComponent_, std::string& strError_) override
        {
            return true;
        }

        bool AllowModify(const CComponentBase& Component_, const PropertySet& Properties_, std::string& strError_) override
        {
            return true;
        }
    };

    class CRepositoryEventCollector final : public IRepositoryEventListener
    {
    public:
        std::vector<RepositoryEventArgs> ChangingEvents;
        std::vector<RepositoryEventArgs> ChangedEvents;

        void OnRepositoryChanging(void* pSender_, const RepositoryEventArgs& Args_) override
        {
            ChangingEvents.push_back(Args_);
        }

        void OnRepositoryChanged(void* pSender_, const RepositoryEventArgs& Args_) override
        {
            ChangedEvents.push_back(Args_);
        }

        void Clear()
        {
            ChangingEvents.clear();
            ChangedEvents.clear();
        }
    };

    class CAddComponentChangedMutator final : public IRepositoryEventListener
    {
    public:
        void OnRepositoryChanging(void* pSender_, const RepositoryEventArgs& Args_) override
        {
        }

        void OnRepositoryChanged(void* pSender_, const RepositoryEventArgs& Args_) override
        {
            if (!DidMutate && Args_.nType == RepositoryEventArgs::kAddComponent && Args_.strClassName == CSumComponent::S_ClassName)
            {
                DidMutate = true;
                if (auto _Component = std::dynamic_pointer_cast<CSumComponent>(Args_.pComponent))
                {
                    _Component->SetA(7);
                }
            }
        }

        bool DidMutate = false;
    };

    class CAddEntityChangedMutator final : public IRepositoryEventListener
    {
    public:
        void OnRepositoryChanging(void* pSender_, const RepositoryEventArgs& Args_) override
        {
        }

        void OnRepositoryChanged(void* pSender_, const RepositoryEventArgs& Args_) override
        {
            if (!DidMutate && Args_.nType == RepositoryEventArgs::kAddEntity && Args_.pEntity)
            {
                DidMutate = true;
                Component = Args_.pEntity->AddComponent<CSumComponent>();
                Component->SetA(5);
            }
        }

        bool DidMutate = false;
        std::shared_ptr<CSumComponent> Component;
    };

    class CRemoveChangingMutator final : public IRepositoryEventListener
    {
    public:
        void OnRepositoryChanging(void* pSender_, const RepositoryEventArgs& Args_) override
        {
            if (Args_.nType == RepositoryEventArgs::kRemoveComponent && Args_.strClassName == CSumComponent::S_ClassName)
            {
                if (auto _Component = std::dynamic_pointer_cast<CSumComponent>(Args_.pComponent))
                {
                    _Component->SetA(99);
                }
            }
        }

        void OnRepositoryChanged(void* pSender_, const RepositoryEventArgs& Args_) override
        {
        }
    };

    std::shared_ptr<IRepository> GetRepositorySpace(const std::shared_ptr<IRepository>& Repository_)
    {
        return Repository_;
    }

    std::string MakeTempOperationLogPath()
    {
        const auto _FileName = std::string("icax_database_") + to_string(GenerateNewUUID()) + ".journal";
        return (std::filesystem::temp_directory_path() / _FileName).string();
    }
}

TEST(DatabaseDerivedPropertyTest, DerivedPropertyIsLazyAndCached)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    CSumComponent::ResetEvaluationCount();

    _Component->SetA(2);
    _Component->SetB(3);

    EXPECT_EQ(5, _Component->GetSum());
    EXPECT_EQ(1, CSumComponent::EvaluationCount);

    EXPECT_EQ(5, _Component->GetSum());
    EXPECT_EQ(1, CSumComponent::EvaluationCount);
}

TEST(DatabaseDerivedPropertyTest, SourceFieldChangeInvalidatesDerivedProperty)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    CSumComponent::ResetEvaluationCount();

    _Component->SetA(2);
    _Component->SetB(3);
    EXPECT_EQ(5, _Component->GetSum());
    EXPECT_EQ(1, CSumComponent::EvaluationCount);

    _Component->SetA(4);

    EXPECT_EQ(7, _Component->GetSum());
    EXPECT_EQ(2, CSumComponent::EvaluationCount);
}

TEST(DatabaseDerivedPropertyTest, DerivedPropertyCannotBeSet)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    EXPECT_THROW(_Component->SetProperty(CSumComponent::PropertyName_Sum, PropertyValue(10)), std::runtime_error);
}

TEST(DatabaseDerivedPropertyTest, GetPropertiesExcludesDerivedProperties)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    _Component->SetA(2);
    _Component->SetB(3);
    EXPECT_EQ(5, _Component->GetSum());

    auto _Properties = _Component->GetProperties();
    EXPECT_TRUE(_Properties.contains(CSumComponent::PropertyName_A));
    EXPECT_TRUE(_Properties.contains(CSumComponent::PropertyName_B));
    EXPECT_FALSE(_Properties.contains(CSumComponent::PropertyName_Sum));
}

TEST(DatabaseDerivedPropertyTest, SetPropertyBumpsComponentVersionOnlyOnce)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    const auto _InitialVersion = _Component->Version();

    _Component->SetProperty(CSumComponent::PropertyName_A, PropertyValue(10));

    EXPECT_EQ(_InitialVersion + 1, _Component->Version());
}

TEST(DatabaseDerivedPropertyTest, DynamicCrossEntityDependenciesAreReplaced)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);

    auto _Parent1Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Parent1 = _Parent1Entity->AddComponent<CChainComponent>();
    _Parent1->SetLocal(10);

    auto _Parent2Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Parent2 = _Parent2Entity->AddComponent<CChainComponent>();
    _Parent2->SetLocal(100);

    auto _ChildEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _Child = _ChildEntity->AddComponent<CChainComponent>();
    _Child->SetLocal(1);
    _Child->SetParentID(_Parent1Entity->GetID());

    EXPECT_EQ(11, _Child->GetTotal());

    _Child->SetParentID(_Parent2Entity->GetID());
    CChainComponent::ResetEvaluationCount();
    EXPECT_EQ(101, _Child->GetTotal());
    const int _CountAfterReparent = CChainComponent::EvaluationCount;

    _Parent1->SetLocal(20);
    EXPECT_EQ(101, _Child->GetTotal());
    EXPECT_EQ(_CountAfterReparent, CChainComponent::EvaluationCount);

    _Parent2->SetLocal(200);
    EXPECT_EQ(201, _Child->GetTotal());
    EXPECT_GT(CChainComponent::EvaluationCount, _CountAfterReparent);
}

TEST(DatabaseDerivedPropertyTest, MissingComponentDependencyInvalidatesWhenComponentIsAdded)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);

    auto _ParentEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _ChildEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _Child = _ChildEntity->AddComponent<CChainComponent>();
    _Child->SetLocal(1);
    _Child->SetParentID(_ParentEntity->GetID());

    CChainComponent::ResetEvaluationCount();
    EXPECT_EQ(1, _Child->GetTotal());
    EXPECT_EQ(1, CChainComponent::EvaluationCount);

    auto _Parent = _ParentEntity->AddComponent<CChainComponent>();
    _Parent->SetLocal(10);

    EXPECT_EQ(11, _Child->GetTotal());
    EXPECT_GT(CChainComponent::EvaluationCount, 1);
}

TEST(DatabaseDerivedPropertyTest, MissingEntityDependencyInvalidatesWhenComponentIsAddedLater)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);

    auto _ParentEntityID = GenerateNewUUID();
    auto _ChildEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _Child = _ChildEntity->AddComponent<CChainComponent>();
    _Child->SetLocal(1);
    _Child->SetParentID(_ParentEntityID);

    CChainComponent::ResetEvaluationCount();
    EXPECT_EQ(1, _Child->GetTotal());
    EXPECT_EQ(1, CChainComponent::EvaluationCount);

    auto _ParentEntity = _Space->CreateEntity(_ParentEntityID);
    auto _Parent = _ParentEntity->AddComponent<CChainComponent>();
    _Parent->SetLocal(10);

    EXPECT_EQ(11, _Child->GetTotal());
    EXPECT_GT(CChainComponent::EvaluationCount, 1);
}

TEST(DatabaseDerivedPropertyTest, CircularDerivedDependencyThrows)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CCycleComponent>();

    EXPECT_THROW(_Component->GetA(), std::runtime_error);
}

TEST(DatabaseDerivedPropertyTest, MissingSourcePropertyDoesNotPreventDerivedEvaluation)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CMissingDependencyComponent>();

    EXPECT_EQ(42, _Component->GetOptionalValue());
}

TEST(DatabaseDerivedPropertyTest, DerivedPropertyCannotDependOnSilentValueProperty)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSilentDependencyComponent>();

    EXPECT_THROW(_Component->GetCachedValue(), std::runtime_error);
}

TEST(DatabasePropertyMetaTest, FieldDeclarationsRegisterPersistenceAndChangePolicy)
{
    auto _Meta = GetGlobalMetaRegistry();

    EXPECT_EQ(EPropertyKind::Value, _Meta->GetPropertyKindByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_PersistentValue));
    EXPECT_EQ(EPropertyPersistence::Persistent, _Meta->GetPropertyPersistenceByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_PersistentValue));
    EXPECT_EQ(EPropertyChangePolicy::Transactional, _Meta->GetPropertyChangePolicyByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_PersistentValue));

    EXPECT_EQ(EPropertyKind::Value, _Meta->GetPropertyKindByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_RuntimeValue));
    EXPECT_EQ(EPropertyPersistence::NonPersistent, _Meta->GetPropertyPersistenceByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_RuntimeValue));
    EXPECT_EQ(EPropertyChangePolicy::Observable, _Meta->GetPropertyChangePolicyByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_RuntimeValue));

    EXPECT_EQ(EPropertyKind::Value, _Meta->GetPropertyKindByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_CacheValue));
    EXPECT_EQ(EPropertyPersistence::NonPersistent, _Meta->GetPropertyPersistenceByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_CacheValue));
    EXPECT_EQ(EPropertyChangePolicy::Silent, _Meta->GetPropertyChangePolicyByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_CacheValue));

    EXPECT_EQ(EPropertyKind::Derived, _Meta->GetPropertyKindByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_DerivedValue));
    EXPECT_EQ(EPropertyPersistence::NonPersistent, _Meta->GetPropertyPersistenceByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_DerivedValue));
    EXPECT_EQ(EPropertyChangePolicy::Silent, _Meta->GetPropertyChangePolicyByName(CPolicyComponent::S_ClassName, CPolicyComponent::PropertyName_DerivedValue));

    const auto _Names = _Meta->GetPropertyNames(CPolicyComponent::S_ClassName);
    ASSERT_EQ(4, _Names.size());
    EXPECT_EQ(CPolicyComponent::PropertyName_PersistentValue, _Names[0]);
    EXPECT_EQ(CPolicyComponent::PropertyName_RuntimeValue, _Names[1]);
    EXPECT_EQ(CPolicyComponent::PropertyName_CacheValue, _Names[2]);
    EXPECT_EQ(CPolicyComponent::PropertyName_DerivedValue, _Names[3]);
}

TEST(DatabasePropertyMetaTest, UnknownPropertyMetaMustBeExplicitlyHandled)
{
    auto _Meta = GetGlobalMetaRegistry();

    EXPECT_TRUE(_Meta->HasTypeByName(CPolicyComponent::S_ClassName));
    EXPECT_FALSE(_Meta->HasPropertyByName(CPolicyComponent::S_ClassName, "Missing"));
    EXPECT_THROW(_Meta->GetPropertyKindByName(CPolicyComponent::S_ClassName, "Missing"), std::runtime_error);
    EXPECT_THROW(_Meta->GetPropertyPersistenceByName(CPolicyComponent::S_ClassName, "Missing"), std::runtime_error);
    EXPECT_THROW(_Meta->GetPropertyChangePolicyByName(CPolicyComponent::S_ClassName, "Missing"), std::runtime_error);
}

TEST(DatabasePropertyMetaTest, ObservableFieldRaisesEventAndBumpsVersionWithoutTransactionalPolicy)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Observer->Clear();

    const auto _InitialVersion = _Component->Version();

    _Component->SetRuntimeValue(10);

    EXPECT_EQ(_InitialVersion + 1, _Component->Version());
    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(RepositoryEventArgs::kModifyComponent, _Observer->ChangedEvents.back().nType);
    EXPECT_EQ(EPropertyChangePolicy::Observable,
        GetGlobalMetaRegistry()->GetPropertyChangePolicyByName(_Observer->ChangedEvents.back().strClassName, CPolicyComponent::PropertyName_RuntimeValue));
    EXPECT_EQ(10, _Observer->ChangedEvents.back().NewProperties.at(CPolicyComponent::PropertyName_RuntimeValue).To<int>());
}

TEST(DatabasePropertyMetaTest, SilentFieldRaisesNormalEventButDoesNotBumpVersion)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Observer->Clear();

    const auto _InitialVersion = _Component->Version();

    _Component->SetCacheValue(10);
    EXPECT_EQ(10, _Component->GetCacheValue());
    EXPECT_EQ(_InitialVersion, _Component->Version());
    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(EPropertyChangePolicy::Silent,
        GetGlobalMetaRegistry()->GetPropertyChangePolicyByName(_Observer->ChangedEvents.back().strClassName, CPolicyComponent::PropertyName_CacheValue));

    _Observer->Clear();
    _Component->SetProperty(CPolicyComponent::PropertyName_CacheValue, PropertyValue(20));
    EXPECT_EQ(20, _Component->GetCacheValue());
    EXPECT_EQ(_InitialVersion, _Component->Version());
    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(EPropertyChangePolicy::Silent,
        GetGlobalMetaRegistry()->GetPropertyChangePolicyByName(_Observer->ChangedEvents.back().strClassName, CPolicyComponent::PropertyName_CacheValue));
}

TEST(DatabaseRepositoryTest, RepositoryIsInitialized)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);

    EXPECT_TRUE(_Space->IsValid());
    ASSERT_NE(nullptr, _Space->GetMetaEntity());
    EXPECT_EQ(_Repository->GetID(), _Space->GetMetaEntity()->GetID());
    EXPECT_EQ(1, _Space->EntityCount());
    EXPECT_NO_THROW(_Space->GetView().BuildCache(CSumComponent::S_ClassName, true));
}

TEST(DatabaseRepositoryTest, DuplicateEntityThrowsRuntimeError)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _EntityID = GenerateNewUUID();

    _Space->CreateEntity(_EntityID);

    EXPECT_THROW(_Space->CreateEntity(_EntityID), std::runtime_error);
}

TEST(DatabaseRepositoryTest, UserCommandScopeEmitsOneMergedChangeSet)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    auto _Space = GetRepositorySpace(_Repository);
    auto _ExistingEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _ExistingComponent = _ExistingEntity->AddComponent<CSumComponent>();
    const auto _ExistingVersion = _ExistingComponent->Version();
    _Observer->Clear();

    auto _Scope = _Repository->BeginChangeScope(EChangeScopeKind::UserCommand, "InsertProject");
    auto _NewEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _NewComponent = _NewEntity->AddComponent<CSumComponent>();
    _NewComponent->SetA(1);
    _NewComponent->SetA(2);
    _NewComponent->SetB(3);
    _ExistingComponent->SetA(10);
    _ExistingComponent->SetA(20);
    _Scope->Commit();

    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(RepositoryEventArgs::kBatchChanged, _Observer->ChangedEvents.front().nType);
    ASSERT_NE(nullptr, _Observer->ChangedEvents.front().pChangeSet);

    const auto& _ChangeSet = *_Observer->ChangedEvents.front().pChangeSet;
    EXPECT_EQ(EChangeScopeKind::UserCommand, _ChangeSet.Kind);
    EXPECT_EQ("InsertProject", _ChangeSet.Name);
    ASSERT_EQ(1, _ChangeSet.CreatedEntities.size());
    EXPECT_EQ(_NewEntity->GetID(), _ChangeSet.CreatedEntities.front().Key.EntityID);

    ASSERT_EQ(1, _ChangeSet.AddedComponents.size());
    EXPECT_EQ(_NewEntity->GetID(), _ChangeSet.AddedComponents.front().Key.EntityID);
    EXPECT_EQ(2, _ChangeSet.AddedComponents.front().NewProperties.at(CSumComponent::PropertyName_A).To<int>());
    EXPECT_EQ(3, _ChangeSet.AddedComponents.front().NewProperties.at(CSumComponent::PropertyName_B).To<int>());

    ASSERT_EQ(1, _ChangeSet.ModifiedProperties.size());
    EXPECT_EQ(_ExistingEntity->GetID(), _ChangeSet.ModifiedProperties.front().Key.EntityID);
    EXPECT_EQ(CSumComponent::PropertyName_A, _ChangeSet.ModifiedProperties.front().Key.PropertyName);
    EXPECT_EQ(0, _ChangeSet.ModifiedProperties.front().PreviousValue.To<int>());
    EXPECT_EQ(20, _ChangeSet.ModifiedProperties.front().NewValue.To<int>());

    EXPECT_EQ(1, _NewComponent->Version());
    EXPECT_EQ(_ExistingVersion + 1, _ExistingComponent->Version());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, LoadBaselineScopeDoesNotNotifyOrMarkDirty)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    auto _Space = GetRepositorySpace(_Repository);
    _Observer->Clear();

    std::shared_ptr<CSumComponent> _Component;
    auto _Scope = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "LoadProjectBaseline");
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(100);
    _Scope->Commit();

    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_EQ(100, _Component->GetA());
    EXPECT_EQ(0, _Component->Version());
    EXPECT_FALSE(_Component->IsChanged());
}

TEST(DatabaseRepositoryTest, UserCommandScopeCancelsTransientComponent)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    _Observer->Clear();

    auto _Scope = _Repository->BeginChangeScope(EChangeScopeKind::UserCommand, "Transient");
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Entity->RemoveComponent<CSumComponent>();
    _Scope->Commit();

    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_FALSE(_Entity->HasComponent<CSumComponent>());
}

TEST(DatabaseRepositoryTest, TransactionCancelRollsBackSilently)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(5);
    _Baseline->Commit();
    _Observer->Clear();

    const auto _TransientEntityID = GenerateNewUUID();
    auto _Transaction = _Repository->BeginTransaction("Rollback");
    _Space->CreateEntity(_TransientEntityID);
    _Component->SetA(9);
    _Transaction->Cancel();

    EXPECT_FALSE(_Space->HasEntity(_TransientEntityID));
    EXPECT_EQ(5, _Component->GetA());
    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, TransactionCancelReplacementRestoresOriginalState)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    const auto _EntityID = GenerateNewUUID();

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(_EntityID);
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(8);
    _Component->SetB(9);
    _Baseline->Commit();
    _Observer->Clear();

    auto _Transaction = _Repository->BeginTransaction("CancelReplaceEntity");
    _Space->DeleteEntity(_EntityID);
    auto _ReplacementEntity = _Space->CreateEntity(_EntityID);
    auto _ReplacementComponent = _ReplacementEntity->AddComponent<CSumComponent>();
    _ReplacementComponent->SetA(80);
    _ReplacementComponent->SetB(90);
    _Transaction->Cancel();

    ASSERT_TRUE(_Space->HasEntity(_EntityID));
    auto _RestoredComponent = _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RestoredComponent);
    EXPECT_EQ(8, _RestoredComponent->GetA());
    EXPECT_EQ(9, _RestoredComponent->GetB());
    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, TransactionCommitWritesOperationLogButDoesNotCreateUndoStep)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _Repository = GenerateRepository(GenerateNewUUID());
        _Repository->OpenOperationLog(_LogPath, true);

        auto _Transaction = _Repository->BeginTransaction("CreateRuntimeTransaction");
        auto _Space = GetRepositorySpace(_Repository);
        auto _Entity = _Space->CreateEntity(_EntityID);
        auto _Component = _Entity->AddComponent<CSumComponent>();
        _Component->SetA(11);
        _Transaction->Commit();

        EXPECT_FALSE(_Repository->CanUndo());
        _Repository->CloseOperationLog();
    }

    auto _RecoveredRepository = GenerateRepository(GenerateNewUUID());
    _RecoveredRepository->ReplayOperationLog(_LogPath);

    auto _RecoveredEntity = _RecoveredRepository->GetEntity(_EntityID);
    ASSERT_NE(nullptr, _RecoveredEntity);
    auto _RecoveredComponent = _RecoveredEntity->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RecoveredComponent);
    EXPECT_EQ(11, _RecoveredComponent->GetA());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, TransactionInsideUndoCommandCreatesUndoStep)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("TransactionUndo");
    auto _Transaction = _Repository->BeginTransaction("SetA");
    _Component->SetA(12);
    _Transaction->Commit();
    _UndoCommand->End();

    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetA());
    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(12, _Component->GetA());
}

TEST(DatabaseRepositoryTest, BatchInsideUndoCommandCreatesUndoStep)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("BatchUndo");
    auto _Batch = _Repository->BeginChangeScope(EChangeScopeKind::UserCommand, "BatchSet");
    _Component->SetA(20);
    _Component->SetB(30);
    _Batch->Commit();
    _UndoCommand->End();

    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetA());
    EXPECT_EQ(0, _Component->GetB());
    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(20, _Component->GetA());
    EXPECT_EQ(30, _Component->GetB());
}

TEST(DatabaseRepositoryTest, UndoRedoUsesCommittedChangeSet)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetA");
    _Component->SetA(10);
    _UndoCommand->End();

    EXPECT_EQ(10, _Component->GetA());
    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_FALSE(_Repository->CanRedo());

    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetA());
    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->CanRedo());

    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(10, _Component->GetA());
    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_FALSE(_Repository->CanRedo());
}

TEST(DatabaseRepositoryTest, UndoRedoReplaysOperationsInOriginalOrder)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    auto _Component = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<COrderedSizeComponent>();
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("Resize");
    _Component->SetWidth(5);
    _Component->SetLength(6);
    _UndoCommand->End();

    EXPECT_EQ(6, _Component->GetLength());
    EXPECT_EQ(5, _Component->GetWidth());

    ASSERT_TRUE(_Repository->Undo());
    EXPECT_EQ(10, _Component->GetLength());
    EXPECT_EQ(8, _Component->GetWidth());

    ASSERT_TRUE(_Repository->Redo());
    EXPECT_EQ(6, _Component->GetLength());
    EXPECT_EQ(5, _Component->GetWidth());
}

TEST(DatabaseRepositoryTest, RepositoryUndoUsesSingleLinearHistory)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    auto _ComponentA = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    auto _ComponentB = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    _ComponentA->SetA(1);
    _ComponentB->SetA(2);
    _Baseline->Commit();

    auto _SharedCommand = _Repository->BeginUndoCommand("MoveBoth");
    _ComponentA->SetA(10);
    _ComponentB->SetA(20);
    _SharedCommand->End();

    EXPECT_TRUE(_Repository->CanUndo());

    auto _BCommand = _Repository->BeginUndoCommand("MoveB");
    _ComponentB->SetA(30);
    _BCommand->End();

    ASSERT_EQ(2, _Repository->GetUndoArray().size());

    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(20, _ComponentB->GetA());
    EXPECT_EQ(10, _ComponentA->GetA());

    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _ComponentA->GetA());
    EXPECT_EQ(2, _ComponentB->GetA());

    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(10, _ComponentA->GetA());
    EXPECT_EQ(20, _ComponentB->GetA());

    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(30, _ComponentB->GetA());
}

TEST(DatabaseRepositoryTest, UndoCommandRecordsOnlyTransactionalFields)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Component->SetPersistentValue(1);
    _Component->SetRuntimeValue(2);
    _Component->SetCacheValue(3);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("MixedPolicy");
    _Component->SetPersistentValue(10);
    _Component->SetRuntimeValue(20);
    _Component->SetCacheValue(30);
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetPersistentValue());
    EXPECT_EQ(20, _Component->GetRuntimeValue());
    EXPECT_EQ(30, _Component->GetCacheValue());

    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(10, _Component->GetPersistentValue());
    EXPECT_EQ(20, _Component->GetRuntimeValue());
    EXPECT_EQ(30, _Component->GetCacheValue());
}

TEST(DatabaseRepositoryTest, UndoCommandWithOnlyRuntimeFieldsCreatesNoStep)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("RuntimeOnly");
    _Component->SetRuntimeValue(20);
    _Component->SetCacheValue(30);
    _UndoCommand->End();

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->GetUndoArray().empty());
}

TEST(DatabaseRepositoryTest, LoadBaselineClearsUndoRedoHistory)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetA");
    _Component->SetA(2);
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    ASSERT_TRUE(_Repository->CanRedo());

    auto _Reload = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Reload");
    _Component->SetA(3);
    _Reload->Commit();

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_FALSE(_Repository->CanRedo());
    EXPECT_TRUE(_Repository->GetUndoArray().empty());
    EXPECT_TRUE(_Repository->GetRedoArray().empty());
}

TEST(DatabaseRepositoryTest, UndoHistoryLimitRemovesOldestRepositoryStep)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    auto _ComponentA = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    auto _ComponentB = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    _ComponentA->SetA(1);
    _ComponentB->SetA(2);
    _Baseline->Commit();

    auto _SharedCommand = _Repository->BeginUndoCommand("MoveBoth");
    _ComponentA->SetA(10);
    _ComponentB->SetA(20);
    _SharedCommand->End();

    ASSERT_EQ(1, _Repository->GetUndoArray().size());

    for (int _Index = 0; _Index < 40; ++_Index)
    {
        auto _Command = _Repository->BeginUndoCommand("MoveB");
        _ComponentB->SetA(100 + _Index);
        _Command->End();
    }

    auto _UndoArray = _Repository->GetUndoArray();
    ASSERT_EQ(40, _UndoArray.size());
    EXPECT_TRUE(_Repository->CanUndo());

    for (int _Index = 0; _Index < 40; ++_Index)
    {
        ASSERT_TRUE(_Repository->Undo());
    }

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_EQ(10, _ComponentA->GetA());
    EXPECT_EQ(20, _ComponentB->GetA());
}

TEST(DatabaseRepositoryTest, NonUndoableTransactionClearsRepositoryHistory)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Component = _Space->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("UndoableSetA");
    _Component->SetA(2);
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    auto _Transaction = _Repository->BeginTransaction("NonUndoableSetA");
    _Component->SetA(3);
    _Transaction->Commit();

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->GetUndoArray().empty());
    EXPECT_EQ(3, _Component->GetA());
}

TEST(DatabaseRepositoryTest, NonUndoableChangeClearsRepositoryHistory)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _SpaceA;
    std::shared_ptr<IRepository> _SpaceB;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _SpaceA = GetRepositorySpace(_Repository);
    _SpaceB = GetRepositorySpace(_Repository);
    auto _ComponentA = _SpaceA->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    auto _ComponentB = _SpaceB->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    _ComponentA->SetA(1);
    _ComponentB->SetA(2);
    _Baseline->Commit();

    auto _SharedCommand = _Repository->BeginUndoCommand("SharedMove");
    _ComponentA->SetA(10);
    _ComponentB->SetA(20);
    _SharedCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    _ComponentB->SetA(30);

    EXPECT_TRUE(_Repository->GetUndoArray().empty());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, ObservableChangeDoesNotClearUndoHistory)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Component = _Space->CreateEntity(GenerateNewUUID())->AddComponent<CPolicyComponent>();
    _Component->SetPersistentValue(1);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetPersistent");
    _Component->SetPersistentValue(2);
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    _Component->SetRuntimeValue(30);
    _Component->SetCacheValue(40);

    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetPersistentValue());
    EXPECT_EQ(30, _Component->GetRuntimeValue());
    EXPECT_EQ(40, _Component->GetCacheValue());
}

TEST(DatabaseRepositoryTest, UndoRestoresDeletedEntityComponents)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    const auto _EntityID = GenerateNewUUID();

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(_EntityID);
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(4);
    _Component->SetB(5);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("DeleteEntity");
    _Space->DeleteEntity(_EntityID);
    _UndoCommand->End();

    EXPECT_FALSE(_Space->HasEntity(_EntityID));
    ASSERT_TRUE(_Repository->CanUndo());

    EXPECT_TRUE(_Repository->Undo());
    ASSERT_TRUE(_Space->HasEntity(_EntityID));
    auto _RestoredComponent = _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RestoredComponent);
    EXPECT_EQ(4, _RestoredComponent->GetA());
    EXPECT_EQ(5, _RestoredComponent->GetB());

    EXPECT_TRUE(_Repository->Redo());
    EXPECT_FALSE(_Space->HasEntity(_EntityID));
}

TEST(DatabaseRepositoryTest, UndoReplaceComponentRestoresOriginalComponent)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    std::shared_ptr<IEntity> _Entity;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _OriginalComponent = _Entity->AddComponent<CSumComponent>();
    _OriginalComponent->SetA(1);
    _OriginalComponent->SetB(2);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("ReplaceComponent");
    _Entity->RemoveComponent<CSumComponent>();
    auto _ReplacementComponent = _Entity->AddComponent<CSumComponent>();
    _ReplacementComponent->SetA(10);
    _ReplacementComponent->SetB(20);
    _UndoCommand->End();

    ASSERT_TRUE(_Entity->HasComponent<CSumComponent>());
    EXPECT_EQ(10, _Entity->GetComponent<CSumComponent>()->GetA());
    EXPECT_EQ(20, _Entity->GetComponent<CSumComponent>()->GetB());

    EXPECT_TRUE(_Repository->Undo());
    ASSERT_TRUE(_Entity->HasComponent<CSumComponent>());
    EXPECT_EQ(1, _Entity->GetComponent<CSumComponent>()->GetA());
    EXPECT_EQ(2, _Entity->GetComponent<CSumComponent>()->GetB());

    EXPECT_TRUE(_Repository->Redo());
    ASSERT_TRUE(_Entity->HasComponent<CSumComponent>());
    EXPECT_EQ(10, _Entity->GetComponent<CSumComponent>()->GetA());
    EXPECT_EQ(20, _Entity->GetComponent<CSumComponent>()->GetB());
}

TEST(DatabaseRepositoryTest, UndoRecreateSameEntityIDRestoresOriginalEntityContents)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    const auto _EntityID = GenerateNewUUID();

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _OriginalEntity = _Space->CreateEntity(_EntityID);
    auto _OriginalComponent = _OriginalEntity->AddComponent<CSumComponent>();
    _OriginalComponent->SetA(3);
    _OriginalComponent->SetB(4);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("ReplaceEntity");
    _Space->DeleteEntity(_EntityID);
    auto _ReplacementEntity = _Space->CreateEntity(_EntityID);
    auto _ReplacementComponent = _ReplacementEntity->AddComponent<CSumComponent>();
    _ReplacementComponent->SetA(30);
    _ReplacementComponent->SetB(40);
    _UndoCommand->End();

    ASSERT_TRUE(_Space->HasEntity(_EntityID));
    EXPECT_EQ(30, _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>()->GetA());
    EXPECT_EQ(40, _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>()->GetB());

    EXPECT_TRUE(_Repository->Undo());
    ASSERT_TRUE(_Space->HasEntity(_EntityID));
    EXPECT_EQ(3, _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>()->GetA());
    EXPECT_EQ(4, _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>()->GetB());

    EXPECT_TRUE(_Repository->Redo());
    ASSERT_TRUE(_Space->HasEntity(_EntityID));
    EXPECT_EQ(30, _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>()->GetA());
    EXPECT_EQ(40, _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>()->GetB());
}

TEST(DatabaseRepositoryTest, OperationLogReplaysPersistentChanges)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _Repository = GenerateRepository(GenerateNewUUID());
        _Repository->OpenOperationLog(_LogPath, true);
        auto _Space = GetRepositorySpace(_Repository);
        auto _Entity = _Space->CreateEntity(_EntityID);
        auto _Component = _Entity->AddComponent<CSumComponent>();
        _Component->SetA(7);
        _Component->SetB(8);
        _Repository->CloseOperationLog();
    }

    auto _RecoveredRepository = GenerateRepository(GenerateNewUUID());
    _RecoveredRepository->ReplayOperationLog(_LogPath);

    auto _RecoveredEntity = _RecoveredRepository->GetEntity(_EntityID);
    ASSERT_NE(nullptr, _RecoveredEntity);
    auto _RecoveredComponent = _RecoveredEntity->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RecoveredComponent);
    EXPECT_EQ(7, _RecoveredComponent->GetA());
    EXPECT_EQ(8, _RecoveredComponent->GetB());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, OperationLogReplaysBatchOperationsInOriginalOrder)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _Repository = GenerateRepository(GenerateNewUUID());
        auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
        auto _Component = _Repository->CreateEntity(_EntityID)->AddComponent<COrderedSizeComponent>();
        _Baseline->Commit();

        _Repository->OpenOperationLog(_LogPath, true);
        auto _Batch = _Repository->BeginChangeScope(EChangeScopeKind::UserCommand, "Resize");
        _Component->SetWidth(5);
        _Component->SetLength(6);
        _Batch->Commit();
        _Repository->CloseOperationLog();
    }

    auto _RecoveredRepository = GenerateRepository(GenerateNewUUID());
    auto _RecoveredBaseline = _RecoveredRepository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    auto _RecoveredComponent = _RecoveredRepository->CreateEntity(_EntityID)->AddComponent<COrderedSizeComponent>();
    _RecoveredBaseline->Commit();

    ASSERT_NO_THROW(_RecoveredRepository->ReplayOperationLog(_LogPath));
    EXPECT_EQ(6, _RecoveredComponent->GetLength());
    EXPECT_EQ(5, _RecoveredComponent->GetWidth());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, ReplayOperationLogClearsUndoRedoHistory)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _LogRepository = GenerateRepository(GenerateNewUUID());
        _LogRepository->OpenOperationLog(_LogPath, true);
        auto _Space = GetRepositorySpace(_LogRepository);
        auto _Entity = _Space->CreateEntity(_EntityID);
        auto _Component = _Entity->AddComponent<CSumComponent>();
        _Component->SetA(30);
        _LogRepository->CloseOperationLog();
    }

    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(_EntityID);
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Baseline->Commit();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetA");
    _Component->SetA(2);
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    _Repository->ReplayOperationLog(_LogPath);

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_FALSE(_Repository->CanRedo());
    EXPECT_EQ(30, _Component->GetA());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, OperationLogSkipsNonPersistentObservableFields)
{
    auto _LogPath = MakeTempOperationLogPath();
    auto _Repository = GenerateRepository(GenerateNewUUID());

    std::shared_ptr<IRepository> _Space;
    auto _Baseline = _Repository->BeginChangeScope(EChangeScopeKind::LoadBaseline, "Baseline");
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Baseline->Commit();

    _Repository->OpenOperationLog(_LogPath, true);
    _Component->SetRuntimeValue(42);
    _Repository->CloseOperationLog();

    EXPECT_EQ(0, std::filesystem::file_size(_LogPath));
    EXPECT_FALSE(_Repository->CanUndo());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, AddEntityChangedObserverCanMutateNewEntity)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Mutator = std::make_shared<CAddEntityChangedMutator>();
    _Repository->AddObserver(_Mutator);
    auto _Space = GetRepositorySpace(_Repository);

    _Space->CreateEntity(GenerateNewUUID());

    ASSERT_TRUE(_Mutator->DidMutate);
    ASSERT_NE(nullptr, _Mutator->Component);
    EXPECT_EQ(5, _Mutator->Component->GetA());
    EXPECT_EQ(2, _Mutator->Component->Version());
}

TEST(DatabaseEntityTest, ExactComponentLookupIsSeparatedFromInheritanceQuery)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());

    auto _Derived = _Entity->AddComponent<CDerivedAssignableComponent>();

    EXPECT_TRUE(_Entity->HasComponent<CDerivedAssignableComponent>());
    EXPECT_FALSE(_Entity->HasComponent<CBaseAssignableComponent>());
    EXPECT_EQ(nullptr, _Entity->GetComponent<CBaseAssignableComponent>());

    auto _AssignableComponents = _Entity->GetComponents(CBaseAssignableComponent::S_ClassName);
    ASSERT_EQ(1, _AssignableComponents.size());
    EXPECT_EQ(_Derived, _AssignableComponents.front());

    EXPECT_NO_THROW(_Entity->RemoveComponent<CBaseAssignableComponent>());
    EXPECT_TRUE(_Entity->HasComponent<CDerivedAssignableComponent>());
}

TEST(DatabaseEntityTest, AddComponentChangedObserverCanModifyNewComponent)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Mutator = std::make_shared<CAddComponentChangedMutator>();
    _Repository->AddObserver(_Mutator);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    EXPECT_TRUE(_Mutator->DidMutate);
    EXPECT_EQ(7, _Component->GetA());
    EXPECT_EQ(2, _Component->Version());
}

TEST(DatabaseComponentEventTest, FailedComponentChangeNotifierDoesNotRaiseChanged)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CThrowingNotifierComponent>();
    _Observer->Clear();

    EXPECT_THROW(_Component->SetValueAndThrow(1), std::runtime_error);

    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
}

TEST(DatabaseComponentEventTest, SuccessfulMutationDuringOuterUnwindRaisesChanged)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CUnwindMutationComponent>();
    _Observer->Clear();

    struct CMutateOnUnwind final
    {
        std::shared_ptr<CUnwindMutationComponent> Component;

        ~CMutateOnUnwind()
        {
            Component->SetValue(5);
        }
    };

    try
    {
        CMutateOnUnwind _Guard{ _Component };
        throw std::runtime_error("outer failure");
    }
    catch (const std::runtime_error&)
    {
    }

    EXPECT_EQ(5, _Component->GetValue());
    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
}

TEST(DatabaseComponentEventTest, RemoveComponentReusesChangingSnapshot)
{
    auto _Repository = GenerateRepository(GenerateNewUUID());
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    auto _Mutator = std::make_shared<CRemoveChangingMutator>();
    _Repository->AddObserver(_Observer);
    _Repository->AddObserver(_Mutator);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Component->SetA(1);
    _Observer->Clear();

    _Entity->RemoveComponent<CSumComponent>();

    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(RepositoryEventArgs::kRemoveComponent, _Observer->ChangingEvents.front().nType);
    EXPECT_EQ(RepositoryEventArgs::kRemoveComponent, _Observer->ChangedEvents.front().nType);
    EXPECT_EQ(1, _Observer->ChangingEvents.front().PreviousProperties.at(CSumComponent::PropertyName_A).To<int>());
    EXPECT_EQ(1, _Observer->ChangedEvents.front().PreviousProperties.at(CSumComponent::PropertyName_A).To<int>());
    EXPECT_EQ(99, _Component->GetA());
}

TEST(DatabaseComponentEventTest, SetPropertiesRollsBackAppliedValuesWhenSetterThrows)
{
    auto _Component = std::make_shared<CPartialThrowingComponent>();

    EXPECT_THROW(_Component->SetProperties({ { "A", PropertyValue(10) }, { "B", PropertyValue(20) } }), std::runtime_error);

    EXPECT_EQ(0, _Component->A);
    EXPECT_EQ(0, _Component->B);
}

TEST(DatabaseMetaRegistryTest, TemplateHelpersUseCorrectArgumentOrder)
{
    auto _Meta = GetGlobalMetaRegistry();
    auto _Attribute = std::make_shared<CTestAttribute>();
    auto _Checker = std::make_shared<CAlwaysAllowChecker>();

    _Meta->RegistAttribute<CPolicyComponent>(_Attribute);
    _Meta->RegistChecker<CPolicyComponent>(_Checker);

    auto _Attributes = _Meta->GetAttributes<CPolicyComponent>();
    auto _Checkers = _Meta->GetCheckers<CPolicyComponent>();

    EXPECT_TRUE(_Attributes.contains(_Attribute));
    EXPECT_TRUE(_Checkers.contains(_Checker));
}

TEST(DatabaseMetaRegistryTest, CreatorTemplateUsesProvidedFactory)
{
    auto _Meta = GetGlobalMetaRegistry();

    EXPECT_FALSE(_Meta->HasCreator<CManualCreatorComponent>());

    _Meta->RegistCreator<CManualCreatorComponent>([](std::shared_ptr<IEntity> Entity_) -> std::shared_ptr<CComponentBase>
    {
        return std::make_shared<CManualCreatorComponent>(Entity_);
    });

    EXPECT_TRUE(_Meta->HasCreator<CManualCreatorComponent>());
}

TEST(DatabaseMetaRegistryTest, GlobalAttributeAndCheckerApplyToSpecificComponent)
{
    auto _Meta = GetGlobalMetaRegistry();
    auto _Attribute = std::make_shared<CGlobalAttribute>();
    auto _Checker = std::make_shared<CGlobalAllowChecker>();

    _Meta->RegistAttributeByName(_Attribute);
    _Meta->RegistCheckerByName(_Checker);

    auto _Attributes = _Meta->GetAttributesByName(CSumComponent::S_ClassName);
    auto _Checkers = _Meta->GetCheckersByName(CSumComponent::S_ClassName);

    EXPECT_TRUE(_Attributes.contains(_Attribute));
    EXPECT_TRUE(_Checkers.contains(_Checker));
}
