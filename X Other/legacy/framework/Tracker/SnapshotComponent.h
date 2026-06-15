//#pragma once
//#include "../Database/Component.h"
//#include "pch.h"
//
//namespace iCAX
//{
//    namespace Tracker
//    {
//        /*
//        * @brief 数据快照组件
//        */
//        class CSnapshotComponent : public iCAX::Database::CComponentBase
//        {
//        public:
//            /*
//            * @brief 构造函数
//            * @param [in] pEntity_
//            */
//            CSnapshotComponent(IN std::weak_ptr<class IEntity> pEntity_);
//
//            /*
//            * @brief 析构函数
//            */
//            virtual ~CSnapshotComponent();
//
//        public:
//            /*
//            * @brief 获取类名
//            * @return std::string
//            */
//            virtual std::string GetComponentClass() const override;
//
//            /*
//            * @brief 获取属性名称列表
//            * @return std::vector<std::string>
//            */
//            virtual std::vector<std::string> GetPropertyNameArray() const override;
//
//            /*
//            * @brief 获取属性值
//            * @return strPropertyName_
//            */
//            virtual iCAX::Database::PropertyValue GetProperty(IN const std::string& strPropertyName_) const override;
//
//        protected:
//            /*
//            * @brief 当设置属性
//            * @param [in] strPropertyName_
//            * @param [in] NewValue_
//            */
//            virtual void OnSetProperty(IN const std::string& strPropertyName_, IN const iCAX::Database::PropertyValue& NewValue_) override;
//
//        private:
//            bool m_bSupportSnapshot;                    //!< 是否支持快照
//            std::string m_SnapshotName;                 //!< 快照名称
//
//        };
//    }
//}