#pragma once

#include "../Machine/MachineExport.h"

#include "Data/Variant.h"
#include "Database/IEntity.h"

namespace iCAX::CAM
{
    /*
    * @brief 生成机床元素 Transform 编辑策略。
    * @details
    *   Database checker 负责真正拦截非法修改；本函数把同一套业务约束投影成前端可读的策略。
    *   前端只根据该策略决定输入框是否可编辑、步长和上下限，不自行推断 SDF 运动副规则。
    */
    _MACHINE_EXP iCAX::Data::ObjectMap MakeMachineTransformEditPolicyPayload(IN const iCAX::Database::IEntity& Entity_);
}
