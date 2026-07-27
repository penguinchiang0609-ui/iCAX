#include "pch.h"
#include "FlatBufferResource.h"

iCAX::Resource::CFlatBufferResource::CFlatBufferResource()
    : m_pBuffer(std::make_shared<const std::vector<uint8_t>>())
{
}

iCAX::Resource::CFlatBufferResource::CFlatBufferResource(
    std::vector<uint8_t> Buffer_)
    : m_pBuffer(
        std::make_shared<const std::vector<uint8_t>>(
            std::move(Buffer_)))
{
}

iCAX::Resource::CFlatBufferResource
iCAX::Resource::CFlatBufferResource::CopyFrom(
    const std::span<const uint8_t> Buffer_)
{
    return CFlatBufferResource(
        std::vector<uint8_t>(Buffer_.begin(), Buffer_.end()));
}

const uint8_t*
iCAX::Resource::CFlatBufferResource::Data() const noexcept
{
    return m_pBuffer == nullptr
        ? nullptr
        : m_pBuffer->data();
}

uint64_t
iCAX::Resource::CFlatBufferResource::Size() const noexcept
{
    return m_pBuffer == nullptr
        ? 0
        : static_cast<uint64_t>(m_pBuffer->size());
}

bool
iCAX::Resource::CFlatBufferResource::Empty() const noexcept
{
    return m_pBuffer == nullptr || m_pBuffer->empty();
}

std::span<const uint8_t>
iCAX::Resource::CFlatBufferResource::Bytes() const noexcept
{
    return m_pBuffer == nullptr
        ? std::span<const uint8_t>{}
        : std::span<const uint8_t>{
            m_pBuffer->data(),
            m_pBuffer->size()
        };
}
