#pragma once
#include "GeometryPrimative.h"
#include "MltBndCurve3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维跑道/胶囊形
        * @details
        * Runway3 表示定义在局部坐标系内的二维跑道形状，由中心矩形段和两端半圆组成。
        * - 中心在局部坐标原点
        * - 可以设置长度和宽度
        * - 可通过 Transform() 接口应用二维仿射变换
        *
        * 该形状常用于模拟飞机跑道、胶囊状物体等几何建模。
        * [0,1)	下直线
        * [1,2)	右半圆弧
        * [2,3)	上直线
        * [3,4)	左半圆弧
        */
        class _GEOMETRY_EXP Runway3 final : public MltBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_ 中心坐标
            * @param [in] nWidth_ 中心线长度
            * @param [in] nRadius_ 半径
            * @param [in] bCCW_ 是否逆时针
            * @param [in] dirNormal_ 法向
            * @param [in] dirRight_ Width所在方向
            */
            Runway3(IN const Point3& ptCenter_, IN const double& nWidth_, IN const double& nRadius_, IN const bool& bCCW_ = true, IN const Dir3& dirNormal_ = Dir3::Up(), IN const Dir3& dirRight_ = Dir3::Right());

            /**
            * @brief 默认析构函数
            */
            virtual ~Runway3();

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

        public: //!< BndCurve3 接口实现
            /**
            * @brief 获取曲线的朝向类型
            * @param [in] dirNormal_ 参考法向
            * @return 曲线朝向类型
            */
            virtual CurveOrientType Orientation(IN const Dir3& dirNormal_) const override;

        public://!< Curve3 接口
            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

            /**
            * @brief 获取曲线所在平面
            * @return 若曲线共面，返回 Plane3；否则返回 std::nullopt
            */
            virtual std::optional<Plane3> SuggestPlane() const override;

        public: //!< Geometry3 接口实现
            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_
            */
            virtual void ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_) override;

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
