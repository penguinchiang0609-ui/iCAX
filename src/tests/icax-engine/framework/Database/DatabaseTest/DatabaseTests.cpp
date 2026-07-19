#include "pch.h"


#include <Database/ComponentHelper.h>
#include <Database/IEntity.h>
#include <Database/IFieldPolicyProvider.h>
#include <Database/IRepository.h>
#include <Database/OperationLog.h>
#include <Data/uuid.h>


namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::Database;

    constexpr const char* S_TestOperationLogMagic = "ICAX_DATABASE_TEST_OPERATION_LOG";
    constexpr uint32_t S_TestOperationLogVersion = 1;

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

    class CForeignTransaction final : public ITransaction
    {
    public:
        void CreateEntity(const uuid&) override
        {
        }

        void DisposeEntity(const uuid&) override
        {
        }

        void AttachComponent(const uuid&, const std::string&, const PropertySet&) override
        {
        }

        void DetachComponent(const uuid&, const std::string&) override
        {
        }

        void ModifyComponent(const uuid&, const std::string&, const PropertySet&) override
        {
        }

        void EnableComponent(const uuid&, const std::string&) override
        {
        }

        void DisableComponent(const uuid&, const std::string&) override
        {
        }
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
            if (!SetProperty(PropertyName_Length, PropertyValue(nLength_)))
            {
                throw std::runtime_error("Failed to set ordered size length");
            }
        }

        void SetWidth(int nWidth_)
        {
            if (!SetProperty(PropertyName_Width, PropertyValue(nWidth_)))
            {
                throw std::runtime_error("Failed to set ordered size width");
            }
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
                throw std::runtime_error("Unregistered ordered size property");
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

        explicit CPartialThrowingComponent(std::shared_ptr<IEntity> pEntity_)
            : CComponentBase(std::move(pEntity_))
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

    class CNamedFieldPolicyProvider final : public IFieldPolicyProvider
    {
    public:
        CNamedFieldPolicyProvider(std::string strName_, bool bHandles_)
            : m_strName(std::move(strName_))
            , m_bHandles(bHandles_)
        {
        }

        bool TryGetFieldPolicy(
            const IEntity& Entity_,
            const CComponentBase& Component_,
            const std::string& strPropertyName_,
            SFieldEditPolicy& Policy_) const override
        {
            ++CallCount;
            if (!m_bHandles)
            {
                return false;
            }

            Policy_.bEditable = true;
            Policy_.bHasRange = true;
            Policy_.dMin = static_cast<double>(Component_.GetProperty(strPropertyName_).To<int>());
            Policy_.dMax = Policy_.dMin + 10.0;
            Policy_.dStep = 1.0;
            Policy_.nPrecision = 0;
            Policy_.strUnit = "test";
            Policy_.strReason = m_strName + ":" + to_string(Entity_.GetID());
            return true;
        }

        mutable int CallCount = 0;

    private:
        std::string m_strName;
        bool m_bHandles = false;
    };

    class CRejectNegativeSumChecker final : public IChecker
    {
    public:
        int ModifyCallCount = 0;
        size_t LastPropertyCount = 0;

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
            ++ModifyCallCount;
            LastPropertyCount = Properties_.size();

            auto _A = Component_.GetProperty(CSumComponent::PropertyName_A).To<int>();
            auto _B = Component_.GetProperty(CSumComponent::PropertyName_B).To<int>();
            if (auto _Ite = Properties_.find(CSumComponent::PropertyName_A); _Ite != Properties_.end())
            {
                _A = _Ite->second.To<int>();
            }
            if (auto _Ite = Properties_.find(CSumComponent::PropertyName_B); _Ite != Properties_.end())
            {
                _B = _Ite->second.To<int>();
            }

            if (_A + _B < 0)
            {
                strError_ = "Sum must not be negative";
                return false;
            }
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

    class CThrowingRepositoryObserver final : public IRepositoryEventListener
    {
    public:
        void OnRepositoryChanging(void* pSender_, const RepositoryEventArgs& Args_) override
        {
            ++ChangingCallCount;
            if (ThrowOnChanging)
            {
                throw std::runtime_error("repository changing observer failed");
            }
        }

        void OnRepositoryChanged(void* pSender_, const RepositoryEventArgs& Args_) override
        {
            ++ChangedCallCount;
            if (ThrowOnChanged)
            {
                throw std::runtime_error("repository changed observer failed");
            }
        }

        bool ThrowOnChanging = true;
        bool ThrowOnChanged = true;
        int ChangingCallCount = 0;
        int ChangedCallCount = 0;
    };

    class CThrowingRemoveEntityObserver final : public IEntityEventListener
    {
    public:
        void OnEntityChanging(void* pSender_, const EntityEventArgs& Args_) override
        {
            if (Args_.nType == EntityEventArgs::kRemoveComponent)
            {
                throw std::runtime_error("entity remove changing failed");
            }
        }

        void OnEntityChanged(void* pSender_, const EntityEventArgs& Args_) override
        {
        }
    };

    class CThrowingComponentObserver final : public IComponentEventListener
    {
    public:
        void OnComponentChanging(void* pSender_, const ComponentEventArgs& Args_) override
        {
            ++ChangingCallCount;
            throw std::runtime_error("component changing observer failed");
        }

        void OnComponentChanged(void* pSender_, const ComponentEventArgs& Args_) override
        {
            ++ChangedCallCount;
            throw std::runtime_error("component changed observer failed");
        }

        int ChangingCallCount = 0;
        int ChangedCallCount = 0;
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
                    EXPECT_TRUE(_Component->SetA(7));
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
                EXPECT_TRUE(Component->SetA(5));
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
                    EXPECT_TRUE(_Component->SetA(99));
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

    std::shared_ptr<IMetaRegistry> CreateTestMetaRegistry()
    {
        auto _Meta = CreateMetaRegistry();
        CMetaRegistrationCatalog::ReplayAll(*_Meta);
        _Meta->RegistType(CPartialThrowingComponent::S_ClassName, CComponentBase::S_ClassName);
        _Meta->RegistCreatorByName(
            CPartialThrowingComponent::S_ClassName,
            [](std::shared_ptr<IEntity> pEntity_) {
                return std::make_shared<CPartialThrowingComponent>(std::move(pEntity_));
            });
        _Meta->RegistPropertyByName(
            CPartialThrowingComponent::S_ClassName,
            "A",
            [](const void* pObject_) {
                return static_cast<const CPartialThrowingComponent*>(pObject_)->GetProperty("A");
            },
            [](void* pObject_, const PropertyValue& Value_) {
                static_cast<CPartialThrowingComponent*>(pObject_)->OnSetProperty("A", Value_);
            });
        _Meta->RegistPropertyByName(
            CPartialThrowingComponent::S_ClassName,
            "B",
            [](const void* pObject_) {
                return static_cast<const CPartialThrowingComponent*>(pObject_)->GetProperty("B");
            },
            [](void* pObject_, const PropertyValue& Value_) {
                static_cast<CPartialThrowingComponent*>(pObject_)->OnSetProperty("B", Value_);
            });
        return _Meta;
    }

    std::shared_ptr<IRepository> GenerateTestRepository()
    {
        return GenerateRepository(GenerateNewUUID(), CreateTestMetaRegistry());
    }
}

TEST(DatabaseDerivedPropertyTest, DerivedPropertyIsLazyAndCached)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    CSumComponent::ResetEvaluationCount();

    EXPECT_TRUE(_Component->SetA(2));
    EXPECT_TRUE(_Component->SetB(3));

    EXPECT_EQ(5, _Component->GetSum());
    EXPECT_EQ(1, CSumComponent::EvaluationCount);

    EXPECT_EQ(5, _Component->GetSum());
    EXPECT_EQ(1, CSumComponent::EvaluationCount);
}

TEST(DatabaseDerivedPropertyTest, SourceFieldChangeInvalidatesDerivedProperty)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    CSumComponent::ResetEvaluationCount();

    EXPECT_TRUE(_Component->SetA(2));
    EXPECT_TRUE(_Component->SetB(3));
    EXPECT_EQ(5, _Component->GetSum());
    EXPECT_EQ(1, CSumComponent::EvaluationCount);

    EXPECT_TRUE(_Component->SetA(4));

    EXPECT_EQ(7, _Component->GetSum());
    EXPECT_EQ(2, CSumComponent::EvaluationCount);
}

TEST(DatabaseDerivedPropertyTest, DerivedPropertyCannotBeSet)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    std::string _strError;
    EXPECT_FALSE(_Component->SetProperty(CSumComponent::PropertyName_Sum, PropertyValue(10), _strError));
    EXPECT_FALSE(_strError.empty());
}

TEST(DatabaseDerivedPropertyTest, GetPropertiesExcludesDerivedProperties)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    EXPECT_TRUE(_Component->SetA(2));
    EXPECT_TRUE(_Component->SetB(3));
    EXPECT_EQ(5, _Component->GetSum());

    auto _Properties = _Component->GetProperties();
    EXPECT_TRUE(_Properties.contains(CSumComponent::PropertyName_A));
    EXPECT_TRUE(_Properties.contains(CSumComponent::PropertyName_B));
    EXPECT_FALSE(_Properties.contains(CSumComponent::PropertyName_Sum));
}

TEST(DatabaseDerivedPropertyTest, SetPropertyBumpsComponentVersionOnlyOnce)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    const auto _InitialVersion = _Component->Version();

    EXPECT_TRUE(_Component->SetProperty(CSumComponent::PropertyName_A, PropertyValue(10)));

    EXPECT_EQ(_InitialVersion + 1, _Component->Version());
}

TEST(DatabaseDerivedPropertyTest, DynamicCrossEntityDependenciesAreReplaced)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);

    auto _Parent1Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Parent1 = _Parent1Entity->AddComponent<CChainComponent>();
    EXPECT_TRUE(_Parent1->SetLocal(10));

    auto _Parent2Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Parent2 = _Parent2Entity->AddComponent<CChainComponent>();
    EXPECT_TRUE(_Parent2->SetLocal(100));

    auto _ChildEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _Child = _ChildEntity->AddComponent<CChainComponent>();
    EXPECT_TRUE(_Child->SetLocal(1));
    EXPECT_TRUE(_Child->SetParentID(_Parent1Entity->GetID()));

    EXPECT_EQ(11, _Child->GetTotal());

    EXPECT_TRUE(_Child->SetParentID(_Parent2Entity->GetID()));
    CChainComponent::ResetEvaluationCount();
    EXPECT_EQ(101, _Child->GetTotal());
    const int _CountAfterReparent = CChainComponent::EvaluationCount;

    EXPECT_TRUE(_Parent1->SetLocal(20));
    EXPECT_EQ(101, _Child->GetTotal());
    EXPECT_EQ(_CountAfterReparent, CChainComponent::EvaluationCount);

    EXPECT_TRUE(_Parent2->SetLocal(200));
    EXPECT_EQ(201, _Child->GetTotal());
    EXPECT_GT(CChainComponent::EvaluationCount, _CountAfterReparent);
}

TEST(DatabaseDerivedPropertyTest, MissingComponentDependencyInvalidatesWhenComponentIsAdded)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);

    auto _ParentEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _ChildEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _Child = _ChildEntity->AddComponent<CChainComponent>();
    EXPECT_TRUE(_Child->SetLocal(1));
    EXPECT_TRUE(_Child->SetParentID(_ParentEntity->GetID()));

    CChainComponent::ResetEvaluationCount();
    EXPECT_EQ(1, _Child->GetTotal());
    EXPECT_EQ(1, CChainComponent::EvaluationCount);

    auto _Parent = _ParentEntity->AddComponent<CChainComponent>();
    EXPECT_TRUE(_Parent->SetLocal(10));

    EXPECT_EQ(11, _Child->GetTotal());
    EXPECT_GT(CChainComponent::EvaluationCount, 1);
}

TEST(DatabaseDerivedPropertyTest, MissingEntityDependencyInvalidatesWhenComponentIsAddedLater)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);

    auto _ParentEntityID = GenerateNewUUID();
    auto _ChildEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _Child = _ChildEntity->AddComponent<CChainComponent>();
    EXPECT_TRUE(_Child->SetLocal(1));
    EXPECT_TRUE(_Child->SetParentID(_ParentEntityID));

    CChainComponent::ResetEvaluationCount();
    EXPECT_EQ(1, _Child->GetTotal());
    EXPECT_EQ(1, CChainComponent::EvaluationCount);

    auto _ParentEntity = _Space->CreateEntity(_ParentEntityID);
    auto _Parent = _ParentEntity->AddComponent<CChainComponent>();
    EXPECT_TRUE(_Parent->SetLocal(10));

    EXPECT_EQ(11, _Child->GetTotal());
    EXPECT_GT(CChainComponent::EvaluationCount, 1);
}

TEST(DatabaseDerivedPropertyTest, CircularDerivedDependencyThrows)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CCycleComponent>();

    EXPECT_THROW(_Component->GetA(), std::runtime_error);
}

TEST(DatabaseDerivedPropertyTest, MissingSourcePropertyFailsFast)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CMissingDependencyComponent>();

    EXPECT_THROW(_Component->GetOptionalValue(), std::runtime_error);
}

TEST(DatabaseDerivedPropertyTest, DerivedPropertyCannotDependOnSilentValueProperty)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSilentDependencyComponent>();

    EXPECT_THROW(_Component->GetCachedValue(), std::runtime_error);
}

TEST(DatabasePropertyMetaTest, FieldDeclarationsRegisterPersistenceAndChangePolicy)
{
    auto _Meta = CreateTestMetaRegistry();

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

TEST(DatabasePropertyMetaTest, UnregisteredPropertyMetaMustBeExplicitlyHandled)
{
    auto _Meta = CreateTestMetaRegistry();

    EXPECT_TRUE(_Meta->HasTypeByName(CPolicyComponent::S_ClassName));
    EXPECT_FALSE(_Meta->HasPropertyByName(CPolicyComponent::S_ClassName, "Missing"));
    EXPECT_THROW(_Meta->GetPropertyKindByName(CPolicyComponent::S_ClassName, "Missing"), std::runtime_error);
    EXPECT_THROW(_Meta->GetPropertyPersistenceByName(CPolicyComponent::S_ClassName, "Missing"), std::runtime_error);
    EXPECT_THROW(_Meta->GetPropertyChangePolicyByName(CPolicyComponent::S_ClassName, "Missing"), std::runtime_error);
}

TEST(DatabasePropertyMetaTest, UnregisteredPropertyIsRejectedBeforeEvents)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Component = _Space->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    _Observer->Clear();

    std::string _strError;
    EXPECT_FALSE(_Component->SetProperty("Missing", PropertyValue(123), _strError));
    EXPECT_FALSE(_strError.empty());
    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
}

TEST(DatabasePropertyMetaTest, ObservableFieldRaisesEventAndBumpsVersionWithoutTransactionalPolicy)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Observer->Clear();

    const auto _InitialVersion = _Component->Version();

    EXPECT_TRUE(_Component->SetRuntimeValue(10));

    EXPECT_EQ(_InitialVersion + 1, _Component->Version());
    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(RepositoryEventArgs::kModifyComponent, _Observer->ChangedEvents.back().nType);
    EXPECT_EQ(EPropertyChangePolicy::Observable,
        CreateTestMetaRegistry()->GetPropertyChangePolicyByName(_Observer->ChangedEvents.back().strClassName, CPolicyComponent::PropertyName_RuntimeValue));
    EXPECT_EQ(10, _Observer->ChangedEvents.back().NewProperties.at(CPolicyComponent::PropertyName_RuntimeValue).To<int>());
}

TEST(DatabasePropertyMetaTest, SilentFieldRaisesNormalEventButDoesNotBumpVersion)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Observer->Clear();

    const auto _InitialVersion = _Component->Version();

    EXPECT_TRUE(_Component->SetCacheValue(10));
    EXPECT_EQ(10, _Component->GetCacheValue());
    EXPECT_EQ(_InitialVersion, _Component->Version());
    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(EPropertyChangePolicy::Silent,
        CreateTestMetaRegistry()->GetPropertyChangePolicyByName(_Observer->ChangedEvents.back().strClassName, CPolicyComponent::PropertyName_CacheValue));

    _Observer->Clear();
    EXPECT_TRUE(_Component->SetProperty(CPolicyComponent::PropertyName_CacheValue, PropertyValue(20)));
    EXPECT_EQ(20, _Component->GetCacheValue());
    EXPECT_EQ(_InitialVersion, _Component->Version());
    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(EPropertyChangePolicy::Silent,
        CreateTestMetaRegistry()->GetPropertyChangePolicyByName(_Observer->ChangedEvents.back().strClassName, CPolicyComponent::PropertyName_CacheValue));
}

TEST(DatabaseRepositoryTest, RepositoryIsInitialized)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);

    EXPECT_TRUE(_Space->IsValid());
    ASSERT_NE(nullptr, _Space->GetMetaEntity());
    EXPECT_EQ(_Repository->GetID(), _Space->GetMetaEntity()->GetID());
    EXPECT_EQ(1, _Space->EntityCount());
    EXPECT_NO_THROW(_Space->GetView().BuildCache(CSumComponent::S_ClassName, true));
}

TEST(DatabaseRepositoryTest, ComponentVersionUsesPdoDataVersionType)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();

    static_assert(std::is_same_v<decltype(_Component->Version()), uint64_t>);
    const uint64_t _Version = _Component->Version();

    EXPECT_EQ(_Version, _Repository->GetComponentVersion(*_Component));
}

TEST(DatabaseRepositoryTest, DuplicateEntityThrowsRuntimeError)
{
    auto _Repository = GenerateTestRepository();
    auto _Space = GetRepositorySpace(_Repository);
    auto _EntityID = GenerateNewUUID();

    _Space->CreateEntity(_EntityID);

    EXPECT_THROW(_Space->CreateEntity(_EntityID), std::runtime_error);
}

TEST(DatabaseRepositoryTest, RepositoryObserverExceptionsDoNotBreakHistoryOrOperationLog)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _Repository = GenerateTestRepository();
        auto _ThrowingObserver = std::make_shared<CThrowingRepositoryObserver>();
        _Repository->AddObserver(_ThrowingObserver);
        _Repository->OpenOperationLog(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);

        auto _UndoCommand = _Repository->BeginUndoCommand("CreateEntity");
        std::shared_ptr<IEntity> _Entity;
        ASSERT_NO_THROW(_Entity = _Repository->CreateEntity(_EntityID));
        ASSERT_NE(nullptr, _Entity);
        auto _Component = _Entity->AddComponent<CSumComponent>();
        ASSERT_TRUE(_Component->SetA(11));
        _UndoCommand->End();

        EXPECT_GT(_ThrowingObserver->ChangingCallCount, 0);
        EXPECT_GT(_ThrowingObserver->ChangedCallCount, 0);
        EXPECT_TRUE(_Repository->CanUndo());
        _Repository->CloseOperationLog();
        ASSERT_TRUE(_Repository->Undo());
        EXPECT_FALSE(_Repository->HasEntity(_EntityID));
    }

    auto _RecoveredRepository = GenerateTestRepository();
    ASSERT_NO_THROW(_RecoveredRepository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion));
    auto _RecoveredEntity = _RecoveredRepository->GetEntity(_EntityID);
    ASSERT_NE(nullptr, _RecoveredEntity);
    auto _RecoveredComponent = _RecoveredEntity->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RecoveredComponent);
    EXPECT_EQ(11, _RecoveredComponent->GetA());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, BatchCommandEmitsOneBatchEvent)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    auto _Space = GetRepositorySpace(_Repository);
    auto _ExistingEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _ExistingComponent = _ExistingEntity->AddComponent<CSumComponent>();
    const auto _ExistingVersion = _ExistingComponent->Version();
    _Observer->Clear();

    _Repository->BeginBatch();
    auto _NewEntity = _Space->CreateEntity(GenerateNewUUID());
    auto _NewComponent = _NewEntity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_NewComponent->SetA(1));
    EXPECT_TRUE(_NewComponent->SetA(2));
    EXPECT_TRUE(_NewComponent->SetB(3));
    EXPECT_TRUE(_ExistingComponent->SetA(10));
    EXPECT_TRUE(_ExistingComponent->SetA(20));
    _Repository->EndBatch();

    ASSERT_EQ(1, _Observer->ChangingEvents.size());
    ASSERT_EQ(1, _Observer->ChangedEvents.size());
    EXPECT_EQ(RepositoryEventArgs::kBatchChanged, _Observer->ChangedEvents.front().nType);
    EXPECT_EQ(RepositoryEventArgs::kBatchChanged, _Observer->ChangingEvents.front().nType);

    EXPECT_EQ(1, _NewComponent->Version());
    EXPECT_EQ(_ExistingVersion + 1, _ExistingComponent->Version());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, LoadBaselineDoesNotNotifyOrMarkDirty)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    auto _Space = GetRepositorySpace(_Repository);
    _Observer->Clear();

    std::shared_ptr<CSumComponent> _Component;
    _Repository->BeginLoadBaseline();
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(100));
    _Repository->EndLoadBaseline();

    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_EQ(100, _Component->GetA());
    EXPECT_EQ(0, _Component->Version());
    EXPECT_FALSE(_Component->IsChanged());
}

TEST(DatabaseRepositoryTest, BatchCommandWithNoNetChangeEmitsNoEvent)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    _Observer->Clear();

    _Repository->BeginBatch();
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Entity->RemoveComponent<CSumComponent>();
    _Repository->EndBatch();

    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_FALSE(_Entity->HasComponent<CSumComponent>());
}

TEST(DatabaseRepositoryTest, ModifyFilterRejectsBeforeMutationAndEvents)
{
    auto _Meta = CreateMetaRegistry();
    CMetaRegistrationCatalog::ReplayAll(*_Meta);
    auto _Checker = std::make_shared<CRejectNegativeSumChecker>();
    _Meta->RegistChecker<CSumComponent>(_Checker);

    auto _Repository = GenerateRepository(GenerateNewUUID(), _Meta);
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    _Observer->Clear();

    std::string _strError;
    EXPECT_TRUE(_Component->SetProperties({
        { CSumComponent::PropertyName_A, PropertyValue(5) },
        { CSumComponent::PropertyName_B, PropertyValue(2) }
    }, _strError));
    EXPECT_TRUE(_strError.empty());
    EXPECT_EQ(1, _Checker->ModifyCallCount);
    EXPECT_EQ(2, _Checker->LastPropertyCount);
    EXPECT_EQ(5, _Component->GetA());
    EXPECT_EQ(2, _Component->GetB());

    _Observer->Clear();
    EXPECT_FALSE(_Component->SetProperties({
        { CSumComponent::PropertyName_A, PropertyValue(-10) },
        { CSumComponent::PropertyName_B, PropertyValue(1) }
    }, _strError));
    EXPECT_EQ("Sum must not be negative", _strError);
    EXPECT_EQ(2, _Checker->ModifyCallCount);
    EXPECT_EQ(2, _Checker->LastPropertyCount);
    EXPECT_EQ(5, _Component->GetA());
    EXPECT_EQ(2, _Component->GetB());
    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
}

TEST(DatabaseRepositoryTest, TransactionCancelDiscardsPendingOperations)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(5));
    _Repository->EndLoadBaseline();
    _Observer->Clear();

    const auto _TransientEntityID = GenerateNewUUID();
    auto& _Transaction = _Repository->BeginTransaction("Rollback");
    _Transaction.CreateEntity(_TransientEntityID);
    _Transaction.ModifyComponent(_Entity->GetID(), CSumComponent::S_ClassName, { { CSumComponent::PropertyName_A, PropertyValue(9) } });
    _Repository->CancelTransaction(_Transaction);

    EXPECT_FALSE(_Space->HasEntity(_TransientEntityID));
    EXPECT_EQ(5, _Component->GetA());
    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, TransactionCancelDiscardsReplacementOperations)
{
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_Observer);
    const auto _EntityID = GenerateNewUUID();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(_EntityID);
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(8));
    EXPECT_TRUE(_Component->SetB(9));
    _Repository->EndLoadBaseline();
    _Observer->Clear();

    auto& _Transaction = _Repository->BeginTransaction("CancelReplaceEntity");
    _Transaction.DisposeEntity(_EntityID);
    _Transaction.CreateEntity(_EntityID);
    _Transaction.AttachComponent(_EntityID, CSumComponent::S_ClassName, {
        { CSumComponent::PropertyName_A, PropertyValue(80) },
        { CSumComponent::PropertyName_B, PropertyValue(90) }
    });
    _Repository->CancelTransaction(_Transaction);

    ASSERT_TRUE(_Space->HasEntity(_EntityID));
    auto _RestoredComponent = _Space->GetEntity(_EntityID)->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RestoredComponent);
    EXPECT_EQ(8, _RestoredComponent->GetA());
    EXPECT_EQ(9, _RestoredComponent->GetB());
    EXPECT_TRUE(_Observer->ChangingEvents.empty());
    EXPECT_TRUE(_Observer->ChangedEvents.empty());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, TransactionLifecycleRequiresReturnedHandle)
{
    auto _Repository = GenerateTestRepository();
    auto& _Transaction = _Repository->BeginTransaction("ActiveTransaction");
    CForeignTransaction _ForeignTransaction;

    EXPECT_THROW((void)_Repository->CommitTransaction(_ForeignTransaction), std::runtime_error);
    EXPECT_THROW((void)_Repository->CancelTransaction(_ForeignTransaction), std::runtime_error);

    _Transaction.CreateEntity(GenerateNewUUID());
    EXPECT_TRUE(_Repository->CommitTransaction(_Transaction));
}

TEST(DatabaseRepositoryTest, TransactionCommitReturnsErrorTextOnFailure)
{
    auto _Repository = GenerateTestRepository();
    const auto _MissingEntityID = GenerateNewUUID();

    auto& _Transaction = _Repository->BeginTransaction("BadTransaction");
    _Transaction.ModifyComponent(_MissingEntityID, CSumComponent::S_ClassName, {
        { CSumComponent::PropertyName_A, PropertyValue(1) }
    });

    std::string _strError;
    EXPECT_FALSE(_Repository->CommitTransaction(_Transaction, _strError));
    EXPECT_FALSE(_strError.empty());
    EXPECT_FALSE(_Repository->HasEntity(_MissingEntityID));
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, TransactionCommitWritesOperationLogButDoesNotCreateUndoStep)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _Repository = GenerateTestRepository();
        _Repository->OpenOperationLog(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);

        auto& _Transaction = _Repository->BeginTransaction("CreateRuntimeTransaction");
        _Transaction.CreateEntity(_EntityID);
        _Transaction.AttachComponent(_EntityID, CSumComponent::S_ClassName, { { CSumComponent::PropertyName_A, PropertyValue(11) } });
        EXPECT_TRUE(_Repository->CommitTransaction(_Transaction));

        EXPECT_FALSE(_Repository->CanUndo());
        _Repository->CloseOperationLog();
    }

    auto _RecoveredRepository = GenerateTestRepository();
    _RecoveredRepository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion);

    auto _RecoveredEntity = _RecoveredRepository->GetEntity(_EntityID);
    ASSERT_NE(nullptr, _RecoveredEntity);
    auto _RecoveredComponent = _RecoveredEntity->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RecoveredComponent);
    EXPECT_EQ(11, _RecoveredComponent->GetA());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, TransactionCanPersistComponentEnabledState)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _Repository = GenerateTestRepository();
        _Repository->BeginLoadBaseline();
        auto _Entity = _Repository->CreateEntity(_EntityID);
        auto _Component = _Entity->AddComponent<CSumComponent>();
        ASSERT_TRUE(_Component->IsEnable());
        _Repository->EndLoadBaseline();

        _Repository->OpenOperationLog(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        auto& _Transaction = _Repository->BeginTransaction("DisableComponent");
        _Transaction.DisableComponent(_EntityID, CSumComponent::S_ClassName);
        EXPECT_TRUE(_Repository->CommitTransaction(_Transaction));
        EXPECT_FALSE(_Component->IsEnable());
        _Repository->CloseOperationLog();
    }

    auto _RecoveredRepository = GenerateTestRepository();
    _RecoveredRepository->BeginLoadBaseline();
    auto _RecoveredComponent = _RecoveredRepository->CreateEntity(_EntityID)->AddComponent<CSumComponent>();
    _RecoveredRepository->EndLoadBaseline();

    ASSERT_NO_THROW(_RecoveredRepository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion));
    EXPECT_FALSE(_RecoveredComponent->IsEnable());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, TransactionInsideUndoCommandCreatesUndoStep)
{
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("TransactionUndo");
    auto& _Transaction = _Repository->BeginTransaction("SetA");
    _Transaction.ModifyComponent(_Entity->GetID(), CSumComponent::S_ClassName, { { CSumComponent::PropertyName_A, PropertyValue(12) } });
    EXPECT_TRUE(_Repository->CommitTransaction(_Transaction));
    _UndoCommand->End();

    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetA());
    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(12, _Component->GetA());
}

TEST(DatabaseRepositoryTest, BatchInsideUndoCommandCreatesUndoStep)
{
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("BatchUndo");
    _Repository->BeginBatch();
    EXPECT_TRUE(_Component->SetA(20));
    EXPECT_TRUE(_Component->SetB(30));
    _Repository->EndBatch();
    _UndoCommand->End();

    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetA());
    EXPECT_EQ(0, _Component->GetB());
    EXPECT_TRUE(_Repository->Redo());
    EXPECT_EQ(20, _Component->GetA());
    EXPECT_EQ(30, _Component->GetB());
}

TEST(DatabaseRepositoryTest, UndoRedoUsesCommittedOperationBatch)
{
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetA");
    EXPECT_TRUE(_Component->SetA(10));
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

TEST(DatabaseRepositoryTest, UndoRedoRestoresComponentEnabledState)
{
    auto _Repository = GenerateTestRepository();

    _Repository->BeginLoadBaseline();
    auto _Component = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    _Repository->EndLoadBaseline();

    ASSERT_TRUE(_Component->IsEnable());

    auto _UndoCommand = _Repository->BeginUndoCommand("DisableComponent");
    _Component->Disable();
    _UndoCommand->End();

    EXPECT_FALSE(_Component->IsEnable());
    ASSERT_TRUE(_Repository->CanUndo());

    EXPECT_TRUE(_Repository->Undo());
    EXPECT_TRUE(_Component->IsEnable());

    EXPECT_TRUE(_Repository->Redo());
    EXPECT_FALSE(_Component->IsEnable());
}

TEST(DatabaseRepositoryTest, UndoRedoReplaysOperationsInOriginalOrder)
{
    auto _Repository = GenerateTestRepository();

    _Repository->BeginLoadBaseline();
    auto _Component = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<COrderedSizeComponent>();
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("Resize");
    EXPECT_NO_THROW(_Component->SetWidth(5));
    EXPECT_NO_THROW(_Component->SetLength(6));
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
    auto _Repository = GenerateTestRepository();

    _Repository->BeginLoadBaseline();
    auto _ComponentA = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    auto _ComponentB = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    EXPECT_TRUE(_ComponentA->SetA(1));
    EXPECT_TRUE(_ComponentB->SetA(2));
    _Repository->EndLoadBaseline();

    auto _SharedCommand = _Repository->BeginUndoCommand("MoveBoth");
    EXPECT_TRUE(_ComponentA->SetA(10));
    EXPECT_TRUE(_ComponentB->SetA(20));
    _SharedCommand->End();

    EXPECT_TRUE(_Repository->CanUndo());

    auto _BCommand = _Repository->BeginUndoCommand("MoveB");
    EXPECT_TRUE(_ComponentB->SetA(30));
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
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    EXPECT_TRUE(_Component->SetPersistentValue(1));
    EXPECT_TRUE(_Component->SetRuntimeValue(2));
    EXPECT_TRUE(_Component->SetCacheValue(3));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("MixedPolicy");
    EXPECT_TRUE(_Component->SetPersistentValue(10));
    EXPECT_TRUE(_Component->SetRuntimeValue(20));
    EXPECT_TRUE(_Component->SetCacheValue(30));
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
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("RuntimeOnly");
    EXPECT_TRUE(_Component->SetRuntimeValue(20));
    EXPECT_TRUE(_Component->SetCacheValue(30));
    _UndoCommand->End();

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->GetUndoArray().empty());
}

TEST(DatabaseRepositoryTest, LoadBaselineClearsUndoRedoHistory)
{
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetA");
    EXPECT_TRUE(_Component->SetA(2));
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    ASSERT_TRUE(_Repository->CanRedo());

    _Repository->BeginLoadBaseline();
    EXPECT_TRUE(_Component->SetA(3));
    _Repository->EndLoadBaseline();

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_FALSE(_Repository->CanRedo());
    EXPECT_TRUE(_Repository->GetUndoArray().empty());
    EXPECT_TRUE(_Repository->GetRedoArray().empty());
}

TEST(DatabaseRepositoryTest, UndoHistoryLimitRemovesOldestRepositoryStep)
{
    auto _Repository = GenerateTestRepository();

    _Repository->BeginLoadBaseline();
    auto _ComponentA = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    auto _ComponentB = _Repository->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    EXPECT_TRUE(_ComponentA->SetA(1));
    EXPECT_TRUE(_ComponentB->SetA(2));
    _Repository->EndLoadBaseline();

    auto _SharedCommand = _Repository->BeginUndoCommand("MoveBoth");
    EXPECT_TRUE(_ComponentA->SetA(10));
    EXPECT_TRUE(_ComponentB->SetA(20));
    _SharedCommand->End();

    ASSERT_EQ(1, _Repository->GetUndoArray().size());

    for (int _Index = 0; _Index < 40; ++_Index)
    {
        auto _Command = _Repository->BeginUndoCommand("MoveB");
        EXPECT_TRUE(_ComponentB->SetA(100 + _Index));
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

TEST(DatabaseRepositoryTest, NonUndoableTransactionClearsRepositoryUndoRedoHistory)
{
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Component = _Space->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("UndoableSetA");
    EXPECT_TRUE(_Component->SetA(2));
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    auto& _Transaction = _Repository->BeginTransaction("NonUndoableSetA");
    auto _Entity = _Component->GetEntity();
    ASSERT_NE(nullptr, _Entity);
    _Transaction.ModifyComponent(_Entity->GetID(), CSumComponent::S_ClassName, { { CSumComponent::PropertyName_A, PropertyValue(3) } });
    EXPECT_TRUE(_Repository->CommitTransaction(_Transaction));

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->GetUndoArray().empty());
    EXPECT_EQ(3, _Component->GetA());
}

TEST(DatabaseRepositoryTest, NonUndoableChangeClearsRepositoryUndoRedoHistory)
{
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _SpaceA;
    std::shared_ptr<IRepository> _SpaceB;
    _Repository->BeginLoadBaseline();
    _SpaceA = GetRepositorySpace(_Repository);
    _SpaceB = GetRepositorySpace(_Repository);
    auto _ComponentA = _SpaceA->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    auto _ComponentB = _SpaceB->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    EXPECT_TRUE(_ComponentA->SetA(1));
    EXPECT_TRUE(_ComponentB->SetA(2));
    _Repository->EndLoadBaseline();

    auto _SharedCommand = _Repository->BeginUndoCommand("SharedMove");
    EXPECT_TRUE(_ComponentA->SetA(10));
    EXPECT_TRUE(_ComponentB->SetA(20));
    _SharedCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    EXPECT_TRUE(_ComponentB->SetA(30));

    EXPECT_TRUE(_Repository->GetUndoArray().empty());
    EXPECT_FALSE(_Repository->CanUndo());
}

TEST(DatabaseRepositoryTest, ObservableChangeDoesNotClearUndoHistory)
{
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Component = _Space->CreateEntity(GenerateNewUUID())->AddComponent<CPolicyComponent>();
    EXPECT_TRUE(_Component->SetPersistentValue(1));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetPersistent");
    EXPECT_TRUE(_Component->SetPersistentValue(2));
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    EXPECT_TRUE(_Component->SetRuntimeValue(30));
    EXPECT_TRUE(_Component->SetCacheValue(40));

    EXPECT_TRUE(_Repository->CanUndo());
    EXPECT_TRUE(_Repository->Undo());
    EXPECT_EQ(1, _Component->GetPersistentValue());
    EXPECT_EQ(30, _Component->GetRuntimeValue());
    EXPECT_EQ(40, _Component->GetCacheValue());
}

TEST(DatabaseRepositoryTest, UndoRestoresDeletedEntityComponents)
{
    auto _Repository = GenerateTestRepository();
    const auto _EntityID = GenerateNewUUID();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(_EntityID);
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(4));
    EXPECT_TRUE(_Component->SetB(5));
    _Repository->EndLoadBaseline();

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
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    std::shared_ptr<IEntity> _Entity;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _OriginalComponent = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_OriginalComponent->SetA(1));
    EXPECT_TRUE(_OriginalComponent->SetB(2));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("ReplaceComponent");
    _Entity->RemoveComponent<CSumComponent>();
    auto _ReplacementComponent = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_ReplacementComponent->SetA(10));
    EXPECT_TRUE(_ReplacementComponent->SetB(20));
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
    auto _Repository = GenerateTestRepository();
    const auto _EntityID = GenerateNewUUID();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _OriginalEntity = _Space->CreateEntity(_EntityID);
    auto _OriginalComponent = _OriginalEntity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_OriginalComponent->SetA(3));
    EXPECT_TRUE(_OriginalComponent->SetB(4));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("ReplaceEntity");
    _Space->DeleteEntity(_EntityID);
    auto _ReplacementEntity = _Space->CreateEntity(_EntityID);
    auto _ReplacementComponent = _ReplacementEntity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_ReplacementComponent->SetA(30));
    EXPECT_TRUE(_ReplacementComponent->SetB(40));
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
        auto _Repository = GenerateTestRepository();
        _Repository->OpenOperationLog(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        auto _Space = GetRepositorySpace(_Repository);
        auto _Entity = _Space->CreateEntity(_EntityID);
        auto _Component = _Entity->AddComponent<CSumComponent>();
        EXPECT_TRUE(_Component->SetA(7));
        EXPECT_TRUE(_Component->SetB(8));
        _Component->Disable();
        _Repository->CloseOperationLog();
    }

    auto _RecoveredRepository = GenerateTestRepository();
    _RecoveredRepository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion);

    auto _RecoveredEntity = _RecoveredRepository->GetEntity(_EntityID);
    ASSERT_NE(nullptr, _RecoveredEntity);
    auto _RecoveredComponent = _RecoveredEntity->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RecoveredComponent);
    EXPECT_EQ(7, _RecoveredComponent->GetA());
    EXPECT_EQ(8, _RecoveredComponent->GetB());
    EXPECT_FALSE(_RecoveredComponent->IsEnable());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, OperationLogRestoresComponentEnabledStateFromAddSnapshot)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    COperationBatch _Batch;
    _Batch.Kind = EOperationBatchKind::UserCommand;
    _Batch.Name = "CreateDisabledComponent";

    CRepositoryOperation _CreateEntity;
    _CreateEntity.Type = RepositoryEventArgs::kAddEntity;
    _CreateEntity.EntityID = _EntityID;
    _Batch.Operations.push_back(_CreateEntity);

    CRepositoryOperation _AddComponent;
    _AddComponent.Type = RepositoryEventArgs::kAddComponent;
    _AddComponent.EntityID = _EntityID;
    _AddComponent.ComponentClass = CSumComponent::S_ClassName;
    _AddComponent.NewProperties = { { CSumComponent::PropertyName_A, PropertyValue(12) } };
    _AddComponent.NewEnabled = false;
    _Batch.Operations.push_back(_AddComponent);

    {
        COperationBatchJournal _Journal;
        _Journal.Open(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        _Journal.Append(_Batch);
        _Journal.Close();
    }

    auto _RecoveredRepository = GenerateTestRepository();
    ASSERT_NO_THROW(_RecoveredRepository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion));

    auto _RecoveredEntity = _RecoveredRepository->GetEntity(_EntityID);
    ASSERT_NE(nullptr, _RecoveredEntity);
    auto _RecoveredComponent = _RecoveredEntity->GetComponent<CSumComponent>();
    ASSERT_NE(nullptr, _RecoveredComponent);
    EXPECT_EQ(12, _RecoveredComponent->GetA());
    EXPECT_FALSE(_RecoveredComponent->IsEnable());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, OperationLogReplaysBatchOperationsInOriginalOrder)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _Repository = GenerateTestRepository();
        _Repository->BeginLoadBaseline();
        auto _Component = _Repository->CreateEntity(_EntityID)->AddComponent<COrderedSizeComponent>();
        _Repository->EndLoadBaseline();

        _Repository->OpenOperationLog(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        _Repository->BeginBatch();
        EXPECT_NO_THROW(_Component->SetWidth(5));
        EXPECT_NO_THROW(_Component->SetLength(6));
        _Repository->EndBatch();
        _Repository->CloseOperationLog();
    }

    auto _RecoveredRepository = GenerateTestRepository();
    _RecoveredRepository->BeginLoadBaseline();
    auto _RecoveredComponent = _RecoveredRepository->CreateEntity(_EntityID)->AddComponent<COrderedSizeComponent>();
    _RecoveredRepository->EndLoadBaseline();

    ASSERT_NO_THROW(_RecoveredRepository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion));
    EXPECT_EQ(6, _RecoveredComponent->GetLength());
    EXPECT_EQ(5, _RecoveredComponent->GetWidth());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, ReplayOperationLogIsStrictAndRollsBackFailedBatch)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    COperationBatch _Batch;
    _Batch.Kind = EOperationBatchKind::UserCommand;
    _Batch.Name = "BrokenReplay";
    CRepositoryOperation _CreateEntity;
    _CreateEntity.Type = RepositoryEventArgs::kAddEntity;
    _CreateEntity.EntityID = _EntityID;
    _Batch.Operations.push_back(_CreateEntity);

    CRepositoryOperation _ModifyMissingComponent;
    _ModifyMissingComponent.Type = RepositoryEventArgs::kModifyComponent;
    _ModifyMissingComponent.EntityID = _EntityID;
    _ModifyMissingComponent.ComponentClass = CSumComponent::S_ClassName;
    _ModifyMissingComponent.PreviousProperties = { { CSumComponent::PropertyName_A, PropertyValue(0) } };
    _ModifyMissingComponent.NewProperties = { { CSumComponent::PropertyName_A, PropertyValue(9) } };
    _Batch.Operations.push_back(_ModifyMissingComponent);

    {
        COperationBatchJournal _Journal;
        _Journal.Open(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        _Journal.Append(_Batch);
        _Journal.Close();
    }

    auto _Repository = GenerateTestRepository();
    EXPECT_THROW(_Repository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion), std::runtime_error);
    EXPECT_FALSE(_Repository->HasEntity(_EntityID));
    EXPECT_EQ(1, _Repository->EntityCount());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, OperationLogRejectsInvalidKindAndType)
{
    ObjectMap _InvalidKind;
    _InvalidKind["kind"] = Variant(std::string("InvalidKind"));
    _InvalidKind["name"] = Variant(std::string("BadKind"));
    _InvalidKind["operations"] = Variant(VariantArray{});

    EXPECT_THROW(OperationBatchFromVariant(Variant(_InvalidKind)), std::runtime_error);

    ObjectMap _InvalidOperation;
    _InvalidOperation["type"] = Variant(std::string("InvalidType"));
    _InvalidOperation["entityID"] = Variant(GenerateNewUUID());
    _InvalidOperation["componentClass"] = Variant(std::string(CSumComponent::S_ClassName));
    _InvalidOperation["previousProperties"] = Variant(ObjectMap{});
    _InvalidOperation["newProperties"] = Variant(ObjectMap{});

    ObjectMap _InvalidType;
    _InvalidType["kind"] = Variant(std::string("UserCommand"));
    _InvalidType["name"] = Variant(std::string("BadType"));
    _InvalidType["operations"] = Variant(VariantArray{ Variant(_InvalidOperation) });

    EXPECT_THROW(OperationBatchFromVariant(Variant(_InvalidType)), std::runtime_error);
}

TEST(DatabaseRepositoryTest, OperationLogRejectsInvalidInMemoryEnums)
{
    CRepositoryOperation _InvalidOperation;
    _InvalidOperation.Type = static_cast<RepositoryEventArgs::EventType>(9999);
    _InvalidOperation.EntityID = GenerateNewUUID();
    _InvalidOperation.ComponentClass = CSumComponent::S_ClassName;

    EXPECT_THROW(_InvalidOperation.IsEmpty(), std::runtime_error);

    COperationBatch _InvalidOperationBatch;
    _InvalidOperationBatch.Kind = EOperationBatchKind::UserCommand;
    _InvalidOperationBatch.Name = "InvalidOperation";
    _InvalidOperationBatch.Operations.push_back(_InvalidOperation);

    EXPECT_THROW(OperationBatchToVariant(_InvalidOperationBatch), std::runtime_error);
    EXPECT_THROW(BuildChangeSetFromOperationBatch(_InvalidOperationBatch), std::runtime_error);
    EXPECT_THROW(FilterTransactionalOperationBatch(_InvalidOperationBatch, *CreateTestMetaRegistry()), std::runtime_error);
    EXPECT_THROW(MakeInverseOperationBatch(_InvalidOperationBatch), std::runtime_error);

    COperationBatch _InvalidKindBatch;
    _InvalidKindBatch.Kind = static_cast<EOperationBatchKind>(9999);
    _InvalidKindBatch.Name = "InvalidKind";

    EXPECT_THROW(OperationBatchToVariant(_InvalidKindBatch), std::runtime_error);
}

TEST(DatabaseRepositoryTest, OperationLogHeaderRejectsMismatchedMagicAndVersion)
{
    auto _LogPath = MakeTempOperationLogPath();

    {
        COperationBatchJournal _Journal;
        _Journal.Open(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        _Journal.Close();
    }

    COperationBatchJournal _Reader;
    EXPECT_THROW(_Reader.ReadAll(_LogPath, "OTHER_MAGIC", S_TestOperationLogVersion), std::runtime_error);
    EXPECT_THROW(_Reader.ReadAll(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion + 1), std::runtime_error);
    EXPECT_THROW(_Reader.Open(_LogPath, false, "OTHER_MAGIC", S_TestOperationLogVersion), std::runtime_error);
    EXPECT_THROW(_Reader.Open(_LogPath, false, S_TestOperationLogMagic, S_TestOperationLogVersion + 1), std::runtime_error);

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, ReplayOperationLogRejectsUnsafeRepositoryStates)
{
    auto _LogPath = MakeTempOperationLogPath();
    {
        COperationBatchJournal _Journal;
        _Journal.Open(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        _Journal.Close();
    }

    auto _Repository = GenerateTestRepository();

    _Repository->BeginBatch();
    EXPECT_THROW(_Repository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion), std::runtime_error);
    _Repository->EndBatch();

    auto& _Transaction = _Repository->BeginTransaction("ActiveTransaction");
    EXPECT_THROW(_Repository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion), std::runtime_error);
    _Repository->CancelTransaction(_Transaction);

    auto _UndoCommand = _Repository->BeginUndoCommand("ActiveUndoCommand");
    EXPECT_THROW(_Repository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion), std::runtime_error);
    _UndoCommand->End();

    _Repository->OpenOperationLog(_LogPath, false, S_TestOperationLogMagic, S_TestOperationLogVersion);
    EXPECT_THROW(_Repository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion), std::runtime_error);
    _Repository->CloseOperationLog();

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, ReplayOperationLogClearsUndoRedoHistory)
{
    auto _LogPath = MakeTempOperationLogPath();
    const auto _EntityID = GenerateNewUUID();

    {
        auto _LogRepository = GenerateTestRepository();
        _LogRepository->BeginLoadBaseline();
        auto _Space = GetRepositorySpace(_LogRepository);
        auto _Entity = _Space->CreateEntity(_EntityID);
        auto _Component = _Entity->AddComponent<CSumComponent>();
        EXPECT_TRUE(_Component->SetA(1));
        _LogRepository->EndLoadBaseline();

        _LogRepository->OpenOperationLog(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
        EXPECT_TRUE(_Component->SetA(30));
        _LogRepository->CloseOperationLog();
    }

    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(_EntityID);
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Repository->EndLoadBaseline();

    auto _UndoCommand = _Repository->BeginUndoCommand("SetA");
    EXPECT_TRUE(_Component->SetA(2));
    _UndoCommand->End();

    ASSERT_TRUE(_Repository->CanUndo());

    _Repository->ReplayOperationLog(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion);

    EXPECT_FALSE(_Repository->CanUndo());
    EXPECT_FALSE(_Repository->CanRedo());
    EXPECT_EQ(30, _Component->GetA());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, OperationLogSkipsNonPersistentObservableFields)
{
    auto _LogPath = MakeTempOperationLogPath();
    auto _Repository = GenerateTestRepository();

    std::shared_ptr<IRepository> _Space;
    _Repository->BeginLoadBaseline();
    _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    _Repository->EndLoadBaseline();

    _Repository->OpenOperationLog(_LogPath, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
    EXPECT_TRUE(_Component->SetRuntimeValue(42));
    _Repository->CloseOperationLog();

    EXPECT_GT(std::filesystem::file_size(_LogPath), 0u);
    COperationBatchJournal _Journal;
    EXPECT_TRUE(_Journal.ReadAll(_LogPath, S_TestOperationLogMagic, S_TestOperationLogVersion).empty());
    EXPECT_FALSE(_Repository->CanUndo());

    std::filesystem::remove(_LogPath);
}

TEST(DatabaseRepositoryTest, AddEntityChangedObserverCanMutateNewEntity)
{
    auto _Repository = GenerateTestRepository();
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
    auto _Repository = GenerateTestRepository();
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
    auto _Repository = GenerateTestRepository();
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
    auto _Repository = GenerateTestRepository();
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
    auto _Repository = GenerateTestRepository();
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
            EXPECT_TRUE(Component->SetValue(5));
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
    auto _Repository = GenerateTestRepository();
    auto _Observer = std::make_shared<CRepositoryEventCollector>();
    auto _Mutator = std::make_shared<CRemoveChangingMutator>();
    _Repository->AddObserver(_Observer);
    _Repository->AddObserver(_Mutator);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    EXPECT_TRUE(_Component->SetA(1));
    _Observer->Clear();

    _Entity->RemoveComponent<CSumComponent>();

    auto _RemoveChanging = std::find_if(
        _Observer->ChangingEvents.begin(),
        _Observer->ChangingEvents.end(),
        [](const RepositoryEventArgs& Args_)
        {
            return Args_.nType == RepositoryEventArgs::kRemoveComponent;
        });
    auto _RemoveChanged = std::find_if(
        _Observer->ChangedEvents.begin(),
        _Observer->ChangedEvents.end(),
        [](const RepositoryEventArgs& Args_)
        {
            return Args_.nType == RepositoryEventArgs::kRemoveComponent;
        });

    ASSERT_NE(_Observer->ChangingEvents.end(), _RemoveChanging);
    ASSERT_NE(_Observer->ChangedEvents.end(), _RemoveChanged);
    EXPECT_EQ(1, _RemoveChanging->PreviousProperties.at(CSumComponent::PropertyName_A).To<int>());
    EXPECT_EQ(1, _RemoveChanged->PreviousProperties.at(CSumComponent::PropertyName_A).To<int>());
    EXPECT_EQ(99, _Component->GetA());
}

TEST(DatabaseComponentEventTest, EntityObserverExceptionsDoNotBlockRemoveComponent)
{
    auto _Repository = GenerateTestRepository();
    auto _RepositoryObserver = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_RepositoryObserver);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    auto _ThrowingEntityObserver = std::make_shared<CThrowingRemoveEntityObserver>();
    _Entity->AddObserver(_ThrowingEntityObserver);
    _RepositoryObserver->Clear();

    std::string _strError;
    EXPECT_TRUE(_Entity->RemoveComponent(CSumComponent::S_ClassName, _strError));
    EXPECT_TRUE(_strError.empty());
    EXPECT_FALSE(_Entity->HasComponent<CSumComponent>());
    ASSERT_EQ(1, _RepositoryObserver->ChangedEvents.size());
    EXPECT_EQ(RepositoryEventArgs::kRemoveComponent, _RepositoryObserver->ChangedEvents.front().nType);
}

TEST(DatabaseComponentEventTest, ComponentObserverExceptionsDoNotBlockModifyComponent)
{
    auto _Repository = GenerateTestRepository();
    auto _RepositoryObserver = std::make_shared<CRepositoryEventCollector>();
    _Repository->AddObserver(_RepositoryObserver);

    auto _Space = GetRepositorySpace(_Repository);
    auto _Entity = _Space->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CSumComponent>();
    auto _ThrowingComponentObserver = std::make_shared<CThrowingComponentObserver>();
    _Component->AddObserver(_ThrowingComponentObserver);
    _RepositoryObserver->Clear();

    std::string _strError;
    EXPECT_TRUE(_Component->SetA(12, _strError));
    EXPECT_TRUE(_strError.empty());
    EXPECT_EQ(12, _Component->GetA());
    EXPECT_EQ(1, _ThrowingComponentObserver->ChangingCallCount);
    EXPECT_EQ(1, _ThrowingComponentObserver->ChangedCallCount);
    ASSERT_EQ(1, _RepositoryObserver->ChangedEvents.size());
    EXPECT_EQ(RepositoryEventArgs::kModifyComponent, _RepositoryObserver->ChangedEvents.front().nType);
    EXPECT_EQ(12, _RepositoryObserver->ChangedEvents.front().NewProperties.at(CSumComponent::PropertyName_A).To<int>());
}

TEST(DatabaseComponentEventTest, SetPropertiesRollsBackAppliedValuesWhenSetterThrows)
{
    auto _Repository = GenerateTestRepository();
    auto _Entity = _Repository->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPartialThrowingComponent>();

    std::string _strError;
    EXPECT_FALSE(_Component->SetProperties({ { "A", PropertyValue(10) }, { "B", PropertyValue(20) } }, _strError));
    EXPECT_FALSE(_strError.empty());

    EXPECT_EQ(0, _Component->A);
    EXPECT_EQ(0, _Component->B);
}

TEST(DatabaseMetaRegistryTest, TemplateHelpersUseCorrectArgumentOrder)
{
    auto _Meta = CreateTestMetaRegistry();
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
    auto _Meta = CreateTestMetaRegistry();

    EXPECT_FALSE(_Meta->HasCreator<CManualCreatorComponent>());

    _Meta->RegistCreator<CManualCreatorComponent>([](std::shared_ptr<IEntity> Entity_) -> std::shared_ptr<CComponentBase>
    {
        return std::make_shared<CManualCreatorComponent>(Entity_);
    });

    EXPECT_TRUE(_Meta->HasCreator<CManualCreatorComponent>());
}

TEST(DatabaseMetaRegistryTest, GlobalAttributeAndCheckerApplyToSpecificComponent)
{
    auto _Meta = CreateTestMetaRegistry();
    auto _Attribute = std::make_shared<CGlobalAttribute>();
    auto _Checker = std::make_shared<CGlobalAllowChecker>();

    _Meta->RegistAttributeByName(_Attribute);
    _Meta->RegistCheckerByName(_Checker);

    auto _Attributes = _Meta->GetAttributesByName(CSumComponent::S_ClassName);
    auto _Checkers = _Meta->GetCheckersByName(CSumComponent::S_ClassName);

    EXPECT_TRUE(_Attributes.contains(_Attribute));
    EXPECT_TRUE(_Checkers.contains(_Checker));
}

TEST(DatabaseMetaRegistryTest, FieldPolicyProviderCanUseInstanceData)
{
    auto _Meta = CreateTestMetaRegistry();
    auto _Provider = std::make_shared<CNamedFieldPolicyProvider>("specific", true);
    _Meta->RegistFieldPolicyProvider<CPolicyComponent>(_Provider);

    auto _Repository = GenerateRepository(GenerateNewUUID(), _Meta);
    auto _Entity = _Repository->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    EXPECT_TRUE(_Component->SetPersistentValue(7));

    SFieldEditPolicy _Policy;
    EXPECT_TRUE(_Meta->TryGetFieldPolicy(*_Entity, *_Component, CPolicyComponent::PropertyName_PersistentValue, _Policy));
    EXPECT_TRUE(_Policy.bEditable);
    EXPECT_TRUE(_Policy.bHasRange);
    EXPECT_EQ(7.0, _Policy.dMin);
    EXPECT_EQ(17.0, _Policy.dMax);
    EXPECT_EQ(1.0, _Policy.dStep);
    EXPECT_EQ(0, _Policy.nPrecision);
    EXPECT_EQ("test", _Policy.strUnit);
    EXPECT_NE(std::string::npos, _Policy.strReason.find("specific:"));
    EXPECT_EQ(1, _Provider->CallCount);
}

TEST(DatabaseMetaRegistryTest, FieldPolicyProviderFallsBackToGlobalWhenSpecificDoesNotHandle)
{
    auto _Meta = CreateTestMetaRegistry();
    auto _Specific = std::make_shared<CNamedFieldPolicyProvider>("specific", false);
    auto _Global = std::make_shared<CNamedFieldPolicyProvider>("global", true);

    _Meta->RegistFieldPolicyProvider<CPolicyComponent>(_Specific);
    _Meta->RegistFieldPolicyProviderByName(_Global);

    auto _Repository = GenerateRepository(GenerateNewUUID(), _Meta);
    auto _Entity = _Repository->CreateEntity(GenerateNewUUID());
    auto _Component = _Entity->AddComponent<CPolicyComponent>();
    EXPECT_TRUE(_Component->SetRuntimeValue(3));

    SFieldEditPolicy _Policy;
    EXPECT_TRUE(_Meta->TryGetFieldPolicy(*_Entity, *_Component, CPolicyComponent::PropertyName_RuntimeValue, _Policy));
    EXPECT_EQ(3.0, _Policy.dMin);
    EXPECT_NE(std::string::npos, _Policy.strReason.find("global:"));
    EXPECT_EQ(1, _Specific->CallCount);
    EXPECT_EQ(1, _Global->CallCount);
}
