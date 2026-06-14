#pragma once
#include "NcData.h"
#include "NcAux.h"
#include "Trace.h"
#include "Postrue.h"
#include <string>
#include "../../01 Foundation/Data/Variant.h"

namespace iCAX
{
    namespace NcData
    {
        /*
         * @brief 运动类型
         */
        enum ncMotionType
        {
            kRapid = 0,                         // !快速移动，即空移
            kCut,                               //!< 进给/切割
        };

        /*
        * @brief 路径
        */
        struct _NCDATA_EXP ncPath final
        {
            ncMotionType nType;                 //!< 运动类型
            NcTrace Trace;                      //!< 轨迹
            ncPostrue Postrues;                 //!< 姿态链，此处如果只有一个姿态，则表明整条路径只有一个姿态
        };

        /*
        * @brief 模态指令
        * @remark
        *   1、平面选择：G17/G18/G19
        *   2、坐标模式：G90/G91
        *   3、刀具补偿：G40/G41/G42
        *   4、工件坐标系：G54~G59
        */
        struct _NCDATA_EXP ncModal final
        {
            std::string ModalCode;                     //!< 模态指令
            iCAX::Data::Variant Parameter;              //!< 参数
        };

        /*
        * @brief 元素类型
        */
        enum ncPrimaryType
        {
            kPrimaryPath = 0,          //!< 路径
            kPrimaryModal,             //!< 模态指令
            kPrimaryBlock,             //!< 块
        };

        /*
        * @brief 主元素
        * @remark 主元素可以携带前置辅助与后置辅助元素
        */
        struct _NCDATA_EXP ncPrimary final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] Modal_
            */
            ncPrimary(IN const ncModal& Modal_);

            /*
            * @brief 构造函数
            * @param [in] Path_
            */
            ncPrimary(IN const ncPath& Path_);

            /*
            * @brief 构造函数
            * @param [in] Primaries_
            */
            ncPrimary(IN const std::vector<ncPrimary>& Primaries_);

            /*
            * @brief 拷贝构造函数
            * @param [in] Primaries_
            */
            ncPrimary(IN const ncPrimary& Right_);

            /*
            * @brief 赋值构造函数
            * @param [in] Right_
            */
            ncPrimary& operator=(IN const ncPrimary& Right_);

            /*
            * @brief 析构函数
            */
            ~ncPrimary();

        public:
            /*
            * @brief 获取主元素类型
            * @return ncPrimaryType
            */
            ncPrimaryType GetType() const;

            /*
            * @brief 获取模态指令
            * @return ncModal&
            */
            ncModal& GetModal();

            /*
            * @brief 获取模态指令
            * @return const ncModal&
            */
            const ncModal& GetModal() const;

            /*
            * @brief 获取路径
            * @return ncPath&
            */
            ncPath& GetPath();

            /*
            * @brief 获取路径
            * @return const ncPath&
            */
            const ncPath& GetPath() const;

            /*
            * @brief 获取块
            * @return ncBlock* const
            */
            struct ncBlock* const GetBlock();

            /*
            * @brief 获取块
            * @return ncBlock*& 
            */
            const struct ncBlock* const GetBlock() const;

            /*
            * @brief 获取前置辅助元素列表
            * @return std::vector<ncAux>&
            */
            std::vector<ncAux>& GetPreAuxes();

            /*
            * @brief 获取前置辅助元素列表
            * @return const std::vector<ncAux>&
            */
            const std::vector<ncAux>& GetPreAuxes() const;

            /*
            * @brief 获取后置辅助元素列表
            * @return std::vector<ncAux>&
            */
            std::vector<ncAux>& GetPostAuxes();

            /*
            * @brief 获取后置辅助元素列表
            * @return const std::vector<ncAux>&
            */
            const std::vector<ncAux>& GetPostAuxes() const;
        private:
            union
            {
                ncModal m_Modal;
                ncPath m_Path;
                struct ncBlock* m_pBlock;
            };
            ncPrimaryType m_nType;                  //!< 元素类型
            std::vector<ncAux> m_PreAuxes;          //!< 前置辅助元素
            std::vector<ncAux> m_PostAuxes;         //!< 后置辅助元素
        };

        /*
        * @brief NC块
        */
        struct _NCDATA_EXP ncBlock final
        {
            std::vector<ncPrimary> Primaries;       //!< 主元素
        };

        typedef ncBlock ncDocument;//!< 暂时document未打算支持作者、应用程序版本等信息，又或者说，这些信息都作为注释。不需要单独列出来
    }
}