#pragma once
#include "Math.h"
#include "../Data/Array2.h"
#include "Point3.h"
#include "Vector3.h"
#include "Dir3.h"
#include <tuple>

using namespace iCAX::Data;

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 三维仿射变换类
        * @details
        * Tranform3 表示一个 4x4 齐次矩阵，用于描述 3D 空间中的仿射变换。
        * 支持平移、旋转、缩放、镜像、切变（Shear）等常见几何操作。
        *
        * 典型应用场景包括：
        * - 点与向量坐标变换
        * - 坐标系之间的转换
        * - 组合多个变换（通过矩阵乘法）
        */
        class _MATH_EXP Tranform3 final
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化为单位矩阵。
            */
            Tranform3();
            /**
            * @brief 按元素构造矩阵
            * @param m00_~m33_ 4×4 仿射矩阵的各元素。
            */
            Tranform3(IN const double& m00_, IN const double& m01_, IN const double& m02_, IN const double& m03_,
                double m10_, IN const double& m11_, IN const double& m12_, IN const double& m13_,
                double m20_, IN const double& m21_, IN const double& m22_, IN const double& m23_,
                double m30_, IN const double& m31_, IN const double& m32_, IN const double& m33_);
            /**
            * @brief 从已有矩阵构造
            * @param mat 输入的 4×4 矩阵。
            */
            Tranform3(IN const Double4x4& mat);
            /**
            * @brief 析构函数
            */
            ~Tranform3();

        public:
            /*
            * @brief m00
            * @return double&
            */
            double& m00();
            /*
            * @brief m00
            * @return const double&
            */
            const double& m00() const;
            /*
            * @brief m01
            * @return double&
            */
            double& m01();
            /*
            * @brief m01
            * @return const double&
            */
            const double& m01() const;
            /*
            * @brief m02
            * @return double&
            */
            double& m02();
            /*
            * @brief m02
            * @return const double&
            */
            const double& m02() const;
            /*
            * @brief m03
            * @return double&
            */
            double& m03();
            /*
            * @brief m03
            * @return const double&
            */
            const double& m03() const;
            /*
            * @brief m10
            * @return double&
            */
            double& m10();
            /*
            * @brief m10
            * @return const double&
            */
            const double& m10() const;
            /*
            * @brief m11
            * @return double&
            */
            double& m11();
            /*
            * @brief m11
            * @return const double&
            */
            const double& m11() const;
            /*
            * @brief m12
            * @return double&
            */
            double& m12();
            /*
            * @brief m12
            * @return const double&
            */
            const double& m12() const;
            /*
            * @brief m13
            * @return double&
            */
            double& m13();
            /*
            * @brief m13
            * @return const double&
            */
            const double& m13() const;
            /*
            * @brief m20
            * @return double&
            */
            double& m20();
            /*
            * @brief m20
            * @return const double&
            */
            const double& m20() const;
            /*
            * @brief m21
            * @return double&
            */
            double& m21();
            /*
            * @brief m21
            * @return const double&
            */
            const double& m21() const;
            /*
            * @brief m22
            * @return double&
            */
            double& m22();
            /*
            * @brief m22
            * @return const double&
            */
            const double& m22() const;
            /*
            * @brief m23
            * @return double&
            */
            double& m23();
            /*
            * @brief m23
            * @return const double&
            */
            const double& m23() const;
            /*
            * @brief m30
            * @return double&
            */
            double& m30();
            /*
            * @brief m30
            * @return const double&
            */
            const double& m30() const;
            /*
            * @brief m31
            * @return double&
            */
            double& m31();
            /*
            * @brief m31
            * @return const double&
            */
            const double& m31() const;
            /*
            * @brief m32
            * @return double&
            */
            double& m32();
            /*
            * @brief m32
            * @return const double&
            */
            const double& m32() const;
            /*
            * @brief m33
            * @return double&
            */
            double& m33();
            /*
            * @brief m33
            * @return const double&
            */
            const double& m33() const;
            /**
            * @brief 获取矩阵引用
            * @return Double4x4&
            */
            Double4x4& Mat();
            /**
            * @brief 获取矩阵常量引用
            * @return const Double4x4&
            */
            const Double4x4& Mat()const;

        public:
            /*
            * @brief 判断两个向量是否相等
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsEqual(IN const Tranform3& Other_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 矩阵乘法
            * @param [in] matRHS_ 右侧参与乘法的矩阵。
            * @return Tranform3
            */
            Tranform3 operator*(IN const Tranform3& matRHS_) const;
            /**
            * @brief 计算矩阵的逆变换
            * @return Tranform3
            */
            Tranform3 Inverse() const;
            /**
            * @brief 判断是否为单位矩阵
            * @param [in] nTol_ 容差（默认 EPSILON）
            * @return bool
            */
            bool IsIdentity(IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 应用变换到点
            * @param [in] ptTarget_ 输入点
            * @return Point3
            */
            Point3 Applied(IN const Point3& ptTarget_) const;

            /**
            * @brief 应用变换到向量
            * @param [in] vTarget_ 输入向量
            * @return Vector3
            */
            Vector3 Applied(IN const Vector3& vTarget_) const;
            /**
            * @brief 应用变换到方向
            * @param [in] dirTarget_ 输入方向
            * @return Dir3
            */
            Dir3 Applied(IN const Dir3& dirTarget_) const;

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
            * @brief 解压三维变换矩阵
            * @return std::tuple<
            *      Double3 translation,         //!< 平移向量
            *      Double3 rotation,            //!< 旋转欧拉角
            *      Double6 scale,               //!< 缩放因子
            *      Double3 shear,               //!< 剪切矩阵，或者用 Vector3 表示各轴剪切
            *      bool                         //!< X轴坐标是否镜像
            * >
            * @details
            *   合并的顺序：Mirror → Scale → Shear → Rotation → Translation
            */
            std::tuple<Double3, class Quaternion, Double6, Double3, bool> Decompose() const;

        public://!< 快速构建
            /**
            * @brief 构建单位矩阵
            * @return Tranform3
            */
            static Tranform3 Identity();
            /**
            * @brief 构建平移矩阵
            * @param [in] vTarget_ 平移向量
            * @return Tranform3
            */
            static Tranform3 Translate(IN const Double3& v_);
            /**
            * @brief 构建绕任意轴的旋转矩阵
            * @param [in] dir_ 旋转轴方向
            * @param [in] nRad_ 旋转角度（弧度）
            * @return Tranform3
            */
            static Tranform3 Rotate(IN const Dir3& dir_, IN const double& nRad_);
            /**
            * @brief 构建缩放矩阵
            * @param [in] nXYZ_ 各轴缩放系数 (x, y, z)
            * @return Tranform3
            */
            static Tranform3 Scale(IN const Double3& nXYZ_);
            /**
            * @brief 构建绕指定点旋转的矩阵
            * @param [in] ptCenter_ 旋转中心
            * @param [in] dir_ 旋转轴方向
            * @param [in] nRad_ 旋转角度（弧度）
            * @return Tranform3
            */
            static Tranform3 Rotate(IN const Point3& ptCenter_, IN const Dir3& dir_, IN const double& nRad_);
            /**
            * @brief 构建相对点缩放的矩阵
            * @param [in] ptCenter_ 缩放中心
            * @param [in] nXYZ_ 各轴缩放系数
            * @return Tranform3
            */
            static Tranform3 Scale(IN const Point3& ptCenter_, const Double3& nXYZ_);
            /**
            * @brief 构建相对轴镜像的矩阵
            * @param [in] ax_ 镜像轴
            * @return Tranform3
            */
            static Tranform3 Mirror(IN const Point3& ptLocation_, IN const Dir3& dir_);
            /**
            * @brief 构建相对点镜像的矩阵
            * @param [in] ptCenter_ 镜像点
            * @return Tranform3
            */
            static Tranform3 Mirror(IN const Point3& ptCenter_);
            /*
            * @brief 沿 X 轴镜像 
            * @return Tranform3 
            */
            static Tranform3 MirrorX();
            /*
            * @brief 沿 Y 轴镜像
            * @return Tranform3
            */
            static Tranform3 MirrorY();
            /*
            * @brief 沿 Z 轴镜像
            * @return Tranform3
            */
            static Tranform3 MirrorZ();

            /**
            * @brief 构建三维剪切矩阵
            * @details
            * 生成一个 4x4 仿射矩阵（Trsf3），
            * 按照上三角和下三角组合：
            *
            * SH_u = 上三角剪切
            * SH_l = 下三角剪切
            * M_shear = SH_u * SH_l
            *
            * @param [in] nShear_ 六个剪切分量
            *        {shXY, shXZ, shYZ, shYX, shZX, shZY}
            * @return Tranform3
            */
            static Tranform3 Shear(IN const Double6& nShear_);

            /**
            * @brief 构建沿 X 轴的切变矩阵
            * @param [in] nY_ X 相对 Y 的切变系数
            * @param [in] nZ_ X 相对 Z 的切变系数
            * @return Tranform3
            */
            static Tranform3 ShearX(IN const double& nY_, IN const double& nZ_);
            /**
            * @brief 构建沿 Y 轴的切变矩阵
            * @param [in] nX_ Y 相对 X 的切变系数
            * @param [in] nZ_ Y 相对 Z 的切变系数
            * @return Tranform3
            */
            static Tranform3 ShearY(IN const double& nX_, IN const double& nZ_);
            /**
            * @brief 构建沿 Z 轴的切变矩阵
            * @param [in] nX_ Z 相对 X 的切变系数
            * @param [in] nY_ Z 相对 Y 的切变系数
            * @return Tranform3
            */
            static Tranform3 ShearZ(IN const double& nX_, IN const double& nY_);

        private:
            Double4x4 m_Matrix;
        };
    }
}