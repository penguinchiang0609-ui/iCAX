#pragma once
#include "PhysicsPrimative.h"
#include "../Math/Point2.h"
#include "../Math/Dir2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @class cSegment2
        * @brief 二维线段类
        * @details
        *   表示二维平面中的有限线段，具有起点和终点。
        *   可用于物理查询、碰撞检测、距离计算或射线投射等场景。
        *
        * @note 与 cSegment3 类似，只是二维版本。
        */
        class _PHYSICSPRIMATIVE_EXP cSegment2
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 将起点和终点初始化为原点。
            */
            cSegment2();

            /**
            * @brief 构造函数
            * @param [in] start 起点
            * @param [in] end 终点
            */
            cSegment2(IN const Point2& start, IN const Point2& end);

        public:
            /**
            * @brief 获取起点
            * @return Point2& 起点引用，可修改
            */
            Point2& Start();

            /**
            * @brief 获取起点（只读）
            * @return const Point2& 起点常量引用
            */
            const Point2& Start() const;

            /**
            * @brief 获取终点
            * @return Point2& 终点引用，可修改
            */
            Point2& End();

            /**
            * @brief 获取终点（只读）
            * @return const Point2& 终点常量引用
            */
            const Point2& End() const;

            /**
            * @brief 获取方向向量
            * @return Dir2 从起点指向终点的单位向量
            */
            Dir2 Direction() const;

            /**
            * @brief 获取线段长度
            * @return double 起点到终点的距离
            */
            double Length() const;

        private:
            Point2 m_ptStart; //!< 起点
            Point2 m_ptEnd;   //!< 终点
        };
    }
}
