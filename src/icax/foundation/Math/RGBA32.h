#pragma once
#include "Math.h"
#include "../Data/Array1.h"

namespace iCAX
{
    namespace Math
    {
        /**
        * @class RGBA32
        * @brief RGBA 颜色类
        * @details
        *   使用 4 个字节存储颜色分量：红、绿、蓝、Alpha。
        *   提供对单独分量以及整体 Byte4 的直接引用访问。
        *
        * @note 设计为可直接修改分量或整体颜色数据，去掉传统的 Get/Set 方法
        */
        class _MATH_EXP RGBA32
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化为白色不透明 (255, 255, 255, 255)
            */
            RGBA32();

            /**
            * @brief 构造函数
            * @param [in] nColor_
            */
            RGBA32(IN const iCAX::Data::Byte4& nColor_);

            /**
            * @brief 构造函数
            * @param [in] nRed_ 红色分量
            * @param [in] nGreen_ 绿色分量
            * @param [in] nBlue_ 蓝色分量
            * @param [in] nAlpha_ 透明度分量
            */
            RGBA32(IN const unsigned char& nRed_, IN const unsigned char& nGreen_, IN const unsigned char& nBlue_, IN const unsigned char& nAlpha_);

            /**
            * @brief 析构函数
            */
            ~RGBA32();

        public:
            /**
            * @brief 红色分量引用（可读写）
            * @return unsigned char&
            */
            unsigned char& R();

            /**
            * @brief 红色分量引用（只读）
            * @return const unsigned char&
            */
            const unsigned char& R() const;

            /**
            * @brief 绿色分量引用（可读写）
            * @return unsigned char&
            */
            unsigned char& G();

            /**
            * @brief 绿色分量引用（只读）
            * @return const unsigned char&
            */
            const unsigned char& G() const;

            /**
            * @brief 蓝色分量引用（可读写）
            * @return unsigned char&
            */
            unsigned char& B();

            /**
            * @brief 蓝色分量引用（只读）
            * @return const unsigned char&
            */
            const unsigned char& B() const;

            /**
            *  
            * @brief Alpha 分量引用（可读写）
            * @return unsigned char&
            */
            unsigned char& A();
             
            /**
            * @brief Alpha 分量引用（只读）
            * @return const unsigned char&
            */
            const unsigned char& A() const;

            /**
            * @brief 整体颜色数据引用（可读写）
            * @return iCAX::Data::Byte4&
            */
            iCAX::Data::Byte4& RGBA();

            /**
            * @brief 整体颜色数据引用（只读）
            * @return const iCAX::Data::Byte4&
            */
            const iCAX::Data::Byte4& RGBA() const;

        private:
            iCAX::Data::Byte4 m_Data; //!< 内部存储 RGBA 数据
        };
    }
}
