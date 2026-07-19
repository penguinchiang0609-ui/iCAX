#pragma once

#include "../Machine/MachineExport.h"

#include "Data/Variant.h"
#include "Database/IEntity.h"

#include <string>

namespace iCAX::CAM
{
    /*
    * @brief 生成机床元素 Transform 编辑策略。
    * @details
    *   Database checker 负责真正拦截非法修改；本函数把同一套业务约束投影成前端可读的策略。
    *   前端只根据该策略决定输入框是否可编辑、步长和上下限，不自行推断 SDF 运动副规则。
    */
    _MACHINE_EXP iCAX::Data::ObjectMap MakeMachineTransformEditPolicyPayload(IN const iCAX::Database::IEntity& Entity_);

    /*
    * @brief 将关节位置变化应用到承载该关节的 Transform。
    * @details
    *   Joint.Position 是运动状态，Transform 仍保存场景实际局部位姿。这里按位置差值叠加关节运动，
    *   因而不会丢失机床定义提供的关节原点，并会通过 Transform 的 Observable 字段触发渲染 PDO。
    */
    _MACHINE_EXP bool ApplyMachineJointPositionToTransform(
        IN iCAX::Database::IEntity& Entity_,
        IN double dPreviousPosition_,
        IN double dCurrentPosition_,
        OUT std::string& strError_);
}
