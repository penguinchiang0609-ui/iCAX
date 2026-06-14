#pragma once
#include "Geometry2.h"
#include "BndCurve2.h"
#include "../Math/Point2.h"
#include <vector>
#include <memory>

namespace iCAX
{
    namespace Geom
    {
        /*
        * @brief 轮廓类型
        */
        enum LoopType
        {
            kLoopUnknow = 0,        //!< 未知
            kLoopOutter,            //!< 外轮廓
            kLoopInner,             //!< 内孔
        };

        /**
        * @brief 二维闭合环类
        * @details
        * Loop2 表示二维区域的最小闭合环，由有界曲线（BndCurve2）组成。
        * 顺逆时针方向用于判断环的几何意义：
        * - 逆时针 (CCW) 表示外环
        * - 顺时针 (CW) 表示孔洞/内环
        *
        * 该类提供基本的环操作接口，包括增加曲线、闭合性判断、
        * 方向判断、面积计算以及方向反转等。
        */
        class _GEOMETRY_EXP Loop2 : public Geometry2
        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化空闭合环
            */
            Loop2();

        /**
            * @brief 析构函数
            */
            virtual ~Loop2();

        public:
            /**
            * @brief 获取段列表（只读）
            * @return 智能指针段列表
            */
            const std::vector<std::shared_ptr<BndCurve2>>& Segments() const;

            /**
            * @brief 动态获取顶点列表
            * @return 顶点列表，由各段的起点组成
            * @note 最后顶点会重复第一个段的起点以保证闭合
            */
            const std::vector<Point2> GetVertices() const;

            /*
            * @brief 解析u得到段以及段内参数
            * @param [in] nU_
            * @param [in] bLeft_ 当nU_位于节点时，向左查找还是向右查找
            * @param [out] pSeg_ 所在段
            * @param [out] nLocalU_ 段内U
            * @detail
            *   1、u位于全局起点且不闭合的场景，则返回首段+首段的起始参数，忽略bLeft_
            *   2、u位于全局终点且不闭合的场景，则返回尾段+尾段的终止参数，忽略bLeft_
            */
            void ResolveU2Segment(IN const double& nU_, IN const bool& bLeft_, OUT std::shared_ptr<BndCurve2>& pSeg_, OUT double& nLocalU_) const;

            /**
            * @brief 向环中增加一条有界曲线
            * @param [in] curve 共享指针指向 BndCurve2 对象
            * @note 新增曲线必须首尾连续，且 Loop 尚未闭合
            */
            void PushBack(IN const std::shared_ptr<BndCurve2>& curve);

            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间定义由具体曲线类型决定。
            */
            double StartParam() const;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常满足 uEnd > uStart。
            */
            double EndParam() const;

            /**
            * @brief 获取曲线起点坐标
            * @return 曲线起点的二维坐标
            */
            Point2 StartPoint() const;

            /**
            * @brief 获取曲线终点坐标
            * @return 曲线终点的二维坐标
            */
            Point2 EndPoint() const;

            /**
            * @brief 判断曲线是否闭合
            * @param [in] nTol_ 容差值（默认 EPSILON）
            * @return 若首尾点间距小于 tol_，则视为闭合
            * @details
            * 对于如 Circle2、ClosedBezier2 等类型，此函数始终返回 true。
            */
            bool IsClosed(IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 获取曲线朝向类型
            * @return LoopType
            */
            LoopType ContourType() const;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的二维点坐标
            */
            Point2 Evaluate(IN const double& nU_) const;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            void Reverse();

        private:
            /*
            * @brief 重算参数表
            */
            void ReCalcParamTable();

            /*
            * @brief 参数归一化
            * @param [in] nU_double
            * @return
            */
            double NormalizeParam(IN const double& nU_) const;

        protected:
            std::vector<std::shared_ptr<BndCurve2>> m_lstSegments; //!< 段列表（支持任意单段曲线）
            std::vector<std::pair<double, double>> m_lstParams; //!< 段累加参数表
        };
    }
}
