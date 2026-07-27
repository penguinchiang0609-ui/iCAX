#pragma once

#include "FlatBufferResource.h"

#ifdef min
#pragma push_macro("min")
#undef min
#define ICAX_RESOURCE_RESTORE_MIN_MACRO
#endif
#ifdef max
#pragma push_macro("max")
#undef max
#define ICAX_RESOURCE_RESTORE_MAX_MACRO
#endif

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#ifdef ICAX_RESOURCE_RESTORE_MAX_MACRO
#pragma pop_macro("max")
#undef ICAX_RESOURCE_RESTORE_MAX_MACRO
#endif
#ifdef ICAX_RESOURCE_RESTORE_MIN_MACRO
#pragma pop_macro("min")
#undef ICAX_RESOURCE_RESTORE_MIN_MACRO
#endif

namespace iCAX::Resource
{
    inline CFlatBufferResource MakeFlatBufferResource(
        const flatbuffers::FlatBufferBuilder& Builder_)
    {
        const auto* _Begin = Builder_.GetBufferPointer();
        return CFlatBufferResource(
            std::vector<uint8_t>(
                _Begin,
                _Begin + Builder_.GetSize()));
    }

    template<typename TRoot>
    bool VerifyFlatBufferResource(
        const CFlatBufferResource& Resource_,
        const char* pFileIdentifier_)
    {
        if (Resource_.Empty() || pFileIdentifier_ == nullptr)
        {
            return false;
        }

        flatbuffers::Verifier _Verifier(
            Resource_.Data(),
            static_cast<size_t>(Resource_.Size()));
        return _Verifier.VerifyBuffer<TRoot>(pFileIdentifier_);
    }

    /*
    * @brief 校验并零拷贝读取资源 FlatBuffer root。
    * @return 校验失败时返回 nullptr；返回的 View 不得超过 Resource 生命周期。
    */
    template<typename TRoot>
    const TRoot* TryGetFlatBufferResourceRoot(
        const CFlatBufferResource& Resource_,
        const char* pFileIdentifier_)
    {
        return VerifyFlatBufferResource<TRoot>(
            Resource_,
            pFileIdentifier_)
            ? flatbuffers::GetRoot<TRoot>(Resource_.Data())
            : nullptr;
    }
}
