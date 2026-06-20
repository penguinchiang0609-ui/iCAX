# pdo

`pdo` 放置 H5 侧 PDO 封装。这里负责高频数据订阅、缓存、脏标记和 UI 数据绑定。

PDO 用于高频、可覆盖、可丢弃的过程状态，例如视图矩阵、仿真进度、鼠标命中结果、运行状态快照。普通命令仍走 Mailbox。

## 目录结构

- `README.md`：本目录说明。
- 后续可放置 PDOStore、subscriber、schema binding、frame scheduler 和 UI binding。

## 使用边界

- PDO 不承载业务命令。
- PDO 不替代 Repository。
- PDO 快照可被覆盖，UI 只关心最新值。
