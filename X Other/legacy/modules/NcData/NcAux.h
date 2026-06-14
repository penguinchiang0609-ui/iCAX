#pragma once
#include "NcData.h"
#include <string>
#include "../../01 Foundation/Data/Variant.h"

namespace iCAX
{
    namespace NcData
    {
        /*
        * @brief 辅助命令
        */
        struct _NCDATA_EXP ncAuxCommand final
        {            
            std::string AuxCode;                   //!< 辅助指令，如M50、M01等
            iCAX::Data::Variant Parameter;         //!< 参数
        };

        /*
        * @brief 辅助注释
        */
        struct _NCDATA_EXP ncAuxComment final
        {
            iCAX::Data::Variant Value;
        };

        /*
        * @brief 辅助元素
        */
        struct _NCDATA_EXP ncAux final
        {
            /*
            * @brief 构造函数
            */
            ncAux();

            /*
            * @brief 构造函数
            * @param [in] Comment_
            */
            ncAux(IN const ncAuxComment& Comment_);

            /*
            * @brief 构造函数
            * @param [in] Command_
            */
            ncAux(IN const ncAuxCommand& Command_);

            /*
            * @brief 拷贝构造函数
            * @param [in] Right_
            */
            ncAux(IN const ncAux& Right_);

            /*
            * @brief 赋值构造函数
            * @param [in] Right_
            */
            ncAux& operator=(IN const ncAux& Right_);

            /*
            * @brief 析构函数
            */
            ~ncAux();

            /*
            * @brief 辅助类型
            */
            enum _NCDATA_EXP AuxType
            {
                kAuxComment = 0,                //!< 注释
                kAuxCommand                     //!< 辅助指令
            } nType;

            union
            {
                ncAuxComment Comment;        //!< 对象注释
                ncAuxCommand Command;           //!< 辅助指令
            };
        };
    }
}