#pragma once
#include "GeometryPrimative.h"
#include "MltBndCurve2.h"
#include "Polyline2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维正多边形
        * @details
        * RegularPolygon2 表示一个二维正多边形，由以下参数定义：
        * - 局部坐标系原点为多边形中心
        * - nEdges 为边数 (>=3)
        * - nRadius 为外接圆半径
        * - bCCW 控制顺/逆时针方向
        *
        * 参数化定义：
        * - [0, nEdges)：每个整数段对应一条边
        * - Evaluate(u) 根据 u 所在段线性插值计算顶点间位置
        *
        * 支持接口：
        * - BndCurve2：StartParam/EndParam
        * - Curve2：Evaluate/Tangent/Orientation/Reverse
        * - Geometry2：Transform
        * - Geometry：Clone
        */
        class _GEOMETRY_EXP RegularPolygon2 final : public MltBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_ 中心坐标
            * @param [in] nEdges_ 边数 (>=3)
            * @param [in] nRadius_ 外接圆半径 (>0)
            * @param [in] csLocal_ 局部坐标系
            * @param [in] bCCW_ 是否逆时针方向
            * @param [in] dirRight_ Width所在方向
            */
            RegularPolygon2(IN const Point2& ptCenter_, IN const int nEdges_, IN const double nRadius_, IN const bool bCCW_ = true, IN const Dir2& dirRight_ = Dir2::Right());

            /**
            * @brief 析构函数
            */
            virtual ~RegularPolygon2();

        public: //!< 属性访问
            /**
            * @brief 获取边数
            */
            int Edges() const;

            /**
            * @brief 获取外接圆半径
            */
            const double& Radius() const;

            /**
            * @brief 设置边数
            * @param [in] nEdges_
            */
            void SetEdges(IN const int nEdges_);

            /**
            * @brief 设置外接圆半径
            * @param [in] nRadius_
            */
            void SetRadius(IN const double nRadius_);

        public://!< Curve2 接口
            /*
            * @brief 获取曲线朝向类型
            * @return CurveOrientType
            */
            virtual CurveOrientType Orientation() const override;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

        public: //!< Geometry2 接口实现
            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_
            */
            virtual void ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆
            * @return 新的对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            /*
            * @brief 重新计算缓存数据
            */
            void ReCalcCache();

        private:
            int m_nEdges;           //!< 边数
            double m_nRadius;       //!< 外接圆半径
            bool m_bCCW;            //!< 是否逆时针方向
        };
    }
}
