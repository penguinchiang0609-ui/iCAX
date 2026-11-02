# CAM 通用组件

此目录保存跨切割产品复用的 CAM 组件设计，不归属于某个产品。

- [IntentToolpath：通用可编辑刀路](IntentToolpath/README.md)：刀路对象、基础编辑、工艺衍生、来源与稳定身份、有效加工输出。
- `ProcessPath`：机床无关执行路径及其后续规划结果，当前实现说明见 [源码说明](../../../src/iCAX-Plugins/cam/ProcessPath/README.md)。

产品负责将 CAD、材料、装夹和产品交互接入这些组件。Engine 不反向依赖 CAM 插件；通用刀路组件不反向依赖具体 CAM 产品。
