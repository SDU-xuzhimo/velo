# Renderer 部分源码导读（Browser↔Renderer 对象对应）

> 第二阶段 · 第三部分 | 代码源：/Volumes/externelSSD/chromium/src（Chrome 146）

---

## 定位

- **核心问题**：Browser 里的 Frame + Renderer 里的 Frame，为什么两个 Frame？
- **关键文件**：
  - `content/public/browser/render_widget_host.h`（RenderWidgetHost 公开接口 + 三种场景注释）
  - `content/browser/renderer_host/render_frame_host_impl.h`（RFH 实现）
  - `content/renderer/render_frame_impl.h`（Renderer 侧翻译官）
  - `third_party/blink/renderer/core/frame/web_local_frame_impl.h`（Blink frame）
  - `third_party/blink/renderer/core/frame/web_frame_widget_impl.h`（Blink widget）

---

# Phase 1 — L1 架构层

## 1. RenderWidgetHost 是什么

`render_widget_host.h:65` 原话："A RenderWidgetHost acts as the abstraction for **compositing and input** functionality."

**分工**：`RenderFrameHost` 管内容与导航，`RenderWidgetHost` 管输入与合成——两条独立通道。

### 三种存在场景（头文件注释原文翻译）

| 场景 | 例子 | 谁拥有 |
|---|---|---|
| Popup | `<select>` 下拉菜单、HTML 日历控件 | WebContents |
| 主 frame | 每个 WebContents 的 root frame | RenderViewHost |
| Child local root | **跨进程 iframe 的顶端** | RenderFrameHost |

## 2. local root（本地根）——核心新概念

**定义**：与父 frame 不在同一进程的 frame（跨进程边界）。头文件原话："they live in their own coordinate space and are an independent unit of compositing and input."

```
FrameTreeNode #1（主 frame）──────── 进程 A
  ├─ #2（iframe，同 site）────────── 进程 A   ← 与父同进程，非 local root
  └─ #3（iframe，跨 site）────────── 进程 B   ← ★ local root
       └─ #4（iframe，同 site）───── 进程 B   ← 与父同进程，非 local root
```

**数量关系**：local root 数 = RenderWidgetHost 数。同进程的 iframe 共享父 local root 的 RenderWidgetHost。

## 3. Browser ↔ Renderer 完整对应表

| 关注点 | Browser 侧 | Renderer 侧 content 层 | Renderer 侧 Blink 层 |
|---|---|---|---|
| frame | `RenderFrameHostImpl` | `RenderFrameImpl` | `WebLocalFrame` / `LocalFrame`（DOM） |
| widget | `RenderWidgetHostImpl` | —（无中间层） | `WebFrameWidget` |
| 页面 | `RenderViewHost`（146 已简化） | — | `WebView` |
| 进程 | `RenderProcessHost` | `AgentSchedulingGroup` | — |

**注意**：widget 线 Renderer 侧**没有 content 层中间层**（RWH 直接和 Blink WebFrameWidget 对话）；frame 线有 RenderFrameImpl 当翻译官。

## 4. RenderFrameImpl 双重身份

```cpp
class RenderFrameImpl
    : public RenderFrame,
      public blink::WebLocalFrameClient,     // 身份1：Blink 回调接收者（上行）
      public mojom::Frame,                    // 身份2：Browser 命令接收者（下行）
```

**两条线**：

| 方法 | 接口 | 方向 | 谁调用 |
|---|---|---|---|
| `CommitNavigation`（:2575） | `mojom::Frame` | **下行**：Browser → Renderer | RenderFrameHostImpl（Mojo，:15923 发出） |
| `DidCommitNavigation`（:3876）/ `DidFinishLoad`（:4158） | `WebLocalFrameClient` | **上行**：Blink → Browser | Blink LocalFrame |

## 5. 两条通道的数据流

### frame 线（内容/导航）——贯穿三站的例子

```
用户点击 <a> → Blink 决定导航 → WebLocalFrameClient::BeginNavigation（:5455）
  → RenderFrameImpl 转译 Mojo → Browser → NavigationRequest 接管
  → ...（throttle/网络/SiteInstance/speculative RFH）
  → mojom::Frame::CommitNavigation → RenderFrameImpl → 字节流交给 Blink
  → Blink 流水线自跑（RenderFrameImpl 退场，只在边界服务）
  → DidFinishLoad → RenderFrameImpl → Mojo → WebContentsObserver
  → embedder（AwContentsClientBridge → App onPageFinished）
```

**重要**：Renderer 发起的导航（点链接/JS/表单）从 `BeginNavigation` 进 → 最终仍归 Browser 管——所有导航无论谁发起都走同一条链。

### widget 线（输入/合成）

```
输入（下行）：平台事件 → Browser UI 线程 → RenderWidgetHostImpl（路由到正确 local root）
            → Mojo → WebFrameWidget → Blink 事件处理

合成（上行）：Blink Paint → cc → CompositorFrame → viz → RenderWidgetHostView
            （平台视图接原生 UI）→ GPU 进程 → 屏幕
```

**输入为什么必须先到 Browser**：
1. Browser 拥有平台窗口，所有平台事件天然先到它
2. 多进程场景只有 Browser 掌握各 local root 的屏幕位置归属——路由决策必须在这
3. **安全（信源可信）**：输入代表用户意图。若允许 Renderer 直接收发输入，被攻破的 Renderer 可伪造用户意图（伪造点击/跨 frame 伪造/绕过点击劫持防护）。Browser 作为可信中介保证"事件真实来自平台 + 路由到正确 frame"

## 6. 核心问题最终答案：「为什么两个 Frame」

**一句话**：进程隔离把“内容”（DOM/JS/渲染）关进沙箱进程，把“控制”（导航/安全/生命周期）留在 Browser——同一逻辑 frame 在两个进程各有一个分身：`RenderFrameHost`（控制面）+ `LocalFrame`（数据面），`RenderFrameImpl` 是连接分身的翻译官。

**三理由**（串起前几站）：
1. **安全**（第三站）：内容不可信必须沙箱化，控制必须在可信 Browser
2. **Crash 恢复**（SiteInstance 补完）：Renderer 死了 DOM 没了，Browser 侧 RFH+FrameTree 还在
3. **导航决策**（Navigation 补完）：导航 Browser 主导，必须有不依赖 Renderer 的 frame 表示

---

# 追加：重点类介绍与关系（后续探讨整理）

## 命名规范：Host = 远端对象的本地代理

| Browser 侧（Host） | Renderer 侧对应 |
|---|---|
| `RenderProcessHost` | Renderer 进程本身（content 层 `AgentSchedulingGroup`） |
| `RenderFrameHost` | `RenderFrameImpl` → Blink `LocalFrame` |
| `RenderWidgetHost` | Blink `WebFrameWidget` |
| `RenderViewHost` | Blink `WebView` |

口诀：**Host = “它在我这儿的代理人”**。

## 三层关系：FrameTree / FrameTreeNode / RenderFrameHost（身份 vs 文档）

```
FrameTree（页面级：整棵树，WebContents 拥有）
  │  持有 root FrameTreeNode + NavigationController
  ▼
FrameTreeNode（节点级：一个逻辑 frame 的"身份/槽位"）    ★ 跨导航持久
  │  持有 navigation_request_
  ▼
RenderFrameHostManager（切换管理者）
  ├─ current RFH        ← 当前显示文档的代理
  └─ speculative RFH    ← 导航中新建（commit 后升格）
```

| | FrameTreeNode | RenderFrameHost |
|---|---|---|
| 代表 | 逻辑 frame 的**身份**（槽位） | 槽里**当前文档**的代理 |
| 生命周期 | 跨导航持久（主 frame 复用） | 随导航更换（跨 site → 换 RFH） |
| 数量 | 一个 | 一生对应多个（先后）；同刻 current + 可选 speculative |

**口诀：身份不动、文档换血。**

## RenderFrameProxyHost / RemoteFrame（跨进程替身）

frame 树跨 N 个进程时，每个进程的 Blink 都需要知道整棵树结构：
- **RemoteFrame**（Blink 侧）：其他进程 frame 的占位（无 DOM，纯引用）
- **RenderFrameProxyHost**（Browser 侧）：管理这个替身的对象

每个 FrameTreeNode 实际关联：**一个 RenderFrameHost**（文档所在进程，干活）+ **N 个 RenderFrameProxyHost**（其他进程，保结构完整）。

## RenderFrameImpl 四大职责

```
1. 创建并持有 Blink 对象
   CreateMainFrame (:1592) / CreateFrame (:1685)
   → 创建 WebLocalFrame/WebView，注册自己为 WebLocalFrameClient

2. 下行翻译（mojom::Frame）
   CommitNavigation (:2575) ← Browser 命令转译给 Blink

3. 上行翻译（WebLocalFrameClient）
   BeginNavigation (:5455) / DidCommitNavigation (:3876) / DidFinishLoad (:4158)

4. 服务台（Blink 没有的能力）
   子资源请求 → URLLoaderFactory；权限；storage；BrowserInterfaceBroker

不做：DOM 解析/样式/排版/绘制 —— 全在 Blink
```

## 流水线边界（重要）

**渲染流水线不以 RenderFrameImpl 为上下文**——DOM 解析、样式计算、排版、绘制全在 Blink 内部：

```
WebView → WebFrameWidget → LocalFrame → Document ★（流水线核心上下文）
  ├─ HTMLDocumentParser（DOM 解析）
  ├─ StyleResolver（样式计算）
  ├─ LayoutView → LayoutObject 树（排版）
  └─ Paint 体系 → cc
```

RenderFrameImpl 只在交接点（CommitNavigation 把字节流交给 Blink）+ 关键节点回调（DidFinishLoad 等）介入。**第四部分流水线的正确入口：`third_party/blink/renderer/core/frame/local_frame.cc` → `document.cc`**，不是 render_frame_impl.cc。

## 贯穿三站的类比

```
AwContents 之于 content  =  RenderFrameImpl 之于 Blink
```
同一个"embedder/翻译官"模式嵌套两层：AwContents 把 content 嵌入 Android；RenderFrameImpl 把 Blink 嵌入 content。

---

# Check 验收记录（Sep 6）

- **题1 local root 计数**：✅ 全对（3 RWH / 3 renderer+1 browser 进程 / {主+iframeA} 共享）
- **题2 双重身份**：AI 作答（CommitNavigation=下行 mojom::Frame；DidCommitNavigation/DidFinishLoad=上行 WebLocalFrameClient）
- **题3 输入路由**：✅ 方向对（不可信任 + 平台能力），补锋利化：信源可信原则——伪造用户意图/跨 frame 伪造/点击劫持防护
