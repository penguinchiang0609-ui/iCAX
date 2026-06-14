#pragma once
#include "GeometryPrimative.h"
#include "MltBndCurve3.h"
#include <vector>
#include <memory>
#include "SglBndCurve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @class PolylineEx3
        * @brief 由任意边组成的二维闭合曲线
        * @details
        * PolylineEx3 表示二维闭合曲线，由任意单段开口曲线（OpnBndCurve2 派生）按顺序首尾相连组成。
        * 继承自 ClsBndCurve2，可表示多边形、带圆角的多边形或其他自由闭合曲线。
        * 曲线方向由段顺序决定，逆序段即可生成反向多边形。
        *
        * 内部维护：
        * - 段列表（m_segments）：记录每条边，支持多种曲线类型
        *
        * 注意：
        * - Evaluate/Tangent 接口会根据参数映射到具体段，统一参数区间为 [0,1]
        * - IsCCW 判断可采用折线近似计算
        * - Clone 会深拷贝所有段
        */
        class _GEOMETRY_EXP PolylineEx3 : public MltBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @note 段必须首尾相连形成闭合曲线
            */
            PolylineEx3();

            /**
            * @brief 构造函数
            * @param [in] lstSegments_ 段列表，每条段必须为 OpnBndCurve2 派生对象
            * @note 段必须首尾相连形成闭合曲线
            */
            PolylineEx3(IN const std::vector<std::shared_ptr<SglBndCurve3>>& lstSegments_);

            /**
            * @brief 析构函数
            */
            virtual ~PolylineEx3();

        public: //!< 段访问
            /*
            * @brief 追加段
            * @param [in] pSeg_
            */
            void PushBack(IN const std::shared_ptr<SglBndCurve3>& pSeg_);

        public://!< Curve2 接口
            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

            /**
            * @brief 获取圆弧所在平面
            * @return 若共面，返回对应的 Plane3；否则返回 std::nullopt
            */
            virtual std::optional<Plane3> SuggestPlane() const override;

        public: //!< Geometry2 接口实现
            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_
            */
            virtual void ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆
            * @return 新的对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;
        };
    }
}
