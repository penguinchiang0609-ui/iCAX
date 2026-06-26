#pragma once
#include "Math.h"
#include "Point3.h"

namespace iCAX
{
    namespace Math
    {
        /**
        * @brief 三维轴对齐包围盒 (AABB)
        * @details
        * 表示在世界坐标系下沿 XYZ 轴方向对齐的包围盒。
        * 使用最小点 (min) 与最大点 (max) 表达空间范围：
        * - min：在各轴方向上取最小分量的点；
        * - max：在各轴方向上取最大分量的点。
        *
        * AABB 常用于快速空间检测、包围体计算、碰撞判定等。
        */
        class _MATH_EXP Box3 final
        {
        public:
            /*
            * @brief 构造函数
            */
            Box3();

            /**
            * @brief 使用给定的最小/最大点构造包围盒
            * @param [in] minPt 最小点
            * @param [in] maxPt 最大点
            * @note 不会检查 min/max 是否符合顺序，请保证 minPt <= maxPt。
            */
            Box3(const Point3& minPt, const Point3& maxPt);

        public:
            /*
            * @brief 最小点
            * @return
            */
            const Point3& MinPoint() const;
            /*
            * @brief 最大点
            * @return
            */
            const Point3& MaxPoint() const;

            /**
            * @brief 获取包围盒中心点
            * @return 中心点坐标
            */
            Point3 Center() const;

            /**
            * @brief 获取包围盒的尺寸向量
            * @return 宽度、高度、深度的向量（max - min）
            */
            Double3 Size() const;

            /**
            * @brief 判断当前包围盒是否为空
            * @return 若 min > max（任意轴上）则为空
            */
            bool IsEmpty() const;

            /**
            * @brief 扩展包围盒以包含给定点
            * @param [in] pt 待扩展的点
            * @note 若当前包围盒为空，将直接以该点初始化 min/max。
            */
            void Extend(const Point3& pt);

            /**
            * @brief 判断一个点是否位于包围盒内部
            * @param [in] pt 待测试点
            * @return 若点在包围盒内部（含边界）返回 true，否则返回 false。
            */
            bool Contains(const Point3& pt) const;

            /**
            * @brief 合并两个包围盒，生成包含二者的新包围盒
            * @param [in] a 第一个包围盒
            * @param [in] b 第二个包围盒
            * @return 新的包围盒，包含 a 和 b 的全部空间。
            */
            static Box3 Union(const Box3& a, const Box3& b);

        private:
            Point3 m_ptMin; //!< 最小坐标点
            Point3 m_ptMax; //!< 最大坐标点

        };
    }
}