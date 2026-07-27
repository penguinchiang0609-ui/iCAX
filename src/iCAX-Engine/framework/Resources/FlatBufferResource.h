#pragma once

#include "ResourcesExport.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace iCAX::Resource
{
    /*
    * @brief 以 Google FlatBuffer 为内容的通用资源对象。
    * @details
    *   Buffer 在构造后不可变，资源副本共享同一份存储。具体 schema、file
    *   identifier 和 schema_version 由拥有该资源类型的业务模块定义。
    */
    class _RESOURCES_EXP CFlatBufferResource final
    {
    public:
        inline static constexpr const char* kResourceTypeName =
            "resource.flatbuffer";

        CFlatBufferResource();
        explicit CFlatBufferResource(std::vector<uint8_t> Buffer_);

        static CFlatBufferResource CopyFrom(
            std::span<const uint8_t> Buffer_);

        const uint8_t* Data() const noexcept;
        uint64_t Size() const noexcept;
        bool Empty() const noexcept;
        std::span<const uint8_t> Bytes() const noexcept;

    private:
        std::shared_ptr<const std::vector<uint8_t>> m_pBuffer;
    };
}
