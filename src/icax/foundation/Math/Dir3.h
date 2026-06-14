#pragma once
#include "Math.h"
#include "Vector3.h"
#include <cmath>

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 三维方向向量类（始终保持单位长度）
        * @details
        *   Dir3 用于表示方向而非位置。
        *   内部自动保持单位长度，适用于旋转、法线、方向计算等。
        *   与 Vector3 不同，gDir3 不包含尺度信息，仅代表方向。
        */
        class _MATH_EXP Dir3 final
        {
        public://!< 构造与析构
            /*
            * @brief 默认构造函数
            * @details 构造一个非法方向（0,0,0），需通过 IsNil 判断有效性。
            */
            Dir3();

            /*
            * @brief 由向量构造（自动归一化）
            * @param [in] vDir_ 输入向量（若长度为 0，将生成非法方向）
            */
            Dir3(IN const Vector3& vDir_);

            /*
            * @brief 由坐标构造（自动归一化）
            * @param [in] nXYZ_ 输入三维坐标
            */
            Dir3(IN const Double3& nXYZ_);

            /*
            * @brief 由分量构造（自动归一化）
            * @param [in] nX_ X 分量
            * @param [in] nY_ Y 分量
            * @param [in] nZ_ Z 分量
            */
            Dir3(IN const double& nX_, IN const double& nY_, IN const double& nZ_);

            /*
            * @brief 析构函数
            */
            ~Dir3();

        public://!< 成员访问
            /*
            * @brief 获取 X 分量
            * @return const double& X 分量
            */
            const double& X() const;

            /*
            * @brief 获取 Y 分量
            * @return const double& Y 分量
            */
            const double& Y() const;

            /*
            * @brief 获取 Z 分量
            * @return const double& Z 分量
            */
            const double& Z() const;

            /*
            * @brief 获取 (X, Y, Z) 坐标三元组
            * @return const Double3& 三维坐标
            */
            const Double3& XYZ() const;

            /*
            * @brief 获取内部向量（保证单位长度）
            * @return const Vector3& 向量形式的方向
            */
            const Vector3& Vector() const;

        public://!< 几何运算
            /*
            * @brief 计算与另一方向的夹角
            * @param [in] dirRight_ 另一方向
            * @return double 两方向夹角（弧度，范围 [0, π]）
            */
            double Angle(IN const Dir3& dirRight_) const;

            /*
            * @brief 判断是否相等
            * @param [in] dirRight_ 另一方向
            * @param [in] nTol_ 比较公差（默认 EPSILON）
            * @return bool 若两方向相同则返回 true
            */
            bool IsEqual(IN const Dir3& dirRight_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 判断是否为零向量（非法方向）
            * @param [in] nTol_ 公差（默认 EPSILON）
            * @return bool 若向量长度小于公差则返回 true
            */
            bool IsNil(IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 判断是否平行
            * @param [in] dirOther_ 另一方向
            * @param [in] nTol_ 公差（默认 EPSILON）
            * @return bool 若两方向平行则返回 true
            */
            bool IsParallel(IN const Dir3& dirOther_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 判断是否同向（夹角接近 0）
            * @param [in] dirOther_ 另一方向
            * @param [in] nTol_ 公差（默认 EPSILON）
            * @return bool 若方向一致则返回 true
            */
            bool IsCodirectional(IN const Dir3& dirOther_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 判断是否反向（夹角接近 π）
            * @param [in] dirOther_ 另一方向
            * @param [in] nTol_ 公差（默认 EPSILON）
            * @return bool 若方向相反则返回 true
            */
            bool IsOpposite(IN const Dir3& dirOther_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 判断是否垂直
            * @param [in] dirOther_ 另一方向
            * @param [in] nTol_ 公差（默认 EPSILON）
            * @return bool 若点积接近 0 则返回 true
            */
            bool IsPerpendicular(IN const Dir3& dirOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 生成取反方向
            * @details 返回一个方向取反（即方向向量取负）的新直方向。
            * @return Dir3 新方向
            */
            Dir3 Reversed() const;

            /**
            * @brief 当前方向取反
            * @details 将当前方向向量取负，改变参数化方向。
            * @return Dir3& 当前方向对象引用
            */
            Dir3& Reverse();

        public://!< 预置常量
            /*
            * @brief 获取非法方向（0,0,0）
            * @return const Dir3& 静态实例
            */
            static const Dir3& Nil();

            /*
            * @brief 获取左方向 (-1, 0, 0)
            * @return const Dir3& 静态实例
            */
            static const Dir3& Left();

            /*
            * @brief 获取右方向 (1, 0, 0)
            * @return const Dir3& 静态实例
            */
            static const Dir3& Right();

            /*
            * @brief 获取上方向 (0, 0, 1)
            * @return const Dir3& 静态实例
            */
            static const Dir3& Up();

            /*
            * @brief 获取下方向 (0, 0, -1)
            * @return const Dir3& 静态实例
            */
            static const Dir3& Down();

            /*
            * @brief 获取前方向 (0, 1, 0)
            * @return const Dir3& 静态实例
            */
            static const Dir3& Forward();

            /*
            * @brief 获取后方向 (0, -1, 0)
            * @return const Dir3& 静态实例
            */
            static const Dir3& Back();

            /*
            * @brief 获取 (1, 1, 1) 方向（自动归一化）
            * @return const Dir3& 静态实例
            */
            static const Dir3& One();

        private:
            Vector3 m_vector;  //!< 内部存储的单位向量
        };

        //------------比较运算符-------------

        /*
        * @brief 判断两方向是否相等
        * @param [in] dirLHS_ 左操作数
        * @param [in] dirRHS_ 右操作数
        * @return bool 若方向相同则返回 true
        */
        bool operator==(IN const Dir3& dirLHS_, IN const Dir3& dirRHS_);

        /*
        * @brief 判断左方向是否小于右方向（用于排序或映射）
        * @param [in] dirLHS_ 左操作数
        * @param [in] dirRHS_ 右操作数
        * @return bool 若左方向字典序小于右方向则返回 true
        */
        bool operator<(IN const Dir3& dirLHS_, IN const Dir3& dirRHS_);
    }
}

namespace std
{
    /*
    * @brief std::hash 模板特化（支持 gDir3 用于哈希容器）
    */
    template<>
    struct hash<iCAX::Math::Dir3>
    {
        /*
        * @brief 计算方向向量的哈希值
        * @param [in] dirLHS_ 输入方向
        * @return std::size_t 哈希值
        */
        std::size_t operator()(IN const iCAX::Math::Dir3& dirLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value) {
                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };

            std::hash<iCAX::Math::Vector3> hasher;
            std::size_t seed = 0;
            seed = HashCombine(seed, hasher(dirLHS_.Vector()));
            return seed;
        }
    };
}
