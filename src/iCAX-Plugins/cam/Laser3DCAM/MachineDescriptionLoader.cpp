#include "pch.h"
#include "MachineDescriptionLoader.h"
#include "SDFLoader.h"
#include "URDFLoader.h"

bool iCAX::CAM::IsSupportedMachineDescriptionPath(
    IN const std::string& strSourcePath_)
{
    return CanLoadSDFMachineDescription(strSourcePath_)
        || CanLoadURDFMachineDescription(strSourcePath_);
}

bool iCAX::CAM::LoadMachineDescription(
    IN const std::string& strSourcePath_,
    OUT CMachineDescriptionResource& Description_,
    OUT std::string& strError_)
{
    if (CanLoadSDFMachineDescription(strSourcePath_))
    {
        return LoadSDFMachineDescription(strSourcePath_, Description_, strError_);
    }
    if (CanLoadURDFMachineDescription(strSourcePath_))
    {
        return LoadURDFMachineDescription(strSourcePath_, Description_, strError_);
    }

    strError_ = "Unsupported machine description file: " + strSourcePath_;
    return false;
}
