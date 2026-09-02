# IntentToolpath

通用意图刀路领域插件，供平面、管材和三维线切割产品复用。

插件负责：

- 意图刀路和生成树。
- CAD、其他意图、绘制、构造、模板及导入来源。
- 参数化生成操作、稳定输出角色和替代关系的数据表达。
- 通用二维/三维曲线及承载曲面资源。
- 从管材中性模型编译机器无关的加工特征、Side Atlas 边界轨迹、端切候选、坡口关联与通用 UV 表面特征。
- 多工件归属和通用切割、微联、引线、桥接工艺切面。

插件依赖 `GeometryData`、`GeometryAdapter` 和 `GeometryAlgo`，但不依赖 OpenCascade、具体机床、具体CAM产品或最终NC表达。具体特征识别、微联生成、桥接生成和曲面映射算法通过产品或算法插件实现。

`CompileTubeMachiningIntent` 只编译中性模型中已经确定的语义。WrappedVolume、端部候选和 UVFeature 可直接得到 UV 轨迹；ExtrudedRegion、TaperedRegion、HalfSpace、CompositeVolume 和 ResidualBRep 会保留稳定来源并标记为需要几何求交。CompositeVolume 额外传递解析面数量和是否支持人工特征提升，不把半参数化复合体误报成完全不透明 residual。UVFeature 只表示零深度的表面曲线、区域或图片，不预设为打标。未选择的 `AlternativeNode` 会完整传递候选 ID，并将结果标为 `RequiresInterpretationSelection`，不会把推荐项冒充成用户或策略已经确认的解释。该层不包含加工排序、割缝补偿、引线、速度或机床姿态。

减材的 `RemovalSemantics` 会原样传入机器无关意图，其中 Blind 只表示几何在材料内部终止。具体选择打标、激光半切、钻削或铣削属于后续加工策略，不由中性 CAD 或意图编译器决定。
