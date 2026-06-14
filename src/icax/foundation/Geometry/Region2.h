#pragma once
#include "Geometry2.h"
#include "Loop2.h"
#include <vector>
#include <memory>

namespace iCAX
{
    namespace Geom
    {
        /**
        * @class Region2
        * @brief 二维封闭区域（Region2）
        * @ingroup Geometry
        * @details
        * Region2 表示一个封闭的二维区域，通常由一个外轮廓（Outer Loop）和若干内轮廓（Inner Loops，孔）组成。
        * 每个轮廓由若干条有界曲线（BndCurve2）首尾相接形成，整体必须闭合。
        *
        * 区域的几何属性与方向约定：
        * - 外轮廓（Outer Loop）通常为**逆时针（CCW）**方向，表示区域的边界。
        * - 内轮廓（Inner Loop）为**顺时针（CW）**方向，表示孔洞区域。
        *
        * Region2 是一种高层几何结构，用于表达平面封闭域，可用于布尔运算、面域生成、镭雕路径生成等。
        *
        * 示例：
        * ```
        * +-------------------+
        * |   Outer MainLoop(CCW) |
        * |   +-------------+ |
        * |   | Inner(CW)   | |
        * |   +-------------+ |
        * +-------------------+
        * ```
        */
        class _GEOMETRY_EXP Region2 : public Geometry2
        {
        public:
            /**
            * @brief 使用外轮廓构造区域
            */
            explicit Region2(IN const CoordSys2& csLocal_ = CoordSys2());

            /**
            * @brief 析构函数
            */
            virtual ~Region2();

        //public: //!< 基础信息接口
        //    /**
        //    * @brief 检查区域几何有效性
        //    * @details
        //    * 判断外轮廓是否存在且闭合；
        //    * 所有内轮廓必须闭合；
        //    * 各轮廓间不应自交或相交。
        //    * @return 若区域有效返回 true，否则返回 false
        //    */
        //    virtual bool IsValid() const override;

        public: //!< 轮廓访问接口

            /**
            * @brief 获取外轮廓
            * @return 外轮廓的智能指针
            */
            const std::shared_ptr<Loop2>& Outter() const;

            /**
            * @brief 设置外轮廓
            * @param [in] Outer_ 新外轮廓
            * @note 若外轮廓无效（非闭合），内部不会被采纳。
            */
            void SetOutter(IN const std::shared_ptr<Loop2>& Outer_);

            /**
            * @brief 获取内轮廓集合
            * @return 内轮廓数组的只读引用
            */
            const std::vector<std::shared_ptr<Loop2>>& InnerLoops() const;

            /**
            * @brief 添加内轮廓
            * @param [in] inner 待添加的内轮廓
            * @note 内轮廓应为顺时针（CW）方向，并且闭合。
            */
            void AddInnerLoop(IN const std::shared_ptr<Loop2>& Inner_);

            /**
            * @brief 移除指定内轮廓
            * @param [in] inner 待移除的内轮廓指针
            */
            void RemoveInnerLoop(IN const std::shared_ptr<Loop2>& inner);

            /**
            * @brief 判断区域是否包含孔洞
            * @return 若存在内轮廓返回 true，否则返回 false
            */
            bool HasHoles() const;

        private:
            std::shared_ptr<Loop2> m_OutterLoop;
            std::vector<std::shared_ptr<Loop2>> m_InnerLoops;
        };
    }
}
