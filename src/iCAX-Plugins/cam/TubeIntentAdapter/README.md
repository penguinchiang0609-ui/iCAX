# TubeIntentAdapter

管材来源适配模块。`CompileTubeMachiningIntent` 接收中性管材几何，产生可供刀路来源接入使用的特征和 UV 轨迹；其支持范围及诊断保持原有语义。

该模块独立于 `IntentToolpath`。需要管材编译的调用方显式包含 `TubeIntentAdapter/TubeMachiningIntent.h` 并链接本模块，公共刀路内核及平面/空间编辑宿主不加载它。

管材特征识别、候选解释和来源求交状态由适配器提供；基础刀路的身份、编辑、工艺操作、删除、保存、撤销和有效输出统一使用 [IntentToolpath](../IntentToolpath/README.md)，不在本模块再建一套刀路模型。
