#pragma once

#include "Data/Variant.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        struct SSDFMachineDocument final
        {
            std::string SDFVersion;
            std::string ModelName;
            bool bStaticModel = false;
            iCAX::Data::VariantArray Links;
            iCAX::Data::VariantArray Joints;
            iCAX::Data::VariantArray Visuals;
            iCAX::Data::VariantArray Collisions;
            iCAX::Data::VariantArray Materials;
            iCAX::Data::VariantArray Includes;
            iCAX::Data::VariantArray Frames;
            iCAX::Data::VariantArray Plugins;
        };

        bool ParseSDFMachineDocument(
            IN const std::string& strSourcePath_,
            OUT SSDFMachineDocument& Document_,
            OUT std::string& strError_);
    }
}
