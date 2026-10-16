# 大型工程源码学习方法论（Architecture First）

> 核心思想：
>
> **不要追求记住代码，而要追求建立 Context（上下文）。**
>
> 对于大型工程，真正重要的不是 Memory（记忆），而是能够迅速恢复 Context（上下文），重新进入源码。

---

# 一、源码理解的五个层次

对一个模块的理解分成五个层次。

只有前两层属于"长期记忆"。

后三层属于"按需恢复"。

```
Architecture（架构）
        │
        ▼
Design（设计）
        │
        ▼
Object Model（对象关系）
        │
        ▼
Execution（执行流程）
        │
        ▼
Implementation（实现细节）
```

---

# Level 1：Architecture（架构层）★★★★★

这是最重要的一层。

也是应该长期记住的一层。

需要回答：

- 它属于 Chromium 哪一层？
- 它和哪些模块交互？
- 它解决什么问题？
- 为什么会存在？

例如：

TileManager

```
Renderer

↓

Paint

↓

TileManager

↓

Raster

↓

GPU
```

需要知道：

> TileManager 是 Rendering Pipeline 的一部分。

而不是：

TileManager 有哪些成员变量。

---

需要输出：

- 一张架构图
- 一张调用链

例如：

```
WebView

↓

AwContents

↓

WebContents

↓

NavigationController

↓

NavigationRequest

↓

Renderer

↓

Blink
```

---

# Level 2：Design（设计层）★★★★★

这是第二重要。

理解：

    为什么。

而不是：

    是什么。

需要回答：

    为什么需要它？
    为什么不用别的方法？
    设计目标是什么？
例如：
    Tile
    
    为什么：

        整个网页不能直接Raster？
    为什么：

        需要Tile？
    为什么：

        需要Priority？

    为什么：

        GPU Memory有限？

    为什么：

        Raster要异步？

如果这些问题都知道。

说明真正理解了设计。

---

# Level 3：Object Model（对象关系）★★★★☆

这一层关注：

    对象。

不是代码。

需要知道：

    谁创建？

    谁持有？

    谁销毁？

    生命周期？

例如：

Tile

```
LayerTreeHostImpl

↓

TileManager

↓

Tile

↓

RasterTask
```

需要回答：

    Tile是谁创建？

    谁管理？

    什么时候释放？

不要背：

    成员变量。

---

# Level 4：Execution（执行流程）★★★★☆

理解：

调用链。

例如：

loadUrl()

```
WebView.loadUrl()

↓

AwContents

↓

WebContents

↓

NavigationController

↓

NavigationRequest

↓

Renderer

↓

Blink
```

或者：

一次Render。

```
HTML

↓

DOM

↓

Style

↓

Layout

↓

Paint

↓

Tile

↓

Raster

↓

GPU
```

这里需要做到：

能够画出来。

不用全部记住函数。

---

# Level 5：Implementation（实现层）★★★☆☆

最后。

才是：

代码实现。

例如：

TilePriority

PrioritizedTileSet

std::vector

成员变量。

各种算法。

这些：

不用长期记忆。

需要：

快速恢复。

---

# 二、什么应该长期记忆？

建议：

## 永久记忆（必须）

Architecture

Design

Object Relationship

例如：

知道：

```
RenderFrameHost

↓

RenderFrameImpl

↓

Blink::Frame
```

之间关系。

---

## 可以遗忘（没关系）

成员变量

函数实现

模板参数

具体算法

枚举

这些：

IDE一分钟。

全部回来。

---

# 三、真正应该培养的能力

不要培养：

"背源码"

而是培养：

## Source Navigation（源码导航能力）

例如：

别人问：

Tile什么时候释放？

你应该知道：

```
TileManager

↓

LayerTreeHostImpl

↓

RasterTask
```

应该去哪。

而不是：

直接背代码。

---

目标：

> 十分钟恢复整个模块 Context。

而不是：

一年以后还能背代码。

---

# 四、学习一个模块的固定模板

每学习一个模块。

必须回答下面五个问题。

## 1.

它为什么存在？

解决什么问题？

---

## 2.

它位于整个系统哪里？

画架构图。

---

## 3.

生命周期？

谁创建？

谁持有？

谁释放？

---

## 4.

执行流程？

调用链？

事件如何流转？

---

## 5.

如果今天线上出了Bug。

第一步打开哪个文件？

如果能回答。

说明真正掌握。

---

# 五、AI时代正确阅读源码的方法

错误方式：

```
AI解释源码

↓

感觉懂了

↓

关闭
```

结果：

三天以后忘。

---

正确方式：

```
AI解释架构

↓

自己画图

↓

自己追调用链

↓

自己回答问题

↓

AI纠正
```

AI应该：

成为：

导师。

Debugger。

Reviewer。

而不是：

阅读器。

---

# 六、每学完一个模块必须输出

不要只阅读。

一定输出。

建议：

## 输出一：

一张架构图。

---

## 输出二：

一张调用链。

---

## 输出三：

500字总结。

包括：

- 为什么存在
- 生命周期
- 核心对象
- 调用流程

---

## 输出四：

修改一次源码。

哪怕：

增加Log。

增加Trace。

增加Feature Flag。

---

# 七、衡量是否真正掌握一个模块

不是：

我能不能背出来。

而是：

我能不能回答：

□ 一周以后还能画架构图？

□ 一个月以后还能画调用链？

□ 半年以后还能10分钟恢复Context？

□ 出Bug知道去哪？

□ 能解释为什么这样设计？

如果：

全部Yes。

说明：

这个模块真正属于你了。

---

# 八、最终目标

不要成为：

> 记住 Chromium 源码的人。

而要成为：

> **能够快速进入任何 Chromium 模块 Context 的工程师。**

因为：

大型工程没有人能全部记住。

真正的竞争力来自：

Architecture Thinking

+

Source Navigation

+

Context Recovery

而不是：

Memory。