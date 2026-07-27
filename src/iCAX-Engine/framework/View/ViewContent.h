#pragma once

#include "ViewExport.h"

#include "Data/Variant.h"
#include "Data/uuid.h"
#include "Database/IEntityView.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        class IProjectContext;
        class ISceneContext;
    }

    namespace View
    {
        using ViewDefinitionID = std::string;
        using ViewInstanceID = iCAX::Data::uuid;

        /*
        * @brief 一个 View 输出的公开描述。
        * @details Type 和 Properties 由输出 Provider 定义；Framework 不解释具体协议。
        *   例如渲染输出可以用 Type="render" 并在 Properties 中公开 renderSceneId。
        */
        struct _VIEW_EXP SViewOutputDescriptor final
        {
            std::string Type;
            iCAX::Data::ObjectMap Properties;
        };

        /*
        * @brief 一个 View 对 Scene Entity 的运行时投影结果。
        * @details EntityID 只用于把 View 内容与 Scene 级 PDO 对齐，不持久化为 Entity 的 View 归属。
        *   Presentation 是 View 局部的可选表现覆盖，不会写回 Scene Database。
        */
        struct _VIEW_EXP SViewObject final
        {
            iCAX::Data::uuid EntityID;
            iCAX::Data::ObjectMap Presentation;
        };

        /*
        * @brief View 内容快照。
        * @details 快照是可丢弃、可重建的派生数据。Revision 只在内容发生变化时递增。
        */
        struct _VIEW_EXP SViewContent final
        {
            ViewInstanceID InstanceID;
            ViewDefinitionID DefinitionID;
            uint64_t nRevision = 0;
            std::vector<SViewObject> Objects;
            std::vector<SViewOutputDescriptor> Outputs;
        };

        /*
        * @brief 创建 View 输出会话时的稳定身份和参数。
        */
        struct _VIEW_EXP SViewInstanceInfo final
        {
            ViewInstanceID InstanceID;
            ViewDefinitionID DefinitionID;
            iCAX::Data::ObjectMap Context;
        };

        /*
        * @brief 一个 ViewInstance 拥有的输出会话。
        * @details
        *   会话只消费 View 内容快照；PDO、渲染、表格等资源的创建和释放由具体实现负责。
        *   Synchronize 可以每帧调用，但实现必须使用 revision/version 跳过未变化的数据。
        */
        class _VIEW_EXP IViewOutputSession
        {
        public:
            virtual ~IViewOutputSession() = default;

            virtual SViewOutputDescriptor GetDescriptor() const = 0;

            virtual void Synchronize(
                IN iCAX::Project::IProjectContext& Project_,
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SViewContent& Content_) = 0;

            virtual void Close() = 0;
        };

        /*
        * @brief 产品注册到 View Definition 的输出会话工厂。
        */
        class _VIEW_EXP IViewOutputProvider
        {
        public:
            virtual ~IViewOutputProvider() = default;

            virtual std::unique_ptr<IViewOutputSession> Open(
                IN iCAX::Project::IProjectContext& Project_,
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SViewInstanceInfo& Instance_) const = 0;
        };

        /*
        * @brief 产品可选提供的 View 局部表现解析器。
        * @details
        *   Entity 成员关系由 Database IEntityView 维护；该接口只计算不写回 Database 的表现覆盖。
        */
        class _VIEW_EXP IViewPresentationProvider
        {
        public:
            virtual ~IViewPresentationProvider() = default;

            virtual iCAX::Data::ObjectMap Resolve(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const iCAX::Data::uuid& EntityID_,
                IN const iCAX::Data::ObjectMap& Context_) const = 0;
        };

        struct _VIEW_EXP SViewDefinition final
        {
            ViewDefinitionID ID;
            iCAX::Database::SEntityWhere EntityWhere;
            std::shared_ptr<const IViewPresentationProvider> pPresentationProvider;
            std::vector<std::shared_ptr<const IViewOutputProvider>> OutputProviders;
        };

        /*
        * @brief 通用 View Definition 注册表。
        * @details 只管理不透明 DefinitionID 与 Provider，不包含任何产品枚举或业务判断。
        */
        class _VIEW_EXP CViewDefinitionRegistry final
        {
        public:
            bool Register(IN SViewDefinition Definition_);
            bool HasDefinition(IN const ViewDefinitionID& DefinitionID_) const;
            std::optional<SViewDefinition> GetDefinition(
                IN const ViewDefinitionID& DefinitionID_) const;
            std::vector<ViewDefinitionID> ListDefinitionIDs() const;

        private:
            mutable std::mutex m_Mutex;
            std::map<ViewDefinitionID, SViewDefinition> m_Definitions;
        };
    }
}
