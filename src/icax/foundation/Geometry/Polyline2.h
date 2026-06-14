#pragma once
#include "GeometryPrimative.h"
#include "MltBndCurve2.h"
#include <vector>
#include "Segment2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 折线组成的多义线
        * @details
        */
        class _GEOMETRY_EXP Polyline2 : public MltBndCurve2
        {
        public:
            /**
            * @brief 二维闭合多边形曲线
            * @details
            * Polyline2 表示由直线段按顺序首尾相连组成的二维闭合曲线。
            * 继承自 ClosedBoundedCurve2，但不使用基类的反向标记（m_reversed）。
            * 曲线方向由顶点顺序决定，逆序顶点即可生成反向多边形。
            */
            Polyline2();

            /**
            * @brief 二维闭合多边形曲线
            * @param [in] csLocal_ 自身坐标系
            * @param [in] Vertices_ 点列表
            * @details
            * Polyline2 表示由直线段按顺序首尾相连组成的二维闭合曲线。
            * 继承自 ClosedBoundedCurve2，但不使用基类的反向标记（m_reversed）。
            * 曲线方向由顶点顺序决定，逆序顶点即可生成反向多边形。
            */
            Polyline2(IN const std::vector<Point2>& Vertices_);

            /**
            * @brief 析构函数
            */
            virtual ~Polyline2();

        public: //!< 成员访问
            /**
            * @brief 获取曲线起点坐标
            * @return 曲线起点的二维坐标
            */
            virtual Point2 StartPoint() const override;

            /*
            * @brief 追加点
            * @param [in] ptEnd_
            */
            void PushBack(IN const Point2& ptEnd_);

            /*
            * @brief 清空
            */
            void Clear();

            /*
            * @brief 是否为空
            * @return bool
            */
            bool IsEmpty() const;

            /*
            * @brief 获取尺寸
            * @return int
            */
            int VertexCount() const;

        public://!< Curve2 接口
            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

        public: //!< Geometry2 接口实现
            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_
            */
            virtual void ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆
            * @return 新的对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            Point2 m_ptStart;                         //!< 起点（本地坐标）
        };
    }
}
