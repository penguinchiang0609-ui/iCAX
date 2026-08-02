#pragma once

#include "ProjectDocument.h"

#include <filesystem>
#include <span>
#include <vector>

namespace iCAX::ProjectFile
{
    struct _PROJECT_FILE_EXP CProjectFileReadResult final
    {
        uint32_t nContainerVersion = 0;
        EProjectFileEncoding Encoding = EProjectFileEncoding::Binary;
        CProjectDocument Document;
    };

    class _PROJECT_FILE_EXP CProjectFileCodec final
    {
    public:
        static std::vector<uint8_t> Encode(
            IN CProjectDocument Document_,
            IN EProjectFileEncoding Encoding_);
        static CProjectFileReadResult Decode(
            IN std::span<const uint8_t> Bytes_);

        static CProjectFileReadResult Read(
            IN const std::filesystem::path& Path_);
        static void WriteAtomic(
            IN const std::filesystem::path& Path_,
            IN CProjectDocument Document_,
            IN EProjectFileEncoding Encoding_);
    };
}

