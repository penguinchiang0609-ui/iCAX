#pragma once
#include "Database.h"
#include "Data/uuid.h"
#include <string>
#include "ComponentBase.h"
#include "IEntityEvent.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 实体
        * @remark
        *   1、实体只有ID与类型信息，不直接拥有其他信息
        *   2、实体的立体描述有一系列承载各个切面信息的组件构成
        */
        class _DATABASE_EXP IEntity : public IEntityEventPublisher
        {
        public:
            IEntity() = default;
            virtual ~IEntity() = default;
            //!< 禁用
            IEntity(IN const IEntity&) = delete;
            IEntity(IN const IEntity&&) = delete;
            IEntity& operator=(IN const IEntity&) = delete;
            IEntity& operator=(IN const IEntity&&) = delete;

        public:
            /*
            * @brief 获取ID
            * @return const iCAX::Data::uuid&
            */
            virtual const iCAX::Data::uuid& GetID() const = 0;

        public://!< 组件相关操作
            /*
            * @brief 添加或替换组件
            * @param [in] strClassName_
            * @return std::shared_ptr<CComponentBase>
            */
            virtual std::shared_ptr<CComponentBase> AddComponent(IN const std::string& strClassName_) = 0;

            /*
            * @brief 移除组件
            * @param [in] strClassName_
            */
            virtual void RemoveComponent(IN const std::string& strClassName_) = 0;

            /*
            * @brief 获取组件
            * @param [in] strClassName_
            * @return  std::shared_ptr<CComponentBase>
            */
            virtual std::shared_ptr<CComponentBase> GetComponent(IN const std::string& strClassName_) const = 0;

            /*
            * @brief 获取组件集
            * @param [in] strClassName_ 父类
            * @return  std::shared_ptr<CComponentBase>
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetComponents(IN const std::string& strClassName_) const = 0;

            /*
            * @brief 是否含有指定组件
            * @param [in] strClassName_
            * @return bool
            */
            virtual bool HasComponent(IN const std::string& strClassName_) const = 0;

            /*
            * @brief 获取组件类型列表
            * @return std::vector<std::string>
            * @remark 此处顺序是添加的顺序，从而保证反序可以移除成功
            */
            virtual std::vector<std::string> GetComponentClasses() const = 0;

            /*
            * @brief 获取组件数量
            * @return int
            */
            virtual int ComponentsCount() const = 0;

            /*
            * @brief 清除所有组件
            */
            virtual void Cleanup() = 0;

            /*
            * @brief 获取所在仓储
            * @return std::shared_ptr<class IRepository>
            */
            virtual class std::shared_ptr<class IRepository> GetRepository() const = 0;

            /*
            * @brief 获取组件
            * @param [in] strClassName_
            * @return  std::shared_ptr<CComponentBase>
            */
            template<typename T>
            std::shared_ptr<T> GetComponent() const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                std::shared_ptr<CComponentBase> _p = GetComponent(T::S_ClassName);
                if (!_p)
                {
                    return nullptr;
                }
                return std::dynamic_pointer_cast<T>(_p);
            }

            /*
            * @brief 获取组件
            * @param [in] strClassName_
            * @return bool
            */
            template<typename T>
            bool HasComponent() const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return HasComponent(T::S_ClassName);
            }

            /*
            * @brief 添加或替换组件
            * @param [in] strClassName_
            * @return std::shared_ptr<CComponentBase>
            */
            template<typename T>
            std::shared_ptr<T> AddComponent()
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                std::shared_ptr<CComponentBase> _p = AddComponent(T::S_ClassName);
                if (!_p)
                {
                    return nullptr;
                }
                return std::dynamic_pointer_cast<T>(_p);
            }

            /*
            * @brief 移除组件
            * @param [in] strClassName_
            */
            template<typename T>
            void RemoveComponent()
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                RemoveComponent(T::S_ClassName);
            }
        };
    }
}
