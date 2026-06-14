#pragma once
#include "GeometryPrimative.h"
#include "BndCurve3.h"
#include "SglBndCurve3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 多段线
        * @details
        * - 中心在局部坐标原点
        * - 可通过 Transform() 接口应用二维仿射变换
        */
        class _GEOMETRY_EXP MltBndCurve3 : public BndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] nWidth_ 中心线长度
            * @param [in] nRadius_ 半径
            */
            MltBndCurve3(IN const CoordSys3& csLocal_);

            /**
            * @brief 默认析构函数
            */
            virtual ~MltBndCurve3();

        public:
            /**
            * @brief 获取段列表（只读）
            * @return 智能指针段列表
            */
            const std::vector<std::shared_ptr<SglBndCurve3>>& Segments() const;

            /**
            * @brief 动态获取顶点列表
            * @return 顶点列表，由各段的起点组成
            * @note 最后顶点会重复第一个段的起点以保证闭合
            */
            const std::vector<Point3> GetVertices() const;

            /*
            * @brief 解析u得到段以及段内参数
            * @param [in] nU_
            * @param [in] bLeft_ 当nU_位于节点时，向左查找还是向右查找
            * @param [out] pSeg_ 所在段
            * @param [out] nLocalU_ 段内U
            * @detail
            *   1、u位于全局起点且不闭合的场景，则返回首段+首段的起始参数，忽略bLeft_
            *   2、u位于全局终点且不闭合的场景，则返回尾段+尾段的终止参数，忽略bLeft_
            */
            void ResolveU2Segment(IN const double& nU_, IN const bool& bLeft_, OUT std::shared_ptr<SglBndCurve3>& pSeg_, OUT double& nLocalU_) const;

        public://!< BndCurve3 接口

        public://!< Curve3 接口
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间定义由具体曲线类型决定。
            */
            virtual double StartParam() const override final;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常满足 uEnd > uStart。
            */
            virtual double EndParam() const override final;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的二维点坐标
            * @note 参数范围由具体曲线类型定义，例如 [0,1] 或 [0,2π]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override final;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

            /*
            * @brief 判断是否在指定平面内
            * @param [in] pln_
            * @return bool
            */
            virtual bool IsOnPlane(IN const Plane3& pln_) const override final;

        protected:
            /*
            * @brief 重算参数表
            */
            void ReCalcParamTable();

        protected:
            std::vector<std::shared_ptr<SglBndCurve3>> m_lstSegments; //!< 段列表（支持任意单段曲线）
            std::vector<std::pair<double, double>> m_lstParams; //!< 段累加参数表
        };
    }
}
