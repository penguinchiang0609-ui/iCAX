# Behaviour

`Behaviour` 是 Framework 中的行为系统文档目录。

Behaviour 把 Component 对应的 system/行为逻辑独立出来。它不拥有数据，不保存项目容器，也不决定线程和帧节奏；Project 持有 Repository、Universe 和运行时调度器，并在每帧或 Repository 事件发生时把 `ApplicationContext`、`ProductContext`、`ProjectContext` 显式传给 Behaviour。

## 目录结构

```text
Behaviour/
  Behaviour规格文档.md
  Behaviour方案文档.md
  README.md
```

