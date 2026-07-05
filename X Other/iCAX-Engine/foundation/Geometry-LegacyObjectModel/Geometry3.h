#pragma once
#include "GeometryPrimative.h"
#include "Geometry.h"
#include "../Math/Tranform2.h"
#include "../Math/CoordSys2.h"
#include "../Math/CoordSys3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /*
        * @brief 二维几何元素
        */
        class _GEOMETRY_EXP Geometry3 : public Geometry
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] csLocal_ 自身坐标系
            */
            Geometry3(IN const CoordSys3& csLocal_);

            /*
            * @brief 析构函数
            */
            virtual ~Geometry3();

        public:
            /*
            * @brief 自身坐标系
            * @return CoordSys3&
            */
            CoordSys3& Local();

            /*
            * @brief 自身坐标系
            * @return const CoordSys3&
            */
            const CoordSys3& Local() const;

        public:
            /**
            * @brief 执行几何变换
            * @param [in] mat_ 二维仿射变换（旋转、平移、缩放）
            */
            void Transform(IN const Tranform3& mat_);

            /**
            * @brief 平移和旋转
            * @param [in] nTranslate_
            * @param [in] nRotate_
            */
            void TransAndRot(IN const Double3& nTranslate_, IN const Quaternion& nRotate_);

            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_ 是否沿X轴镜像
            */
            virtual void ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorXoZ_) = 0;

        protected:
            CoordSys3 m_csLocal;            //!< 自身坐标系
        };
    }
}