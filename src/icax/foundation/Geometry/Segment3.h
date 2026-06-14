#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维直线段
        * @details
        * Segment3 表示局部坐标系下的一条三维有界线段。
        * - 参数化区间：u ∈ [0,1]，其中 u=0 对应起点，u=1 对应终点。
        * - 支持仿射变换（Transform、ScaleAndShear）。
        * - 支持反转（Reverse），即交换起点与终点方向。
        * - 直线段始终是共线共面的。
        */
        class _GEOMETRY_EXP Segment3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptStart_ 起点（局部坐标）
            * @param [in] ptEnd_   终点（局部坐标）
            */
            Segment3(IN const Point3& ptStart_, IN const Point3& ptEnd_);

            /**
            * @brief 析构函数
            */
            virtual ~Segment3();

        public: //!< 属性访问接口
            /**
            * @brief 设置线段的起点与终点
            * @param [in] ptStart_ 起点（局部坐标）
            * @param [in] ptEnd_   终点（局部坐标）
            * @note 设置后会自动更新线段长度缓存。
            */
            void SetPoints(IN const Point3& ptStart_, IN const Point3& ptEnd_);

        public: //!< BndCurve3 接口（若有需要可在实现中补充）

        public: //!< Curve3 接口重写

            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值，固定为 0.0
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值，固定为 1.0
            */
            virtual double EndParam() const override;

            /**
            * @brief 计算给定参数处的点坐标
            * @param [in] nU_ 曲线参数（范围 [0,1]）
            * @return 对应的三维点坐标（世界坐标系）
            * @note 若参数超出范围，将被自动截取到 [0,1]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转线段方向
            * @details
            * 交换起点与终点，使参数方向相反。
            * 对几何形状无影响，仅影响参数化方向。
            */
            virtual void Reverse() override;

            /**
            * @brief 获取圆弧所在平面
            * @return 若共面，返回对应的 Plane3；否则返回 std::nullopt
            */
            virtual std::optional<Plane3> SuggestPlane() const override;

            /*
            * @brief 判断是否在指定平面内
            * @param [in] pln_
            * @return bool
            */
            virtual bool IsOnPlane(IN const Plane3& pln_) const override;

        public: //!< Geometry3 接口重写
            /**
            * @brief 执行缩放与剪切变换
            * @param [in] nScale_ 缩放向量（x, y, z）
            * @param [in] nShear_ 剪切参数（6维）
            * @param [in] bMirrorX_ 是否X坐标镜像
            * @details
            * 该操作在局部坐标系下对线段端点进行变换。
            */
            virtual void ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口重写

            /**
            * @brief 克隆当前几何对象
            * @return 新的 Segment3 智能指针副本
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            double m_nLength;   //!< 线段长度
            bool   m_bReversed; //!< 是否反向
        };
    }
}
