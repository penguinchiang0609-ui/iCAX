#pragma once

#include "PhysicsPrimative.h"
#include "Collider3.h"
#include "../Math/Vector3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维盒子碰撞器
        * @details
        *   表示一个三维轴对齐或局部坐标系下的立方体/长方体碰撞区域，
        *   使用半偏移（HalfExtents）表示盒子在 x、y、z 方向的半宽、高、深。
        *   继承自 cCollider3，支持局部坐标系、旋转和偏移。
        */
        class _PHYSICSPRIMATIVE_EXP cBox3 : public cCollider3
        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化半偏移为零向量。
            */
            cBox3();

            /**
            * @brief 析构函数
            */
            virtual ~cBox3();

        public:
            /**
            * @brief 设置半偏移
            * @param vHalfExtents_ 盒子在 x、y、z 方向的半宽、高、深
            */
            Vector3& HalfExtents();

            /**
            * @brief 获取半偏移（只读）
            * @return Vector3 盒子在 x、y、z 方向的半宽、高、深
            */
            const Vector3& HalfExtents() const;

        private:
            Vector3 m_vHalfExtents;  //!< 盒子在 x、y、z 方向的半宽、高、深
        };
    }
}
