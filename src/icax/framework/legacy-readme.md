# 定位

- 本目录为框架代码：EC+System+Service

  - Databse：EC
  - System：System/Behaviour，提供Universe-world-behaviourSet。每个World可以拥有自己的behaviourSet
  - Service：
  - Engine：封装出IAPP接口，粘合Database、System、Service

- Tracker 配合Database实现undo/redo与事务。

  