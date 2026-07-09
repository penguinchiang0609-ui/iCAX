#include "pch.h"
#include "RenderSceneIds.h"

#include "Data/uuid.h"

namespace
{
    uint64_t MakeStableRenderID(IN const std::string& strText_) noexcept
    {
        uint64_t _Hash = 14695981039346656037ull;
        for (const auto _Char : strText_)
        {
            _Hash ^= static_cast<unsigned char>(_Char);
            _Hash *= 1099511628211ull;
        }
        return _Hash == 0 ? 1 : _Hash;
    }
}

iCAX::Render::RenderSceneID iCAX::Render::MakeRenderSceneID(IN const iCAX::Data::uuid& SceneID_) noexcept
{
    return static_cast<RenderSceneID>(MakeStableRenderID("scene:" + iCAX::Data::to_string(SceneID_)));
}

