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
        * @brief 二维正星形（非自相交）
        * @details
        * Star2 表示一个二维正星形，由外顶点和内顶点交替组成。
        * - 局部坐标系原点为星形中心
        * - 外顶点半径 outerRadius
        * - 内顶点半径 innerRadius
        * - nPoints 定义星形尖角数（外顶点数量）
        * - bCCW 控制顺/逆时针方向
        *
        * 参数化定义：
        * - 参数区间 [0, 2*nPoints)
        * - 每个整数段对应一条边，Evaluate(u) 根据边线性插值计算点位置
        *
        * 示例：
        * \code
        * Star2 star(5, 10.0, 5.0);
        * Point2 p = star.Evaluate(2.5);
        * \endcode
        */
        class _GEOMETRY_EXP Star2 final : public MltBndCurve2
        {
        public:
            /**
            * @brief 构造函数
             * @param [in] ptCenter_ 中心坐标
            * @param [in] nPoints_ 星形尖角数 (>=3)
            * @param [in] nOuterRadius_ 外顶点半径 (>0)
            * @param [in] nInnerRadius_ 内顶点半径 (>=0, < outerRadius_)
            * @param [in] bCCW_ 是否逆时针方向（默认 true）
            * @param [in] dirRight_ Width所在方向
            */
            Star2(IN const Point2& ptCenter_, IN const int nPoints_, IN const double nOuterRadius_, IN const double nInnerRadius_, IN const bool bCCW_ = true, IN const Dir2& dirRight_ = Dir2::Right());

            /**
            * @brief 析构函数
            */
            virtual ~Star2();

        public: //!< 属性访问接口
            /**
            * @brief 获取星形尖角数
            * @return 尖角数
            */
            int Points() const;

            /**
            * @brief 获取外顶点半径
            * @return 外顶点半径
            */
            const double& OuterRadius() const;

            /**
            * @brief 获取内顶点半径
            * @return 内顶点半径
            */
            const double& InnerRadius() const;

            /**
            * @brief 设置尖角数
            * @param [in] nPoints_ 新尖角数 (>=3)
            */
            void SetPoints(IN const int nPoints_);

            /**
            * @brief 设置外顶点半径
            * @param [in] outerRadius_ 新外顶点半径 (>0)
            */
            void SetOuterRadius(IN const double outerRadius_);

            /**
            * @brief 设置内顶点半径
            * @param [in] innerRadius_ 新内顶点半径 (>=0, < outerRadius_)
            */
            void SetInnerRadius(IN const double innerRadius_);

        public://!< Curve2 接口
            /**
            * @brief 获取曲线朝向类型
            * @return CurveOrientType2，表示顺时针或逆时针
            */
            virtual CurveOrientType Orientation() const override;

            /**
            * @brief 反转方向
            * @details 反转曲线方向，会影响 Evaluate/Tangent/Orientation
            */
            virtual void Reverse() override;

        public: //!< Geometry2 接口
            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_
            */
            virtual void ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口
            /**
            * @brief 克隆对象
            * @return 新的 Star2 智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            /**
            * @brief 重新计算缓存多边形
            * @details 根据当前尖角数、内外半径和方向生成 Polyline2
            */
            void ReCalcCache();

        private:
            int m_nPoints;                  //!< 星形尖角数
            double m_nOuterRadius;          //!< 外顶点半径
            double m_nInnerRadius;          //!< 内顶点半径
            bool m_bCCW;                    //!< 是否逆时针方向
        };
    }
}
