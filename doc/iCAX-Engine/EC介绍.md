# ECS



## 为何需要EC？

假设我们有一个场景Scene，里面一个是正方体，那么我可以设计这么几个类：

```c++
class Entity
{
	float[3] m_Position;
};

class Box : public Entity {};

class Scene
{
	std::vector<Entity> m_Entities;
};
```

在C++里，public继承是is-a的关系，private继承是has-a的关系，那么如果我需要两个正方体，一个可以播放音频，一个可以播放动画，那么代码是这样：

```c++
class Entity 
{
	float[3] m_Position;
};

class Box : public Enitty {};

class Scene
{
	std::vector<Entity> m_Entities;
};

class BoxWithAudio : public Box {};
class BoxWithAnimator : public Box {};

```

当如果Box既可以播放音频、又可以播放动画，那么一共会有这么些类：

```C++
class Box : public Enitty {};
class BoxWithAudio : public Box {};
class BoxWithAnimator : public Box {};
// 出现了multiple inheritance
class BoxWithAnimatorAndAudio : public BoxWithAnimator, public BoxWithAudio {};

```

这里很容易就出现了一个类继承于两个类的情况，而BoxWithAnimator和BoxWithAudio 又同时继承于Box，所以这是个菱形继承。Box类才添加了两个功能就会造成这么复杂的类设计，所以说这种写法，很容易造成类混乱，更何况其他的很多语言里根本不支持多重继承。

所以，这里有个更好的设计思路，代码如下：
```c++
class Entity 
{
	std::vector<Component> m_Components;
	float[3] m_Position;
};

class Scene
{
	std::vector<Entity> m_Entities;
};

class BoxComponent : public Component {};
class AudioComponent : public Component {};
class AnimatorComponent : public Component {};

// 创建Entity
Entity boxWithAnimatorAndAudio;
boxWithAnimatorAndAudio.AddComponent(new BoxComponent());
boxWithAnimatorAndAudio.AddComponent(new AudioComponent());
boxWithAnimatorAndAudio.AddComponent(new AnimatorComponent());

```

此时的Entity，它需要什么功能，就会在其`m_Components`里添加对应的`Component`组件，这样代码设计就不混乱了。



