#pragma once

/**
 * @file Physics.h
 * @brief 物理原语数据模块导入/导出与宏定义
 * @details
 *   该模块定义了物理系统中最基础的原语数据结构，用于碰撞检测和物理模拟。
 *   核心价值在于：
 *   1. **统一表达物理实体形状**：
 *      - 提供二维和三维碰撞体的原语（Box、Sphere、Capsule、Plane 等）
 *      - 支持局部坐标系和缩放管理
 *      - 支持触发器（Trigger）功能
 *
 *   2. **支持物理查询和检测原语**：
 *      - 为射线检测（Ray/Raycast）或其他碰撞查询提供基础数据
 *      - 与碰撞器原语兼容，方便计算接触点、穿透深度等
 *
 *   3. **与数学模块无缝结合**：
 *      - 依赖 Math 模块提供 Vector、CoordSys、Point 等基础数学结构
 *      - 保持物理原语与几何/渲染模块分离，确保系统模块化
 *
 *   4. **扩展性强**：
 *      - 模块设计为可扩展原语集合，未来可加入 Force、Constraint 等物理计算原语
 *      - 提供统一接口，方便 Physics System、Collision System 调用
 */

#ifdef _PHYSICSPRIMATIVE
#define _PHYSICSPRIMATIVE_EXP __declspec(dllexport)
#else
#define _PHYSICSPRIMATIVE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT