#pragma once
#include <cstdint>
#include <Data/uuid.h>

namespace iCAXOpenOp
{
    namespace Document
    {
        struct Header
        {
        public:
            //! 文件唯一ID
            //! 用途：
            //! 1 云端同步 识别文件。
            //! 2 资源缓存
            //! 3 防止装配循环引用
            iCAX::Data::uuid FileID;

            //! 文件类型识别
            //! 例如：ICAXCF
            //! 程序打开文件时第一步：read magic，如果不是ICAXCF直接报错。
            //! 为什么必须有？因为用户可能改后缀
            char strMagic[8];

            //! 该字段应该用文件后缀名
            ////! 为什么需要文件格式版本
            ////! 早期可能使用text
            ////! 后期可能改成binary
            ////! 甚至演化成xml等格式
            ////! 该字段用于判断当前采用的文件格式
            //uint32_t nFileVersion;                  //! 文件格式版本

            //! 文件布局的版本，即后面这些字段的顺序，或者新增了一些字段，需要更新此处
            uint32_t nFileFormatVersion;

            char strEngineName[64];                 //! 引擎名称
            //! 为什么必须有 ProductName
            //! 用于区分不同的产品线，供人肉眼识别。
            char strProductName[64];                //! 产品名称

            //! 文件创建时间
            uint64_t nCreateTime;
            //! 文件最后修改时间
            uint64_t nLastModifiedTime;

            //! 数据规约偏移
            //! 用于可以快速定位读取对应信息
            uint64_t nSchemaOffset;
            //! 仓储表偏移
            //! 用于可以快速定位读取对应信息
            uint64_t nRepositoryOffset;
            //! 资源位置偏移
            //! 用于可以快速定位读取对应信息
            uint64_t nResourceOffset;
        };
    }
}