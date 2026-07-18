# Machine

通用机床插件。独立编译机床组件、SDF/URDF加载、实例化命令、运动约束、渲染附件和碰撞Behaviour。

Laser3DCAM仅消费Machine组件和命令，不再拥有或编译机床实现。当前迁移阶段公共源文件仍位于Laser3DCAM目录，由Machine工程独占编译；后续只做物理目录整理，不再改变二进制依赖边界。
