#pragma once
#include "Math.h"
#include "../Data/Array2.h"
#include "Point2.h"
#include "Vector2.h"
#include "Dir2.h"
#include <tuple>

using namespace iCAX::Data;

namespace iCAX
{
    namespace Math
    {
        /**
        * @brief 二维仿射变换矩阵类 (2D Transformation)
        * @details
        * 封装二维空间的仿射变换，内部使用 3×3 齐次坐标矩阵表示：
        *
        *      [ m00 m01 m02 ]
        *      [ m10 m11 m12 ]
        *      [ m20 m21 m22 ]
        *
        * 可实现平移、旋转、缩放、镜像、错切等操作，并支持与 Point2、Vector2、Axis2、CoordSys2 等几何对象联动。
        */
        class _MATH_EXP Tranform2 final
        {
        public:
            //------------构造与析构-------------

            /**
            * @brief 默认构造函数
            * @details 初始化为单位矩阵（不执行任何变换）
            */
            Tranform2();

            /**
            * @brief 使用 9 个矩阵元素构造
            * @param [in] m00_ ~ m22_ 矩阵元素
            * @details 用于显式构建自定义仿射矩阵
            */
            Tranform2(IN const double& m00_, IN const double& m01_, IN const double& m02_,
                IN const double& m10_, IN const double& m11_, IN const double& m12_,
                IN const double& m20_, IN const double& m21_, IN const double& m22_);

            /**
            * @brief 使用 3x3 矩阵构造
            * @param [in] mat 输入矩阵
            */
            Tranform2(IN const Double3x3& mat);

            /**
            * @brief 析构函数
            */
            ~Tranform2();

        public: //------------矩阵元素访问-------------

            /**
            * @brief 获取/修改 m00 元素
            * @return double& m00 引用
            */
            double& m00();

            /**
            * @brief 获取 m00 元素（只读）
            * @return const double& m00 引用
            */
            const double& m00() const;

            /**
            * @brief 获取/修改 m01 元素
            * @return double& m01 引用
            */
            double& m01();

            /**
            * @brief 获取 m01 元素（只读）
            * @return const double& m01 引用
            */
            const double& m01() const;

            /**
            * @brief 获取/修改 m02 元素
            * @return double& m02 引用
            */
            double& m02();

            /**
            * @brief 获取 m02 元素（只读）
            * @return const double& m02 引用
            */
            const double& m02() const;

            /**
            * @brief 获取/修改 m10 元素
            * @return double& m10 引用
            */
            double& m10();

            /**
            * @brief 获取 m10 元素（只读）
            * @return const double& m10 引用
            */
            const double& m10() const;

            /**
            * @brief 获取/修改 m11 元素
            * @return double& m11 引用
            */
            double& m11();

            /**
            * @brief 获取 m11 元素（只读）
            * @return const double& m11 引用
            */
            const double& m11() const;

            /**
            * @brief 获取/修改 m12 元素
            * @return double& m12 引用
            */
            double& m12();

            /**
            * @brief 获取 m12 元素（只读）
            * @return const double& m12 引用
            */
            const double& m12() const;

            /**
            * @brief 获取/修改 m20 元素
            * @return double& m20 引用
            */
            double& m20();

            /**
            * @brief 获取 m20 元素（只读）
            * @return const double& m20 引用
            */
            const double& m20() const;

            /**
            * @brief 获取/修改 m21 元素
            * @return double& m21 引用
            */
            double& m21();

            /**
            * @brief 获取 m21 元素（只读）
            * @return const double& m21 引用
            */
            const double& m21() const;

            /**
            * @brief 获取/修改 m22 元素
            * @return double& m22 引用
            */
            double& m22();

            /**
            * @brief 获取 m22 元素（只读）
            * @return const double& m22 引用
            */
            const double& m22() const;

            /**
            * @brief 获取内部矩阵引用，可修改
            * @return Double3x3& 内部 3x3 矩阵引用
            */
            Double3x3& Mat();

            /**
            * @brief 获取内部矩阵引用（只读）
            * @return const Double3x3& 内部 3x3 矩阵引用
            */
            const Double3x3& Mat() const;

            /*
            * @brief 计算线性部分矩阵行列式，即旋转缩放剪切部分，不考虑平移（防止在透视矩阵下，影响判断）
            * @return double
            */
            double LinearDeterminant() const;

        public: //------------矩阵运算-------------
            /*
            * @brief 判断两个向量是否相等
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsEqual(IN const Tranform2& Other_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 矩阵乘法
            * @param [in] tOther_ 右乘矩阵
            * @return Tranform2 复合变换矩阵 (this × tOther_)
            */
            Tranform2 operator*(IN const Tranform2& tOther_) const;

            /**
            * @brief 原地求逆矩阵
            * @return Tranform2& 自身引用（逆矩阵）
            */
            Tranform2& Inverse();

            /**
            * @brief 返回逆矩阵
            * @return Tranform2 逆矩阵
            */
            Tranform2 Inversed() const;

            /**
            * @brief 判断是否为单位矩阵
            * @param [in] nTol_ 容差（默认 EPSILON）
            * @return bool true 表示矩阵为单位矩阵
            */
            bool IsIdentity(IN const double& nTol_ = EPSILON) const;

        public: //------------几何应用-------------

            /**
            * @brief 将变换应用到点
            * @param [in] ptTarget_ 输入点
            * @return Point2 变换后的点
            */
            Point2 Applied(IN const Point2& ptTarget_) const;

            /**
            * @brief 将变换应用到向量
            * @param [in] vTarget_ 输入向量
            * @return Vector2 变换后的向量
            * @remark 仅受线性部分影响，不受平移影响
            */
            Vector2 Applied(IN const Vector2& vTarget_) const;

            /**
            * @brief 将变换应用到方向
            * @param [in] dirTarget_ 输入方向
            * @return Dir2 变换后的方向
            * @remark 仅受线性部分影响，不受平移影响
            */
            Dir2 Applied(IN const Dir2& dirTarget_) const;

            /*
            * @brief 是否存在非刚性变换
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool HasNonUniformScaling(IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 是否存在剪切
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool HasShear(IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 解压出平移、旋转、缩放、剪切
            * @return std::tuple<
            *      Double2 translation,      //!< 平移分量
            *      double rotation,          //!< 旋转欧拉角
            *      Double2 shear,            //!< 剪切矩阵，或者用 Vector3 表示各轴剪切
            *      Double2 scale,            //!< 缩放因子
            *      bool                      //!< 是否对X轴镜像
            * >
            * @detail
            *   合并的顺序：Mirror → Scale → Shear → Rotation → Translation
            */
            std::tuple<Double2, double, Double2, Double2, bool> Decompose() const;

        public: //!< 快速构建

            /**
            * @brief 单位矩阵
            * @return Tranform2 单位矩阵
            */
            static Tranform2 Identity();

            /**
            * @brief 平移矩阵
            * @param [in] vOffset_ 平移向量
            * @return Tranform2 平移矩阵
            */
            static Tranform2 Translate(IN const Double2& vOffset_);

            /**
            * @brief 绕原点旋转矩阵
            * @param [in] nRad_ 弧度
            * @return Tranform2 旋转矩阵
            */
            static Tranform2 Rotate(IN const double& nRad_);

            /**
            * @brief 原点缩放矩阵
            * @param [in] sXY_ 缩放因子 {sx, sy}
            * @return Tranform2 缩放矩阵
            */
            static Tranform2 Scale(IN const Double2& sXY_);

            /**
            * @brief 切变矩阵
            * @param [in] nXY_ 切变因子 {shxy(X对Y的影响), shyx（Y对X的影响）}
            * @return Tranform2 切变矩阵
            */
            static Tranform2 Shear(IN const Double2& nXY_);

            /**
            * @brief 以指定点为中心旋转
            * @param [in] ptCenter_ 中心点
            * @param [in] nRad_ 弧度
            * @return Tranform2 旋转矩阵
            */
            static Tranform2 Rotate(IN const Point2& ptCenter_, IN const double& nRad_);

            /**
            * @brief 以指定点为中心缩放
            * @param [in] ptCenter_ 中心点
            * @param [in] sXY_ 缩放因子 {sx, sy}
            * @return Tranform2 缩放矩阵
            */
            static Tranform2 Scale(IN const Point2& ptCenter_, IN const Double2& sXY_);

            /**
            * @brief 轴镜像矩阵
            * @param [in] ptCenter_
            * @param [in] dir_
            * @return Tranform2 镜像矩阵
            */
            static Tranform2 Mirror(IN const Point2& ptCenter_, IN const Dir2& dir_);

            /**
            * @brief 点镜像矩阵
            * @param [in] ptCenter_ 镜像中心
            * @return Tranform2 镜像矩阵
            */
            static Tranform2 Mirror(IN const Point2& ptCenter_);

            /**
            * @brief x 轴镜像矩阵
            * @return Tranform2 镜像矩阵
            */
            static Tranform2 MirrorX();

            /**
            * @brief y 轴镜像矩阵
            * @return Tranform2 镜像矩阵
            */
            static Tranform2 MirrorY();

        private:
            Double3x3 m_Matrix; ///< 内部 3x3 矩阵存储
        };
    }
}
