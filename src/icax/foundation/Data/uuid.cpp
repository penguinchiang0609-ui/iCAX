#include "pch.h"
#include "uuid.h"

//!< 生成新UUID
iCAX::Data::uuid iCAX::Data::GenerateNewUUID()
{
    // 随机数引擎
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static uuid_random_generator _uuidGen(gen);
    return _uuidGen();
}

//!< 生成空uuid
iCAX::Data::uuid iCAX::Data::GenerateNilUUID()
{
    return uuid();  // 全 0 的 UUID
}
