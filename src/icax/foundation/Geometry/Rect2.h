#pragma once
#include "GeometryPrimative.h"
#include "MltBndCurve2.h"
#include <vector>
#include "Polyline2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维矩形类
        * @details
        * Rect2 表示二维平面上的矩形闭合曲线。
        * 矩形按顺时针参数化，顶点顺序为：
        * 左下 -> 右下 -> 右上 -> 左上 -> 左下（闭合）
        * 可直接用于 Loop2 或 Region2 的闭合轮廓。
        * 继承自 ClsBndCurve2。
        * 逆时针无变换时：
        * [0,1)	下边直线	左下 → 右下
        * [1,2)	右边直线	下 → 上
        * [2,3)	上边直线	右上 → 左上
        * [3,4)	左边直线	上 → 下
        */
        class _GEOMETRY_EXP Rect2 final : public MltBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_ 中心坐标
            * @param [in] nWidth_ 矩形宽度（X方向）
            * @param [in] nHeight_ 矩形高度（Y方向）
            * @param [in] bCCW_ 是否逆时针
            * @param [in] dirRight_ Width所在方向
            * @details 构造一个闭合矩形对象，并生成内部缓存多边形对象
            */
            Rect2(IN const Point2& ptCenter_, IN const double& nWidth_, IN const double& nHeight_, IN const bool& bCCW_ = true, IN const Dir2& dirRight_ = Dir2::Right());

            /**
            * @brief 析构函数
            * @details 默认析构，无需特殊资源释放
            */
            virtual ~Rect2();

        public: //!< 成员访问
            /**
            * @brief 获取矩形宽度
            * @return 矩形宽度
            */
            const double& Width() const;

            /**
            * @brief 获取矩形高度
            * @return 矩形高度
            */
            const double& Height() const;

            /*
            * @brief 设置尺寸
            * @param [in] nWidth_ 宽度
            * @param [in] nHeight_ 高度
            */
            void SetSize(IN const double& nWidth_, IN const double& nHeight_);


        public://!< Curve2 接口
            /*
            * @brief 获取曲线朝向类型
            * @return CurveOrientType
            */
            virtual CurveOrientType Orientation() const override;

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
            /*
            * @brief 重新计算缓存数据
            */
            void ReCalcCache();

        private:
            double m_nWidth;                            //!< 矩形宽度
            double m_nHeight;                           //!< 矩形高度
            bool m_bCCW;        //!< 逆时针标记位
        };
    }
}
