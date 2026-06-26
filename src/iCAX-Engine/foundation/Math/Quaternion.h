#pragma once
#include "Math.h"
#include "../Data/Array1.h"
#include "Point3.h"
#include "Tranform3.h"
#include "Dir3.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 四元数类（Quaternion）
        * @details
        *   用于表示三维空间的旋转。
        *   相较于欧拉角或旋转矩阵的优势：
        *       - 无万向锁问题（Gimbal Lock）
        *       - 支持球面插值 SLERP，实现平滑旋转
        *       - 存储紧凑、计算高效
        *
        *   四元数定义为 (x, y, z, w)：
        *       - 向量部分 (x, y, z) = 旋转轴 * sin(angle / 2)
        *       - 标量部分 w = cos(angle / 2)
        *
        *   默认采用右手坐标系。
        *   欧拉角转换采用 Z-Y-X 顺序（Yaw–Pitch–Roll）。
        */
        class _MATH_EXP Quaternion final
        {
        public:
            /*
            * @brief 默认构造函数
            * @details 初始化为单位四元数 (0,0,0,1)，表示无旋转
            */
            Quaternion();

            /*
            * @brief 从四元数向量构造
            * @param [in] nXYZW_ 四元数 (x, y, z, w)
            */
            Quaternion(IN const Double4& nXYZW_);

            /*
            * @brief 从欧拉角构造（Z-Y-X 顺序）
            * @param [in] nEuler_ 欧拉角 (roll, pitch, yaw)，单位：弧度
            * @details 构造后的四元数等价于 R = Rz(yaw) * Ry(pitch) * Rx(roll)
            */
            Quaternion(IN const Double3& nEuler_);

            /*
            * @brief 从分量构造
            * @param [in] nX_ X分量
            * @param [in] nY_ Y分量
            * @param [in] nZ_ Z分量
            * @param [in] nW_ W分量
            */
            Quaternion(IN const double& nX_, IN const double& nY_, IN const double& nZ_, IN const double& nW_);

            /*
            * @brief 从轴角构造
            * @param [in] dir_ 旋转轴方向
            * @param [in] angleRad 旋转角度（弧度）
            */
            Quaternion(IN const Dir3& dir_, IN const double& angleRad);

            /*
            * @brief 析构函数
            */
            ~Quaternion();

        public://!< 成员访问
            /*
            * @brief 获取或设置 X 分量
            * @return double& X分量引用
            */
            double& X();
            /*
            * @brief 获取 X 分量（只读）
            * @return const double& X分量常量引用
            */
            const double& X() const;
            /*
            * @brief 获取或设置 Y 分量
            * @return double& Y分量引用
            */
            double& Y();
            /*
            * @brief 获取 Y 分量（只读）
            * @return const double& Y分量常量引用
            */
            const double& Y() const;
            /*
            * @brief 获取或设置 Z 分量
            * @return double& Z分量引用
            */
            double& Z();
            /*
            * @brief 获取 Z 分量（只读）
            * @return const double& Z分量常量引用
            */
            const double& Z() const;
            /*
            * @brief 获取或设置 W 分量
            * @return double& W分量引用
            */
            double& W();
            /*
            * @brief 获取 W 分量（只读）
            * @return const double& W分量常量引用
            */
            const double& W() const;
            /*
            * @brief 获取整个四元数向量
            * @return Double4& 四元数向量引用
            */
            Double4& XYZW();
            /*
            * @brief 获取整个四元数向量（只读）
            * @return const Double4& 四元数向量常量引用
            */
            const Double4& XYZW() const;

            /*
            * @brief 判断两个向量是否相等
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsEqual(IN const Quaternion& qOther_, IN const double& nTol_ = EPSILON) const;

        public://!< 算术运算
            /*
            * @brief 四元数乘法（旋转复合）
            * @param [in] q_ 右乘四元数
            * @return Quaternion 复合后的四元数
            * @details 表示先旋转 q_，再旋转当前四元数
            * @note 四元数乘法不交换
            */
            Quaternion operator*(IN const Quaternion& q_) const;
            /*
            * @brief 四元数乘法复合赋值
            * @param [in] q 右乘四元数
            * @return Quaternion& 当前四元数引用
            */
            Quaternion& operator*=(IN const Quaternion& q);
            /*
            * @brief 取负（共轭方向）
            * @return Quaternion 与原四元数表示相同旋转
            */
            Quaternion operator-() const;

        public://!< 几何运算
            /*
            * @brief 共轭四元数
            * @return Quaternion 共轭四元数
            * @details 若 q = (x, y, z, w)，则共轭 q* = (-x, -y, -z, w)
            */
            Quaternion Conjugate() const;
            /*
            * @brief 逆四元数
            * @return Quaternion 四元数逆
            * @details q⁻¹ = q* / |q|²；单位四元数 q⁻¹ = q*
            */
            Quaternion Inverse() const;
            /*
            * @brief 四元数模（范数）
            * @return double 四元数模
            */
            double Magnitude() const;
            /*
            * @brief 模平方
            * @return double 四元数模平方
            */
            double MagnitudeSquared() const;
            /*
            * @brief 单位化四元数（返回新四元数）
            * @return Quaternion 单位化后的四元数
            * @details 若模为 0，则返回原四元数
            */
            Quaternion Normalized() const;
            /*
            * @brief 单位化自身
            * @return Quaternion& 当前四元数引用
            */
            Quaternion& Normalize();
            /*
            * @brief 使用四元数旋转向量
            * @param [in] vTarget_ 输入向量
            * @return Vector3 旋转后的向量
            * @details v' = q * (0, v) * q⁻¹
            */
            Vector3 Applied(IN const Vector3& vTarget_) const;
            /*
            * @brief 使用四元数旋转点
            * @param [in] ptTarget_ 输入点
            * @return Point3 旋转后的点（不含平移）
            */
            Point3 Applied(IN const Point3& ptTarget_) const;
            /*
            * @brief 使用四元数旋转方向
            * @param [in] dirTarget_ 输入方向
            * @return Dir3 旋转后的方向
            */
            Dir3 Applied(IN const Dir3& dirTarget_) const;
            /*
            * @brief 转换为仿射矩阵
            * @return Tranform3 仿射变换（旋转矩阵 + 单位缩放）
            */
            Tranform3 ToTrsf3() const;
            
            /*
            * @brief 从3x3旋转矩阵读取四元数信息
            */
            Quaternion& FromMatrix(IN const iCAX::Data::Double3x3 mat_);
            /*
            * @brief 获取欧拉角
            * @return Double3 欧拉角 (roll, pitch, yaw)
            * @details Yaw(Z) * Pitch(Y) * Roll(X)
            */
            Double3 Euler() const;

        public://!< 插值运算
            /*
            * @brief 球面线性插值（SLERP）
            * @param [in] qStart_ 起始四元数
            * @param [in] qEnd_ 结束四元数
            * @param [in] nFactor_ 插值因子 [0,1]
            * @return Quaternion 插值结果（单位化）
            */
            static Quaternion Slerp(IN const Quaternion& qStart_, IN const Quaternion& qEnd_, IN const double& nFactor_);
            /*
            * @brief 线性插值（LERP）
            * @param [in] qStart_ 起始四元数
            * @param [in] qEnd_ 结束四元数
            * @param [in] nFactor_ 插值因子 [0,1]
            * @return Quaternion 插值结果（需归一化）
            */
            static Quaternion Lerp(IN const Quaternion& qStart_, IN const Quaternion& qEnd_, IN const double& nFactor_);

        private:
            Double4 m_nXYZW; //!< 四元数向量 (x, y, z, w)
        };

        /*
        * @brief == 运算符
        * @param [in] qLHS_ 左操作数
        * @param [in] qRHS_ 右操作数
        * @return bool 是否相等
        */
        _MATH_EXP  bool operator==(IN const Quaternion& qLHS_, IN const Quaternion& qRHS_);

        /*
        * @brief < 运算符
        * @param [in] qLHS_ 左操作数
        * @param [in] qRHS_ 右操作数
        * @return bool 是否小于
        */
        _MATH_EXP  bool operator<(IN const Quaternion& qLHS_, IN const Quaternion& qRHS_);
    }
}

namespace std
{
    template<>
    struct hash<iCAX::Math::Quaternion>
    {
        std::size_t operator()(IN const iCAX::Math::Quaternion& qLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value) {
                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };
            std::hash<double> hasher;
            std::size_t seed = 0;
            seed = HashCombine(seed, hasher(qLHS_.X()));
            seed = HashCombine(seed, hasher(qLHS_.Y()));
            seed = HashCombine(seed, hasher(qLHS_.Z()));
            seed = HashCombine(seed, hasher(qLHS_.W()));
            return seed;
        }
    };
}
