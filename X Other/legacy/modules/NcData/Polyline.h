#pragma once
#include "NcData.h"
#include "../Math/Point3.h"
#include "../Math/Vector3.h"
#include <vector>

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace NcData
    {
        /*
        * @brief 多义线
        */
        struct _NCDATA_EXP ncPolyline final
        {
            /*
            * @brief 节点类型
            */
            enum _NCDATA_EXP NodeType
            {
                kNcLineSeg,
                kNcArcSeg,
                kNcEllipseArcSeg,
                kNcBezierSeg
            };

            /*
            * @brief 直线节点
            */
            struct _NCDATA_EXP LineNode final
            {
                Point3 ptEnd;
            };

            /*
            * @brief 圆弧节点
            */
            struct _NCDATA_EXP ArcNode final
            {
                Point3 ptEnd;                  //!< 终点
                Point3 ptCenter;               //!< 圆心偏移
                Vector3 vNormal;               //!< 法向
                bool bCW = false;                       //!< 顺逆时针
            };

            /*
            * @brief 椭圆节点
            */
            struct _NCDATA_EXP EllipseArcNode final
            {
                Point3 ptEnd;                  //!< 终点
                Point3 ptLeftFocus;            //!< 左焦点
                Point3 ptRightFocus;           //!< 右焦点
                Vector3 vNormal;               //!< 法向
                bool bCW;                       //!< 顺逆时针
            };

            /*
            * @brief 贝塞尔节点
            */
            struct _NCDATA_EXP BezierNode final
            {
                Point3 ptEnd;                  //!< 终点
                Point3 ptCtrl0;                //!< 控制点0
                Point3 ptCtrl1;                //!< 控制点1
            };

            /*
            * @brief 节点
            */
            struct _NCDATA_EXP Node final
            {
            public:
                /*
                * @brief 构造函数
                */
                Node();

                /*
                * @brief 追加直线段
                * @param [in] ptEnd_ 结束点
                * @param [in] nFeedrate_ 速率
                */
                Node(IN const Point3& ptEnd_, IN const double& nFeedrate_);

                /*
                * @brief 追加圆弧段
                * @param [in] ptEnd_ 结束点
                * @param [in] ptCenter_ 圆心
                * @param [in] vNormal_ 法向
                * @param [in] bCW_ 顺逆时针
                * @param [in] nFeedrate_ 速率
                */
                Node(IN const Point3& ptEnd_, IN const Point3& ptCenter_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_);

                /*
                * @brief 追加椭圆弧段
                * @param [in] ptEnd_ 结束点
                * @param [in] ptLeftFocus_ 左焦点
                * @param [in] ptRightFocus_ 右焦点
                * @param [in] vNormal_ 法向
                * @param [in] bCW_ 顺逆时针
                * @param [in] nFeedrate_ 速率
                */
                Node(IN const Point3& ptEnd_, IN const Point3& ptLeftFocus_, IN const Point3& ptRightFocus_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_);

                /*
                * @brief 追加贝塞尔段
                * @param [in] ptEnd_ 结束点
                * @param [in] ptCtrl0_ 控制点1
                * @param [in] ptCtrl1_ 控制点2
                * @param [in] nFeedrate_ 速率
                */
                Node(IN const Point3& ptEnd_, IN const Point3& ptCtrl0_, IN const Point3& ptCtrl1_, IN const double& nFeedrate_);


                /*
                * @brief 拷贝构造函数
                * @param [in] Right_
                */
                Node(IN const Node& Right_);

                /*
                * @brief 赋值构造函数
                * @param [in] Right_
                */
                Node& operator=(IN const Node& Right_);

                /*
                * @brief 析构函数
                */
                ~Node();

            public:

                NodeType nSegType;
                union
                {
                    LineNode Line;
                    ArcNode Arc;
                    EllipseArcNode EArc;
                    BezierNode Bezier;
                };
                double nFeedrate;               //!< 速度
            };

        public:
            /*
            * @brief 构造函数
            */
            ncPolyline();

            /*
            * @brief 析构函数
            */
            ~ncPolyline();

        public:
            /*
            * @brief 设置起点
            * @param [in] pt_
            */
            void SetStart(IN const Point3& pt_);

            /*
            * @brief 获取起点
            * @return Point3
            */
            Point3 GetStart() const;

            /*
            * @brief 获取节点列表
            * @return std::vector<Node>&
            */
            std::vector<Node>& GetNodes();

            /*
            * @brief 获取节点列表
            * @return const std::vector<Node>&
            */
            const std::vector<Node>& GetNodes() const;

            /*
            * @brief 追加直线段
            * @param [in] ptEnd_ 结束点
            * @param [in] nFeedrate_ 速率
            */
            void PushLine(IN const Point3& ptEnd_, IN const double& nFeedrate_);

            /*
            * @brief 追加圆弧段
            * @param [in] ptEnd_ 结束点
            * @param [in] ptCenter_ 圆心
            * @param [in] vNormal_ 法向
            * @param [in] bCW_ 顺逆时针
            * @param [in] nFeedrate_ 速率
            */
            void PushArc(IN const Point3& ptEnd_, IN const Point3& ptCenter_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_);

            /*
            * @brief 追加椭圆弧段
            * @param [in] ptEnd_ 结束点
            * @param [in] ptLeftFocus_ 左焦点
            * @param [in] ptRightFocus_ 右焦点
            * @param [in] vNormal_ 法向
            * @param [in] bCW_ 顺逆时针
            * @param [in] nFeedrate_ 速率
            */
            void PushEllipseArc(IN const Point3& ptEnd_, IN const Point3& ptLeftFocus_, IN const Point3& ptRightFocus_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_);
            
            /*
            * @brief 追加贝塞尔段
            * @param [in] ptEnd_ 结束点
            * @param [in] ptCtrl0_ 控制点1
            * @param [in] ptCtrl1_ 控制点2
            * @param [in] nFeedrate_ 速率
            */
            void PushBezier(IN const Point3& ptEnd_, IN const Point3& ptCtrl0_, IN const Point3& ptCtrl1_, IN const double& nFeedrate_);

        private:
            Point3 m_ptStart;                    //!< 起点
            std::vector<Node> m_vecNodes;       //!< 节点列表
        };
    }
}