# ExtrusionRecognition

`ExtrusionRecognition` 是与 TubeOne、型钢等产品解耦的拉伸体基础识别插件。

- Service 输入尚未进入资源池的中立 `GeometryData::BRepModel`。
- Service 按调用方传入顺序执行外部声明式截面类型定义，首个匹配成功的定义生效。
- 成功结果包含规范化 BRep、截面类型 ID、动态参数和长度；资源发布与业务组件创建由调用方负责。
- C++ 扩展点是可复用的规则算子 `ISectionRuleOperator`，不是具体管型类。
- Service 不持有 Product、Project、Scene、资源池或匹配顺序状态。

