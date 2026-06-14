#pragma once
#include "GeometryPrimative.h"
#include "MltBndCurve2.h"
#include "PolylineEx2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维圆角矩形
        * @details
        * RoundRect2 表示一个定义在局部坐标系内的二维矩形，其四个角为圆角。
        * - 中心在局部坐标原点
        * - 可以设置宽度、高度和圆角半径
        * - 可通过 Transform() 接口应用二维仿射变换
        *
        * 圆弧或线段组成的圆角矩形，可作为闭合曲线用于几何操作。
        * 逆时针无变换时：
        * [0,1)	下边直线	左下 → 右下	
        * [1,2)	右下圆角	0°→90°	
        * [2,3)	右边直线	下 → 上	
        * [3,4)	右上圆角	90°→180°
        * [4,5)	上边直线	右上 → 左上
        * [5,6)	左上圆角	180°→270°
        * [6,7)	左边直线	上 → 下
        * [7,8)	左下圆角	270°→360°
        */
        class _GEOMETRY_EXP RoundRect2 final : public MltBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_ 中心坐标
            * @param [in] nWidth_ 矩形宽度
            * @param [in] nHeight_ 矩形高度
            * @param [in] nRadius_ 圆角半径
            * @param [in] bCCW_ 是否逆时针
            * @param [in] dirRight_ Width所在方向
            */
            RoundRect2(IN const Point2& ptCenter_, IN const double& nWidth_, IN const double& nHeight_, IN const double& nRadius_, IN const bool& bCCW_ = true, IN const Dir2& dirRight_ = Dir2::Right());

            /**
            * @brief 默认析构函数
            */
            virtual ~RoundRect2();

        public: //!< 属性访问接口
            /**
            * @brief 获取矩形宽度
            * @return const double&
            */
            const double& Width() const;

            /**
            * @brief 获取矩形高度
            * @return const double&
            */
            const double& Height() const;

            /**
            * @brief 获取圆角半径
            * @return const double&
            */
            const double& Radius() const;

            /**
            * @brief 获取矩形四个顶点
            * @return 顶点列表，按顺时针排列
            * @note 返回缓存的 Polygon2 顶点
            */
            const std::vector<Point2> Vertices() const;

            /*
            * @brief 设置尺寸
            * @param [in] nWidth_ 宽度
            * @param [in] nHeight_ 高度
            */
            void SetSize(IN const double& nWidth_, IN const double& nHeight_, IN const double& nRadius_);

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
            /**
            * @brief 重新计算内部缓存多边形
            * @details 当矩形尺寸发生变化或执行仿射变换时调用
            */
            void ReCalcCache();

        private:
            double m_nWidth;   //!< 矩形宽度
            double m_nHeight;  //!< 矩形高度
            double m_nRadius;  //!< 圆角半径
            bool m_bCCW;        //!< 逆时针标记位


        };
    }
}
