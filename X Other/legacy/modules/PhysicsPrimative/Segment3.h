#pragma once
#include "PhysicsPrimative.h"
#include "../Math/Point3.h"
#include "../Math/Dir3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
         * @class cSegment3
         * @brief 三维线段类
         * @details
         *   表示空间中的有限直线段，具有起点和终点。
         *   可用于物理查询、碰撞检测和距离计算等场景。
         *
         * @note 区别于射线和无限直线：
         *   - Line：无限延伸
         *   - Ray：从起点沿方向无限延伸
         *   - Segment：有限起止点
         */
        class _PHYSICSPRIMATIVE_EXP cSegment3
        {
        public:
            /**
             * @brief 默认构造函数
             * @details 将起点和终点初始化为原点。
             */
            cSegment3();

            /**
             * @brief 构造函数
             * @param [in] start 起点
             * @param [in] end 终点
             */
            cSegment3(IN const Point3& start, IN const Point3& end);

        public:
            /**
             * @brief 获取起点
             * @return Point3& 起点引用，可修改
             */
            Point3& Start();

            /**
             * @brief 获取起点（只读）
             * @return const Point3& 起点常量引用
             */
            const Point3& Start() const;

            /**
             * @brief 获取终点
             * @return Point3& 终点引用，可修改
             */
            Point3& End();

            /**
             * @brief 获取终点（只读）
             * @return const Point3& 终点常量引用
             */
            const Point3& End() const;

            /**
             * @brief 获取方向向量
             * @return Dir3 从起点指向终点的单位向量
             */
            Dir3 Direction() const;

            /**
             * @brief 获取线段长度
             * @return double 起点到终点的距离
             */
            double Length() const;

        private:
            Point3 m_ptStart; //!< 起点
            Point3 m_ptEnd;   //!< 终点
        };
    }
}
