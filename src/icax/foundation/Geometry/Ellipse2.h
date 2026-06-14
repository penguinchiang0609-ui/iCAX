#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维完整椭圆类
        * @details
        * Ellipse2 表示二维平面上的完整椭圆（闭合曲线）。
        * 继承自 ClosedBoundedCurve2，参数区间固定为 [0, 2*PI]。
        * 可直接用于 Loop2 或 Region2 的闭合轮廓。
        */
        class _GEOMETRY_EXP Ellipse2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造完整椭圆
            * @param [in] ptCenter_ 圆心坐标
            * @param [in] nMajorRadius_ 长半轴长度
            * @param [in] nMinorRadius_ 短半轴长度
            * @param [in] bCCW_ 是否逆时针
            * @param [in] dirRight_ 圆心角为零的方向
            * @details 构造一个完整闭合椭圆对象
            */
            Ellipse2(IN const Point2& ptCenter_, IN const double& nMajorRadius_, IN const double& nMinorRadius_, IN const bool& bCCW_ = true, IN const Dir2& dirRight_ = Dir2::Right());

            /**
            * @brief 析构函数
            * @details 默认析构，无需特殊资源释放
            */
            virtual ~Ellipse2();

        public: //!< 成员访问
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

            /*
            * @brief 设置尺寸
            * @param [in] nMajorRadius_ 长半轴长度
            * @param [in] nMinorRadius_ 短半轴长度
            */
            void SetSize(IN const double& nMajorRadius_, IN const double& nMinorRadius_);

            /*
            * @brief 根据参数得到角度
            * @param [in] nU_
            * @return double
            */
            double GetTheta(IN const double& nU_) const;

        public://!< BndCurve2 接口
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间定义由具体曲线类型决定。
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常满足 uEnd > uStart。
            */
            virtual double EndParam() const override;

        public://!< Curve2 接口
            /*
            * @brief 获取曲线朝向类型
            * @return CurveOrientType
            */
            virtual CurveOrientType Orientation() const override;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的二维点坐标
            * @note 参数范围由具体曲线类型定义，例如 [0,1] 或 [0,2π]。
            */
            virtual Point2 Evaluate(IN const double& nU_) const override;

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
            double m_nMajorRadius;        ///< 长半轴
            double m_nMinorRadius;        ///< 短半轴
            bool m_bCCW;        //!< 逆时针标记位
        };
    }
}
