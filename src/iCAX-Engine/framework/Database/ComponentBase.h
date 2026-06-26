#pragma once
#include "Database.h"
#include "IComponentEvent.h"
#include <cstdint>

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 组件基类
        * @remark 
        *   1、一个实体上，同类型（strClassName取值相同）的组件只能添加一个
        *   2、组件不可独立存在，其作为实体某个切面的描述存在
        *   3、修改组件的属性字段时会触发属性修改事件，Repository 会将其汇总为仓储事件，用于撤销还原、日志和派生字段失效
        *   4、提供了OnSetPropertyT<T>方法，用于子类实现设置属性值时按需发布事件（此处类似C#INotifier接口的实现）
        */
        class _DATABASE_EXP CComponentBase : public IComponentEventPublisher, public std::enable_shared_from_this<CComponentBase>
        {
        public:
            static constexpr const char* S_ClassName = "CComponentBase";
        public:
            /*
            * @brief 构造函数
            * @param [in] pEntity_
            */
            CComponentBase(IN std::shared_ptr<class IEntity> pEntity_);

            /*
            * @brief 析构函数
            */
            virtual ~CComponentBase();

        public:
            /*
            * @brief 获取类名
            * @return std::string
            */
            virtual std::string GetComponentClass() const = 0;

        public:
            /*
            * @brief 设置属性
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 值
            * @details
            *   会在写入前后触发 ComponentChanging/ComponentChanged。
            *   字段是否真正触发事件、版本或日志，最终由字段 meta 的 ChangePolicy 决定。
            */
            [[nodiscard]] bool SetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_, OUT std::string& strError_);
            [[nodiscard]] bool SetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_);

            /*
            * @brief 批量设置
            * @param [in] Properties_
            * @remark
            *   此处不可以通过调用 SetProperty 来实现，因为 SetProperty 会逐字段触发修改。
            *   SetProperties 用于回放、加载和批量恢复已有状态。
            */
            [[nodiscard]] bool SetProperties(IN const PropertySet& Properties_, OUT std::string& strError_);
            [[nodiscard]] bool SetProperties(IN const PropertySet& Properties_);

            /*
            * @brief 获取属性
            * @return 当前组件全部属性集合。
            */
            virtual PropertySet GetProperties() const;

            /*
            * @brief 获取属性名称列表
            * @return std::vector<std::string>
            */
            virtual std::vector<std::string> GetPropertyNameArray() const = 0;

            /*
            * @brief 获取属性值
            * @return strPropertyName_
            */
            virtual PropertyValue GetProperty(IN const std::string& strPropertyName_) const = 0;

            /*
            * @brief 获取所在实体
            * @return std::shared_ptr<class IEntity>
            */
            std::shared_ptr<class IEntity> GetEntity() const;

        public:
            /*
            * @brief 启用
            * @details 组件默认处于启用状态；从禁用状态切回启用时会触发组件启用事件。
            */
            void Enable();

            /*
            * @brief 禁用
            * @details 从启用状态切到禁用时会触发组件禁用事件；Behaviour 帧更新阶段会跳过禁用组件。
            */
            void Disable();

            /*
            * @brief 是否启用
            * @return bool
            */
            bool IsEnable() const;

            /*
            * @brief 获取版本号
            * @return 组件在 Repository 版本表中的版本号；脱离 Repository 时返回 0。该版本可直接作为 PDO dataVersion。
            */
            uint64_t Version() const;

            /*
            * @brief 是否发生了更改
            * @return true 表示本帧或当前统计周期内组件被标记为 changed。
            */
            bool IsChanged() const;

            /*
            * @berief 删除
            */
            void Delete();

            /*
            * @brief 是否被标记删除
            * @return bool
            */
            bool IsDeleted() const;

        protected:
            /*
            * @brief 当设置属性
            * @param [in] strPropertyName_
            * @param [in] NewValue_
            */
            virtual void OnSetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_) = 0;

        protected:
            std::weak_ptr<class IEntity> m_pEntity;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IComponentEventListener> Observer_) override;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IComponentEventListener> Observer_) override;

        protected:
            /*
            * @brief 预通知
            * @param [in] nEventType_ 属性名
            * @param [in] Previous_ 旧值
            * @param [in] New_ 新值
            */
            void TriggerComponentChanging(IN const ComponentEventArgs::EventType& nEventType_, IN const PropertySet& Previous_, IN const PropertySet& New_);

            /*
            * @brief 预通知
            * @param [in] nEventType_ 属性名
            * @param [in] Previous_ 旧值
            * @param [in] New_ 新值
            */
            void TriggerComponentChanged(IN const ComponentEventArgs::EventType& nEventType_, IN const PropertySet& Previous_, IN const PropertySet& New_);

        private:
            std::list<std::weak_ptr<IComponentEventListener>> m_Observers;      //!< 观察者
            bool m_bEnable;                                                     //!< 是否启用
            bool m_bDeleted;                                                    //!< 标记删除

        protected:
            struct _DATABASE_EXP ComponentChangeNotifier final
            {
                /*
                * @brief 构造组件修改通知器。
                * @details 构造时发送 Changing，析构时在无异常或异常数未增加时发送 Changed。
                */
                ComponentChangeNotifier(IN CComponentBase* pBase_, IN ComponentEventArgs::EventType nType_, IN const PropertySet& Previous_, IN const PropertySet& New_);
                ~ComponentChangeNotifier();
                ComponentChangeNotifier(IN const ComponentChangeNotifier&) = delete;
                ComponentChangeNotifier& operator=(IN const ComponentChangeNotifier&) = delete;
                ComponentChangeNotifier(IN ComponentChangeNotifier&&) = delete;
                ComponentChangeNotifier& operator=(IN ComponentChangeNotifier&&) = delete;
                CComponentBase* pBase;
                ComponentEventArgs::EventType nType;
                PropertySet Previous;
                PropertySet New;
                bool bEnabled;
                int nUncaughtExceptionCount;
            };

            struct _DATABASE_EXP ComponentChangeNotificationSuppressor final
            {
                /*
                * @brief 临时抑制组件事件。
                * @details 用于回放或批量状态恢复，避免内部写入再次触发事件。
                */
                ComponentChangeNotificationSuppressor(IN CComponentBase* pBase_);
                ~ComponentChangeNotificationSuppressor();
                ComponentChangeNotificationSuppressor(IN const ComponentChangeNotificationSuppressor&) = delete;
                ComponentChangeNotificationSuppressor& operator=(IN const ComponentChangeNotificationSuppressor&) = delete;
                ComponentChangeNotificationSuppressor(IN ComponentChangeNotificationSuppressor&&) = delete;
                ComponentChangeNotificationSuppressor& operator=(IN ComponentChangeNotificationSuppressor&&) = delete;
                CComponentBase* pBase;
            };

            bool IsComponentChangeNotificationSuppressed() const;

        //public:
        //    mutable uint32_t m_bDirtyFlag;//! 用于标记哪些派生字段需要重新计算，子类具体实现，此处只是给出建议
        private:
            int m_nComponentChangeNotificationSuppressionDepth;
        };
    }
}
