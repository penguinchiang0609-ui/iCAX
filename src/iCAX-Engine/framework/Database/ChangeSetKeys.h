#pragma once

#include "OperationLog.h"

#include <set>
#include <stdexcept>

namespace iCAX::Database::detail
{
    // Internal projection consumed by version/derived invalidation. It owns
    // every key; no property snapshot or borrowed pointer survives Build().
    // Public CChangeSet continues to own its complete before/after values.
    struct CChangeSetKeys final
    {
        std::set<CChangeEntityKey> CreatedEntities;
        std::set<CChangeEntityKey> DeletedEntities;
        std::map<CChangeComponentKey, std::set<std::string>> AddedComponents;
        std::map<CChangeComponentKey, std::set<std::string>> RemovedComponents;
        std::set<CChangePropertyKey> ModifiedProperties;
        std::map<CChangeComponentKey, std::pair<bool, bool>> ModifiedComponentStates;

        bool IsEmpty() const
        {
            return CreatedEntities.empty() && DeletedEntities.empty()
                && AddedComponents.empty() && RemovedComponents.empty()
                && ModifiedProperties.empty() && ModifiedComponentStates.empty();
        }
    };

    class CChangeSetKeysBuilder final
    {
    public:
        explicit CChangeSetKeysBuilder(EOperationBatchKind Kind_) : m_Kind(Kind_) {}

        void RecordAddEntity(const CChangeEntityKey& Key_)
        {
            if (m_Kind == EOperationBatchKind::LoadBaseline) return;
            if (m_Keys.DeletedEntities.erase(Key_) > 0) return;
            m_Keys.CreatedEntities.insert(Key_);
        }

        void RecordDeleteEntity(const CChangeEntityKey& Key_)
        {
            if (m_Kind == EOperationBatchKind::LoadBaseline) return;
            if (m_Keys.CreatedEntities.erase(Key_) > 0)
            {
                std::erase_if(m_Keys.AddedComponents,
                    [&](const auto& Item_) { return Item_.first.EntityID == Key_.EntityID; });
                std::erase_if(m_Keys.RemovedComponents,
                    [&](const auto& Item_) { return Item_.first.EntityID == Key_.EntityID; });
                std::erase_if(m_ModifiedValues,
                    [&](const auto& Item_) { return Item_.first.EntityID == Key_.EntityID; });
                std::erase_if(m_Keys.ModifiedComponentStates,
                    [&](const auto& Item_) { return Item_.first.EntityID == Key_.EntityID; });
                return;
            }
            m_Keys.DeletedEntities.insert(Key_);
        }

        void RecordAddComponent(const CChangeComponentKey& Key_,
            const iCAX::Data::PropertySet& New_)
        {
            if (m_Kind == EOperationBatchKind::LoadBaseline) return;
            EraseComponentChanges(Key_);
            auto& names = m_Keys.AddedComponents[Key_];
            names.clear();
            for (const auto& [name, _] : New_) names.insert(name);
        }

        void RecordRemoveComponent(const CChangeComponentKey& Key_,
            const iCAX::Data::PropertySet& Previous_)
        {
            if (m_Kind == EOperationBatchKind::LoadBaseline) return;
            if (m_Keys.AddedComponents.erase(Key_) > 0)
            {
                EraseComponentChanges(Key_);
                return;
            }
            std::set<std::string> names;
            for (const auto& [name, _] : Previous_) names.insert(name);
            // The owning summary restores first previous values into a
            // removal snapshot. Its key set includes those modified fields.
            for (const auto& [key, _] : m_ModifiedValues)
                if (MatchComponent(key, Key_)) names.insert(key.PropertyName);
            EraseComponentChanges(Key_);
            m_Keys.RemovedComponents[Key_] = std::move(names);
        }

        void RecordModifyComponent(const CChangeComponentKey& Key_,
            const iCAX::Data::PropertySet& Previous_, const iCAX::Data::PropertySet& New_)
        {
            if (m_Kind == EOperationBatchKind::LoadBaseline) return;
            if (auto added = m_Keys.AddedComponents.find(Key_);
                added != m_Keys.AddedComponents.end())
            {
                for (const auto& [name, _] : New_) added->second.insert(name);
                return;
            }
            if (m_Keys.RemovedComponents.contains(Key_)) return;
            for (const auto& [name, value] : New_)
            {
                const auto previous = Previous_.find(name);
                if (previous == Previous_.end()) continue;
                CChangePropertyKey key{Key_.EntityID, Key_.ComponentClass, name};
                const auto change = m_ModifiedValues.find(key);
                if (change == m_ModifiedValues.end())
                {
                    if (previous->second != value)
                        m_ModifiedValues.emplace(std::move(key),
                            CValues{&previous->second});
                }
                else
                {
                    if (*change->second.Previous == value) m_ModifiedValues.erase(change);
                }
            }
        }

        void RecordComponentEnabledState(const CChangeComponentKey& Key_,
            bool Previous_, bool New_)
        {
            if (m_Kind == EOperationBatchKind::LoadBaseline || Previous_ == New_)
                return;
            if (m_Keys.AddedComponents.contains(Key_) || m_Keys.RemovedComponents.contains(Key_))
                return;
            const auto change = m_Keys.ModifiedComponentStates.find(Key_);
            if (change == m_Keys.ModifiedComponentStates.end())
                m_Keys.ModifiedComponentStates.emplace(Key_, std::pair{Previous_, New_});
            else
            {
                change->second.second = New_;
                if (change->second.first == New_) m_Keys.ModifiedComponentStates.erase(change);
            }
        }

        CChangeSetKeys Build() &&
        {
            for (const auto& [key, _] : m_ModifiedValues)
                m_Keys.ModifiedProperties.insert(key);
            return std::move(m_Keys);
        }

    private:
        static bool MatchComponent(const CChangePropertyKey& Key_,
            const CChangeComponentKey& Component_)
        {
            return Key_.EntityID == Component_.EntityID
                && Key_.ComponentClass == Component_.ComponentClass;
        }

        void EraseComponentChanges(const CChangeComponentKey& Key_)
        {
            m_Keys.ModifiedComponentStates.erase(Key_);
            std::erase_if(m_ModifiedValues,
                [&](const auto& Item_) { return MatchComponent(Item_.first, Key_); });
        }

        struct CValues
        {
            const iCAX::Data::PropertyValue* Previous;
        };
        EOperationBatchKind m_Kind;
        CChangeSetKeys m_Keys;
        std::map<CChangePropertyKey, CValues> m_ModifiedValues;
    };

    inline CChangeSetKeys BuildChangeSetKeys(const COperationBatch& Batch_)
    {
        // Batch is frozen for this synchronous projection. The temporary
        // builder may borrow its values only to compare net changes. Build()
        // returns owned keys before effects, history or observers can reenter.
        CChangeSetKeysBuilder builder(Batch_.Kind);
        for (const auto& operation : Batch_.Operations)
        {
            const CChangeComponentKey component{operation.EntityID, operation.ComponentClass};
            switch (operation.Type)
            {
            case RepositoryEventArgs::kAddEntity:
                builder.RecordAddEntity({operation.EntityID});
                if (const auto initial = operation.NewProperties.find(kInitialComponentsProperty);
                    initial != operation.NewProperties.end())
                {
                    if (!initial->second.Is<iCAX::Data::VariantArray>())
                        throw std::runtime_error("Entity snapshot components must be an array");
                    for (const auto& value : std::get<iCAX::Data::VariantArray>(initial->second.m_Value))
                    {
                        const auto& record = std::get<iCAX::Data::ObjectMap>(value.m_Value);
                        builder.RecordAddComponent({operation.EntityID,
                            record.at("class").To<std::string>()},
                            std::get<iCAX::Data::ObjectMap>(record.at("properties").m_Value));
                    }
                }
                break;
            case RepositoryEventArgs::kDeleteEntity:
                builder.RecordDeleteEntity({operation.EntityID});
                break;
            case RepositoryEventArgs::kAddComponent:
                builder.RecordAddComponent(component, operation.NewProperties);
                break;
            case RepositoryEventArgs::kRemoveComponent:
                builder.RecordRemoveComponent(component, operation.PreviousProperties);
                break;
            case RepositoryEventArgs::kModifyComponent:
                builder.RecordModifyComponent(component, operation.PreviousProperties, operation.NewProperties);
                break;
            case RepositoryEventArgs::kEnableComponent:
            case RepositoryEventArgs::kDisableComponent:
                builder.RecordComponentEnabledState(component,
                    operation.PreviousEnabled.value_or(operation.Type == RepositoryEventArgs::kDisableComponent),
                    operation.NewEnabled.value_or(operation.Type == RepositoryEventArgs::kEnableComponent));
                break;
            default:
                throw std::runtime_error("Invalid repository operation type");
            }
        }
        return std::move(builder).Build();
    }
}
