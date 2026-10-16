# Blink Rendering Pipeline 源码导读

> 第二阶段 · 第四部分 | 代码源：/Volumes/externelSSD/chromium/src（Chrome 146）
> **边界原则：讲结构与职责（类/函数是什么、输入输出、环节怎么衔接），不讲算法与实现。**

---

## 衔接点：流水线从哪里启动（接 Renderer 部分）

```
RenderFrameImpl::CommitNavigation(...)            content 翻译官（render_frame_impl.cc:2575）
  │  把 response body 的 data pipe、URLLoaderFactory 打包成 NavigationParams
  ▼
frame_->CommitNavigation(std::move(navigation_params))   （:3001）
  │  frame_ = blink::WebLocalFrame*
  ▼
WebLocalFrameImpl::CommitNavigation(...)          Blink 出口层（web_local_frame_impl.cc:2782）
  ▼
DocumentLoader                                    ★ Blink 加载协调者（core/loader/document_loader.h:137）
  ▼
HTMLDocumentParser                                ★ 流水线第一棒
```

**关键函数**（衔接链的函数级锚点）：
- `RenderFrameImpl::CommitNavigation`（render_frame_impl.cc:2575）→ `frame_->CommitNavigation`（:3001）
- `WebLocalFrameImpl::CommitNavigation`（web_local_frame_impl.cc:2782）
- `DocumentLoader::CommitNavigation()`（document_loader.h:298）——Blink 侧 commit 入口
- `DocumentLoader::CommitData(BodyData&)`（:578）——**字节流进 Blink 的口子**
- `DocumentLoader::ProcessDataBuffer(BodyData*)`（:581）——数据缓冲调度
- `DocumentLoader::CreateParserPostCommit()`（:537）——**创建 parser**（流水线启动）
- `DocumentLoader::FinishedLoading(...)`（:569）——加载完成 → 触发 DidFinishLoad 上行回调

---

## ① DOM 解析（HTML 字节流 → DOM 树）

### 关键类与函数

**`DocumentLoader`**（`core/loader/document_loader.h:137`）——加载协调者
- `CommitData` / `ProcessDataBuffer`：把网络数据喂给 parser
- `CreateParserPostCommit`：创建 `HTMLDocumentParser`

**`HTMLDocumentParser`**（`core/html/parser/html_document_parser.h:89`）——解析入口
- `AppendBytes(bytes)`（:130）——**接收字节流的入口**（DocumentLoader 喂进来的）
- `PumpTokenizer()`（:216）——把字节流转成 token 再建 DOM 节点（内部循环，概念级了解）
- `insert(const String&)`（:150）——script 等场景直接插内容
- `Flush()`（:131）——数据结束时收尾
- 特性：**增量解析**（分块到达分块解析）；**可暂停**（script 标签暂停解析执行 JS）

**`Document`**（`core/dom/document.h:359`）——DOM 树根 + 后续②③④的骨架

**preload scanner**（`core/html/parser/html_preload_scanner*`）
- `FlushPendingPreloads()`（html_document_parser.h:185）——主解析被 script 阻塞时，旁路扫描器提前发现资源并发起请求（首屏速度关键）

### 🚫 边界外
tokenizer 状态机、tree builder 插入模式、解析容错、自定义元素升级。

### 症状关联
Parse HTML 段长；JS 阻塞时资源请求晚（preload scanner 场景）。

---

## ② 样式计算（DOM + CSS → ComputedStyle）

### 关键类与函数

**`StyleEngine`**（`core/css/style_engine.h`）——样式管理者
- `UpdateStyleAndLayoutTree()`（:671）——**样式失效重算的驱动入口**
- `UpdateStyleInvalidationRoot(ancestor, dirty_node)`（:452）——标记哪些子树样式脏了
- `UpdateStyleRecalcRoot(...)`（:453）——计算需要重算的最小根

**`StyleResolver`**（`core/css/resolver/style_resolver.h`）——计算入口
- `CreateComputedStyleBuilder()`（:92）——为元素构建最终样式的入口（选择器匹配+级联+继承在此发生，算法不讲）

**`ComputedStyle`**（`core/css/computed_style.h`）——输出物，挂在 LayoutObject 上，是③的输入

### 🚫 边界外
选择器匹配算法、级联排序实现、样式共享缓存、CSS 变量求值。

### 症状关联
Recalculate Style 段慢（大量 CSS/深选择器）。

---

## ③ Layout（排版：算几何）

### 关键驱动类（146 版重要补充）：`LocalFrameView`

**`LocalFrameView`**（`core/frame/local_frame_view.h`）——**每个 LocalFrame 一个 View，是 Style/Layout/Paint 三个阶段的驱动者**（146 版流水线的驱动核心）：
- `UpdateStyleAndLayout()`（:233）——**对外统一入口**：先跑②样式重算，再跑③排版
- `UpdateLifecyclePhasesInternal(...)`（:998）——按序推进 lifecycle 阶段
- `UpdateStyleAndLayoutIfNeededRecursive()`（:1012）——递归驱动子 frame
- **Document Lifecycle 阶段枚举**也定义在此（kStyleClean / kLayoutClean / kPaintClean——Blink 官方的流水线阶段划分，顺序不可乱）

### 关键类

**`LayoutObject`**（`core/layout/layout_object.h:272`）——排版树节点抽象
- `NeedsLayout()`（:1411）——几何是否失效
- `SetNeedsLayout` 系列——JS 改几何属性后在此标记失效（标记-集中处理模式的"标记"侧）
- 核心认知：**Layout 树是 DOM 的子集**（display:none 不生成）

**`LayoutView`**（`core/layout/layout_view.h`）——排版树根，对应 Document

**Fragment**（概念）——LayoutNG 输出物：不可变排版片段（位置/大小确定）

### 🚫 边界外
LayoutNG 求解算法（BFC/flex/grid）、行内排版、文本测量。

### 症状关联
强制同步布局（读写交替 offsetHeight）→ Layout 段暴涨。

---

## ④ Paint 记录（生成"怎么画"的指令清单）

### 关键类与函数

**`LocalFrameView`**（同③的驱动者）——lifecycle 推进到 Paint 阶段（kPaintClean）

**`PaintLayer`**（`core/paint/paint_layer.h:169`）——分层概念（stacking context / transform / will-change 成为独立层，是⑤合成分层的基础）

**`DisplayItemList`**（输出物）——**指令清单，不是像素**。"在 (x,y) 画矩形"这类指令的列表。真正画像素在⑦。

**`PropertyTree`**（transform/clip/effect 三棵）——变换/裁剪/效果的抽象。⑤合成器靠它做动画（改 transform 只改树上的值）。

### 🚫 边界外
DisplayItemList 编码优化、paint ops 类型清单、partial paint 机制。

### 症状关联
大区域重绘；layer 爆炸。

---

## ⑤ 合成（cc：Layer 树 + 合成器线程）

### 关键类与函数

**`cc::LayerTreeHost`**（`cc/trees/layer_tree_host.h:142`）——cc 在 Renderer 侧入口
- `SetNeedsRedrawRect(damage_rect)`（:407）——标记需要重绘的区域
- `SetNeedsCommitWithForcedRedraw()`（:412）——强制 commit 一帧
- `SetNeedsDisplayOnAllLayers()`（:714）——全部失效（少见）

**`cc::Layer`**——合成层抽象（Blink 的 PaintLayer 内容落到 cc Layer）

**`Compositor`（合成器线程）**——Renderer 进程内独立线程：滚动、transform/opacity 动画、Tile 管理。**不占主线程**——JS 卡死时滚动/动画依然流畅的秘密。

### 🚫 边界外
cc 调度器状态机（BeginFrame 细节）、layer 提升启发式。

### 症状关联
transform 动画掉帧（没成为独立 layer 就回主线程跑 Layout/Paint）。

---

## ⑥ Tile 分块 + ⑦ 光栅化

### 关键类与函数

**`cc::TileManager`**（`cc/tiles/tile_manager.h:90`）——概念级：内容按块（如 256×256）切 tile
- `ScheduleTasks(PrioritizedWorkToSchedule)`（:341）——**按可视优先级排队光栅化任务**（视口内优先）

**光栅化位置**：GPU 光栅化在 GPU 进程执行；软件光栅化在 Renderer 进程。只需知道"光栅化把指令清单变成像素，可以不在主线程"。

### 🚫 边界外
tile 内存池、Skia 光栅化实现、raster buffer 管理。

### 症状关联
快速滚动白块（tile 没来得及光栅化）；滚动后短暂模糊（低分辨率 tile 先顶上）。

---

## ⑧ viz 最终合成（CompositorFrame → 屏幕）

### 关键类与函数

**`CompositorFrame`（跨进程货币）**——所有视觉内容的统一交换格式。每个 local root 一个 frame。

**viz display compositor**（`components/viz/service/display/`）
- `DisplayScheduler`（display_scheduler.h）——调度最终合成
- `DrawAndSwap`（display_scheduler.h:105 附近）——**把合成结果画出来并交换上屏**的最终动作

衔接：这就是 Renderer 部分讲的合成流出口（cc → viz → RenderWidgetHostView → GPU）。

### 🚫 边界外
SurfaceAggregator 内部、GPU 命令编码。

---

## 贯穿线 1：一帧的生命周期（时间维度）

```
┌─ 主线程一帧（~16.7ms @60fps）────────────────────────┐
│ Task（宏任务：JS/事件回调）                              │
│   └→ 微任务清空（Promise.then）                         │
│ requestAnimationFrame 回调（改 DOM/样式的最佳时机）      │
│   └→ Style（LocalFrameView lifecycle: kStyleClean）    │
│   └→ Layout（kLayoutClean）                            │
│   └→ Paint 记录（kPaintClean）                          │
│ → 提交合成器线程                                         │
│   Composite（滚动/动画/合成新帧——合成器线程）              │
│ 空闲 → requestIdleCallback                              │
└───────────────────────────────────────────────────────┘
```

各 API 插位：`setTimeout`→宏任务队列；`Promise.then`→当前宏任务尾部微任务；`rAF`→样式计算前（rAF 里改样式只跑一次）；`rIC`→帧末空闲。
**JS 长任务卡死一切**：Task 不结束，Style/Layout/Paint 全排队等待。

## 贯穿线 2：失效机制（增量维度）

| 改动 | 重跑层 | 代价 |
|---|---|---|
| `transform`（已是独立 layer） | 只更新 PropertyTree → 合成器用旧光栅结果 | **最便宜**（主线程不参与） |
| 改颜色/背景 | Paint → 重光栅化该 tile | 中 |
| 改宽高（reflow） | Layout → Paint → Raster 全链 | **贵** |
| 强制同步布局（读写交替 offsetHeight） | JS 执行中**同步**跑 Layout，反复多次 | **最贵**（性能杀手） |

**原则**：失效是"标记-集中处理"——JS 改 DOM 只标记（`SetNeedsLayout` 等），真正的 Style/Layout/Paint 在 rAF 后由 `LocalFrameView::UpdateStyleAndLayout` 集中跑一次；强制同步布局打破此模式，当场重排。

---

## 全景串讲（三站 + 本阶段全连接）

```
App loadUrl → AwContents → NavigationController → NavigationRequest（Browser 主导）
→ Network Service 发 HTTP → CommitNavigation → Mojo → RenderFrameImpl
→ DocumentLoader::CommitData → HTMLDocumentParser::AppendBytes（①DOM）
→ StyleEngine::UpdateStyleAndLayoutTree（②Style）
→ LocalFrameView::UpdateStyleAndLayout（③Layout）
→ Paint 记录（④）
→ cc::LayerTreeHost（⑤合成器线程）→ TileManager::ScheduleTasks（⑥⑦）
→ CompositorFrame → viz DisplayScheduler::DrawAndSwap（⑧）→ GPU → 屏幕
→ DocumentLoader::FinishedLoading → DidFinishLoad → RenderFrameImpl
→ Mojo → AwContentsClientBridge → onPageFinished
```

## 🎯 Check

1. **线程题**：页面 JS 陷入 `while(true){}`，用户滚动页面。哪些还能动、哪些不能动？为什么？
2. **失效题**：`element.style.transform = "translateX(100px)"`（已是独立 layer）vs `element.style.width = "100px"`——分别重跑哪些层？为什么？
