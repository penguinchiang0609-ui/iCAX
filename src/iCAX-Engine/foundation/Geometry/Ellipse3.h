#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维完整椭圆类
        * @details
        * Ellipse3 表示三维空间中位于某个平面上的完整椭圆（闭合曲线）。
        * - 参数化区间固定为 [0, 1]，对应角度范围 [0, 2π]。
        * - 可直接用于 Loop3 或 Region3 的闭合轮廓。
        * - 支持缩放、剪切、反转等几何变换。
        */
        class _GEOMETRY_EXP Ellipse3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造完整椭圆
            * @param [in] ptCenter_ 椭圆中心
            * @param [in] nMajorRadius_ 长半轴长度
            * @param [in] nMinorRadius_ 短半轴长度
            * @param [in] bCCW_ 是否逆时针方向
            * @param [in] dirNormal_ 椭圆所在平面的法向量
            * @param [in] dirRight_ 椭圆起始点方向（对应角度 0）
            * @details
            * 构造一个以 ptCenter_ 为圆心、在法向为 dirNormal_ 的平面上定义的完整椭圆。
            */
            Ellipse3(IN const Point3& ptCenter_,
                IN const double& nMajorRadius_,
                IN const double& nMinorRadius_,
                IN const bool& bCCW_ = true,
                IN const Dir3& dirNormal_ = Dir3::Up(),
                IN const Dir3& dirRight_ = Dir3::Right());

            /**
            * @brief 析构函数
            * @details 默认析构，无需特殊资源释放。
            */
            virtual ~Ellipse3();

        public: //!< 成员访问接口
            /**
            * @brief 获取长半轴
            * @return const double& 长半轴长度
            */
            const double& MajorRadius() const;

            /**
            * @brief 获取短半轴
            * @return const double& 短半轴长度
            */
            const double& MinorRadius() const;

            /**
            * @brief 设置椭圆尺寸
            * @param [in] nMajorRadius_ 长半轴长度
            * @param [in] nMinorRadius_ 短半轴长度
            */
            void SetSize(IN const double& nMajorRadius_, IN const double& nMinorRadius_);

            /**
            * @brief 将参数 u 映射为极角 θ
            * @param [in] nU_ 曲线参数（范围 [0,1]）
            * @return double 对应的极角（单位：弧度）
            */
            double GetTheta(IN const double& nU_) const;

        public: //!< BndCurve3 接口
            /**
            * @brief 获取曲线朝向类型
            * @param [in] dirNormal_ 法向
            * @return 曲线朝向类型
            * @details 根据法向与椭圆方向判断顺/逆时针。
            */
            virtual CurveOrientType Orientation(IN const Dir3& dirNormal_) const override;

        public: //!< Curve3 接口
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 对于椭圆，uStart = 0。
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 对于椭圆，uEnd = 1。
            */
            virtual double EndParam() const override;

            /**
            * @brief 计算参数对应的空间点
            * @param [in] nU_ 曲线参数
            * @return 对应的三维点坐标
            * @note 参数范围为 [0,1]，内部自动映射到 [0, 2π]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 参数方向取反后，几何上保持不变。
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

        public: //!< Geometry3 接口实现
            /**
            * @brief 对椭圆进行缩放和剪切变换
            * @param [in] nScale_ 缩放比例
            * @param [in] nShear_ 剪切系数
            * @param [in] bMirrorXoZ_ 是否沿 X 或 Z 轴镜像
            */
            virtual void ScaleAndShear(IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorXoZ_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆当前椭圆
            * @return 新的几何对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            double m_nMajorRadius;   //!< 长半轴长度
            double m_nMinorRadius;   //!< 短半轴长度
            bool m_bCCW;             //!< 是否逆时针
        };
    }
}
