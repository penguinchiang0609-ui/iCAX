#include "pch.h"
#include "CameraComponent.h"
//
////!< 静态变量定义
//std::string iCAX::Component::CameraComponent::m_sLayers[32] = {
//    "Default", "Layer1", "Layer2", "Layer3", "Layer4",
//    "Layer5", "Layer6", "Layer7", "Layer8", "Layer9",
//    "Layer10", "Layer11", "Layer12", "Layer13", "Layer14",
//    "Layer15", "Layer16", "Layer17", "Layer18", "Layer19",
//    "Layer20", "Layer21", "Layer22", "Layer23", "Layer24",
//    "Layer25", "Layer26", "Layer27", "Layer28", "Layer29",
//    "Layer30", "Layer31"
//};
//
////!< 获取图层名称
//std::string iCAX::Component::CameraComponent::GetLayerName(IN const unsigned int& nLayerNo_)
//{
//    if (nLayerNo_ >= 32)
//    {
//        throw std::runtime_error("iCAX::Component::CameraComponent::IsVisiable, nLayerNo_ bigger than 32");
//    }
//    return m_sLayers[nLayerNo_];
//}
//
////!< 设置图层名称
//void iCAX::Component::CameraComponent::SetLayerName(IN const unsigned int& nLayerNo_, IN const std::string& strLayerName_)
//{
//    if (nLayerNo_ >= 32)
//    {
//        throw std::runtime_error("iCAX::Component::CameraComponent::IsVisiable, nLayerNo_ bigger than 32");
//    }
//    m_sLayers[nLayerNo_] = strLayerName_;
//}
//
//
