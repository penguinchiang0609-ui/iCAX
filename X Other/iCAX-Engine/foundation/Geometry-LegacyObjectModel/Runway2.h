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
        * @brief 二维跑道/胶囊形
        * @details
        * Runway2 表示定义在局部坐标系内的二维跑道形状，由中心矩形段和两端半圆组成。
        * - 中心在局部坐标原点
        * - 可以设置长度和宽度
        * - 可通过 Transform() 接口应用二维仿射变换
        *
        * 该形状常用于模拟飞机跑道、胶囊状物体等几何建模。
        * [0,1)	上直线	左 → 右
        * [1,2)	右半圆弧	上 → 下
        * [2,3)	下直线	右 → 左
        * [3,4)	左半圆弧	下 → 上
        */
        class _GEOMETRY_EXP Runway2 final : public MltBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_ 中心坐标
            * @param [in] nWidth_ 中心线长度
            * @param [in] nRadius_ 半径
            * @param [in] bCCW_ 是否逆时针
            * @param [in] dirRight_ Width所在方向
            */
            Runway2(IN const Point2& ptCenter_, IN const double& nWidth_, IN const double& nRadius_, IN const bool& bCCW_ = true, IN const Dir2& dirRight_ = Dir2::Right());

            /**
            * @brief 默认析构函数
            */
            virtual ~Runway2();

        public: //!< 属性访问接口
            /**
            * @brief 获取中心线长度
            */
            const double& Width() const;

            /**
            * @brief 获取半径
            */
            const double& Radius() const;

            /*
            * @brief 设置尺寸
            * @param [in] nWidth_
            * @param [in] nRadius_
            */
            void SetSize(IN const double& nWidth_, IN const double& nRadius_);

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
            double m_nWidth; //!< 中心线长度
            double m_nRadius; //!< 胶囊半径
            bool m_bCCW;        //!< 逆时针标记位
        };
    }
}
