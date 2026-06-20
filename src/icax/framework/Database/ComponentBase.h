#pragma once
#include "Database.h"
#include "IComponentEvent.h"

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
            */
            void SetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_);

            /*
            * @brief 批量设置
            * @param [in] Properties_
            * @remark 此处不可以通过调用SetProperty来实现，因为SetProperty会触发修改
            */
            void SetProperties(IN const PropertySet& Properties_);

            /*
            * @brief 获取属性
            * @return PropertySet
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
            */
            void Enable();

            /*
            * @brief 禁用
            */
            void Disable();

            /*
            * @brief 是否启用
            * @return bool
            */
            bool IsEnable() const;

            /*
            * @brief 获取版本号
            * @return size_t
            */
            size_t Version() const;

            /*
            * @brief 是否发生了更改
            * @return bool
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
