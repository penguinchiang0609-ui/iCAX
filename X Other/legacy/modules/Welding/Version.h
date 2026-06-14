#pragma once

/*
* @brief 版本
*/
struct Version
{
    int nMajor;             //!< 主版本号，有重大变更（可能不兼容旧版本）
    int nMinor;             //!< 次版本号，添加了向后兼容的新功能
    int nPatch;             //!< 修复BUG版本号，修复 Bug，向后兼容
};