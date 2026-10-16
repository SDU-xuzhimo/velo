# content/ 核心六对象 源码导读

> 模块：`BrowserContext` / `WebContents` / `NavigationController` / `NavigationRequest` / `RenderFrameHost` / `RenderProcessHost`
> 方法论：架构优先（Architecture First），按 L1→L5 逐层推进。
> 前置：android_webview 是 content/ 的 embedder，AwContents 拥有 content::WebContents。

---

## 定位

- **根目录**：`content/`，Chromium 的浏览器内核层。
- **目录分层**（本身就是 content 的设计）：

| 目录 | 角色 | 谁能用 |
|---|---|---|
| `content/public/browser/` | **稳定 API 边界**，供 embedder 调用 | Chrome / WebView / Electron / CEF |
| `content/browser/` | **内部实现**（不对外暴露） | 仅 content 自己 |
| `content/renderer/` | **Renderer 进程侧**实现 | 仅 content 自己 |
| `content/common/` | Browser + Renderer **共享**的代码 | 两边都用 |

- **六个核心对象定位**：

| 对象 | 头文件 | 在哪 | 性质 |
|---|---|---|---|
| `BrowserContext` | `content/public/browser/browser_context.h:108` | public | **公开 API** |
| `WebContents` | `content/public/browser/web_contents.h:131` | public | **公开 API** |
| `NavigationController` | `content/public/browser/navigation_controller.h:74` | public | **公开 API** |
| `RenderFrameHost` | `content/public/browser/render_frame_host.h:144` | public | **公开 API** |
| `RenderProcessHost` | `content/public/browser/render_process_host.h:129` | public | **公开 API** |
| `NavigationRequest` | `content/browser/renderer_host/navigation_request.h:104` | browser | **内部实现** |

> ⚠️ 五个在 `public/`（API），一个在 `browser/`（实现）。`NavigationRequest` 是内部实现细节，embedder 看不到——它通过 `NavigationHandle`（公开 API）暴露给 embedder。

---

# Phase 1 — L1 架构层

## 根本使命：为什么会有 `content/` 这一层

`content/` 是 Chromium 的**浏览器内核层**——它提供一组**平台无关、embedder 无关**的核心 API，让任何想嵌入 Chromium 内核的应用（Chrome、WebView、Electron、CEF）都能复用同一套多进程浏览器基础设施。

它解决四个核心问题：

1. **多进程管理**：Browser 进程怎么管 Renderer 进程？
2. **页面表示**：一个网页在 Browser 侧怎么被表示和管控？
3. **导航**：一个 URL 怎么变成一个加载完成的页面？
4. **会话隔离**：不同用户/模式怎么隔离？

### 和 android_webview 的关系（衔接上一站）

```
App → WebView → AwContents(Native) → content::WebContents → ... → Blink
```

`AwContents` **拥有** `content::WebContents`——`WebContents` 就是 content/ 的核心类。android_webview 是 content/ 的 **embedder**，通过 `content/public/browser/` 的 API 使用 content 层能力。现在从 embedder 侧下沉到内核侧。

## 六个对象各自的角色

### 会话层

**`BrowserContext`** —— 浏览会话的上下文
- BrowserContext是Browser级服务的入口，例如通过GetDownloadManager，GetStoragePartition（cookie服务入口）获取相应服务的对象，其不直接存储或处理这些服务数据
- "This class holds the context needed for a browsing session."
- storage / cookie / cache / permission 的**归属主体**。同一 BC 下的 WC 共享存储；不同 BC 隔离。
- 类比：Chrome **Profile**（普通/隐身各一个）。WebView: `AwBrowserContext`。
- **为什么存在**：多用户/多模式隔离。没有它，所有网页共享一份 cookie/storage，无法实现隐身模式或多账号。

### 页面层

**`WebContents`** —— 一个网页的容器
- "WebContents is the core class in content/. A WebContents renders web content in a rectangular area."
- "WebContents will do all the multi-process stuff behind the scenes."
- 类比：Chrome **一个 Tab**。WebView: 一个 WebView 实例。
- **拥有** `NavigationController`（`GetController()`）。
- 生命周期约束：WebContents 必须在 BrowserContext **之前**销毁。
- **为什么存在**：它是 embedder 操作网页的唯一入口——加载 URL、前进后退、拿 NativeView 插入 UI、接收回调，全从这里走。它把多进程复杂性藏在身后。

### 导航层

**`NavigationController`** —— 导航控制器
- WebContents 的成员，通过 `GetController()` 获取。
- 提供 `LoadURL` / `LoadURLWithParams`、前进、后退。管理导航历史。
- **为什么存在**：把"导航"这个复杂操作（网络请求、重定向、安全检查、跨进程切换）封装成简单的 `LoadURL` 调用。

**`NavigationRequest`** —— 一次导航的完整生命周期（内部实现）
- "The primary implementation of NavigationHandle."
- 状态机：`STARTED → WILL_REDIRECT → RESPONSE_PROCESSING → WILL_COMMIT → COMMITTED`（或 `CANCELED` / `FAILED`）。
- 管理一次导航从发起到提交的全过程：网络请求、重定向、throttle、选择 RenderFrameHost、commit。
- **为什么存在**：导航是多阶段异步过程（可能重定向多次、安全检查挂起、需切换进程）。用状态机管理，通过 `NavigationHandle` 向 embedder 暴露关键节点。

### 渲染层

**`RenderFrameHost`** —— Browser 侧的 frame 代理
- "The interface provides a communication conduit with a frame in the renderer."
- "RenderFrameHost will always be in a different process."
- Browser 进程中代表 Renderer 进程里**一个 frame** 的对象，IPC 通信管道。
- **为什么存在**：Browser 不直接碰网页内容（Blink 在 Renderer），需要"代理"发命令、收回调。每个 frame 一个。

**`RenderProcessHost`** —— Renderer 进程的管理者
- 管理 Renderer 进程生命周期（启动、关闭、crash 恢复）。每个 Renderer 进程一个。
- **为什么存在**：Browser 要管 Renderer 进程生死，分配页面到进程（Site Isolation），监控 crash。

---

## 图 1：对象关系图（纵向归属 + 数量关系）

```
                    BrowserContext (会话上下文)                    [public/browser/]
                   ╔═════════════════════════════════╗
                   ║  storage / cookie / cache        ║
                   ║  permission / download / blob    ║   类比: Chrome Profile
                   ║  (同一 BC 下的 WC 共享存储)       ║   WebView: AwBrowserContext
                   ╚════════════════╤════════════════╝
                                    │ 拥有 (1 : N)
                    ┌───────────────┴───────────────┐
                    ▼                               ▼
              WebContents #1                  WebContents #2       [public/browser/]
              ╔══════════════╗                ╔══════════════╗
              ║  一个 Tab     ║                ║  一个 Tab     ║    类比: Chrome Tab
              ║  渲染网页内容  ║                ║              ║    WebView: 一个 WebView
              ║  藏多进程复杂度║                ║              ║
              ╚══════╤═══════╝                ╚══════════════╝
                     │ 拥有 (1 : 1)
                     ▼
            NavigationController                                   [public/browser/]
              ╔══════════════╗
              ║ LoadURL()     ║
              ║ 前进 / 后退    ║
              ║ 管导航历史     ║
              ╚══════╤═══════╝
                     │ 创建 (每次导航 1 个活跃)
                     ▼
              NavigationRequest                                     [browser/] ⚠️内部
              ╔══════════════════════════════╗
              ║  状态机:                      ║
              ║  STARTED                      ║
              ║    → WILL_REDIRECT            ║
              ║    → RESPONSE_PROCESSING      ║
              ║    → WILL_COMMIT              ║
              ║    → COMMITTED / CANCELED     ║
              ║  选择 RenderFrameHost          ║
              ║  (current vs speculative)     ║
              ╚══════════════╤═══════════════╝
                             │ 使用 / 选择
                             ▼
         ┌───────────────────┴───────────────────┐
         ▼                                       ▼
   RenderFrameHost                         RenderFrameHost      [public/browser/]
   (main frame)                            (iframe)
   ╔══════════════╗                        ╔══════════════╗
   ║ Browser 侧的  ║                        ║ Browser 侧的  ║
   ║ frame 代理    ║                        ║ frame 代理    ║
   ║ IPC 通信管道  ║                        ║ IPC 通信管道  ║
   ╚══════╤═══════╝                        ╚══════╤═══════╝
          │                                         │
          │  属于 (N : 1)                            │ 属于
          ▼                                         ▼
   RenderProcessHost #1                    RenderProcessHost #2  [public/browser/]
   ╔══════════════════╗                    ╔══════════════════╗
   ║ Renderer 进程管理 ║                    ║ Renderer 进程管理 ║
   ║ 启动/关闭/crash   ║                    ║ (不同 site)       ║
   ║ 恢复              ║                    ║                    ║
   ╚══════════════════╝                    ╚══════════════════╝
```

**数量关系速记**：

| 关系 | 数量 | 说明 |
|---|---|---|
| BrowserContext → WebContents | 1 : N | 一个 Profile 多个 Tab |
| WebContents → NavigationController | 1 : 1 | 一个 Tab 一个导航控制器 |
| NavigationController → NavigationRequest | 1 : 1(活跃) | 每次导航一个，串行 |
| WebContents → RenderFrameHost | 1 : N | 主 frame + iframe 各一个 |
| RenderFrameHost → RenderProcessHost | N : 1 | 一个进程跑多个 frame |
| RenderProcessHost → Renderer 进程 | 1 : 1 | 一个进程管理者对应一个进程 |

---

## 图 2：跨进程对应（content/ 最核心的架构）

```
   Browser 进程                              Renderer 进程
   ══════════════                            ══════════════

   RenderProcessHost #1   ←── Mojo/IPC ──→   Renderer 进程 #1
                                                 │
   RenderFrameHost        ←── Mojo/IPC ──→   RenderFrameImpl
   (main frame)              (per-frame)         (main frame)
                                                 │
   RenderFrameHost        ←── Mojo/IPC ──→   RenderFrameImpl
   (iframe)                                     (iframe)
                                                 │
                                                 ▼
                                              Blink
                                           (DOM/Layout/Paint)
```

**关键认知**：
- **RenderFrameHost（Browser）↔ RenderFrameImpl（Renderer）是一对一跨进程对应**。Browser 不直接碰 Blink，通过 RenderFrameHost 发 IPC 命令，RenderFrameImpl 在 Renderer 侧执行。
- **RenderProcessHost 是进程级管理者**，管生死；**RenderFrameHost 是 frame 级通信管道**，管具体 frame 的交互。一个 RenderProcessHost 下可以有多个 RenderFrameHost（同进程多 frame）。
- NavigationRequest 在 Browser 侧发起导航，最终让 Renderer 侧的 RenderFrameImpl 执行 commit（把响应写进 DOM）。

---

## 图 3：public/ vs browser/ vs renderer/ 分层

```
   embedder (Chrome / WebView / Electron / CEF)
       │
       │  只能调 public/ 的 API
       ▼
   ┌──────────────────────────────────────────────────┐
   │  content/public/browser/        ← 稳定 API 边界   │
   │                                                  │
   │    BrowserContext    WebContents                  │
   │    NavigationController  RenderFrameHost         │
   │    RenderProcessHost                             │
   │    (NavigationHandle ← NavigationRequest 的公开面)│
   └──────────────────────┬───────────────────────────┘
                          │ 内部实现，embedder 看不到
                          ▼
   ┌──────────────────────────────────────────────────┐
   │  content/browser/              ← 实现             │
   │                                                  │
   │    NavigationRequest (状态机)                     │
   │    FrameTree / FrameTreeNode                     │
   │    RenderFrameHostImpl (← RenderFrameHost 实现)  │
   │    RenderProcessHostImpl (← RenderProcessHost)   │
   │    WebContentsImpl (← WebContents 实现)          │
   └──────────────────────┬───────────────────────────┘
                          │ Mojo / IPC
                          ▼
   ┌──────────────────────────────────────────────────┐
   │  content/renderer/            ← Renderer 侧       │
   │                                                  │
   │    RenderFrameImpl (对应 RenderFrameHost)         │
   │    RenderViewImpl                                │
   └──────────────────────┬───────────────────────────┘
                          │
                          ▼
                       Blink
```

**为什么这样分**：
- `public/` 是**稳定契约**——embedder 依赖它，content 承诺不随意破坏。改 `browser/` 的实现不破坏 embedder。
- `NavigationRequest` 在 `browser/` 而非 `public/`——它的状态机是内部实现细节，embedder 只需通过 `NavigationHandle`（public）观察导航节点。
- 每个 `public/` 的抽象类在 `browser/` 有一个 `Impl`（`WebContentsImpl`、`RenderFrameHostImpl`、`RenderProcessHostImpl`）——这是 content 的 **Interface/Impl 模式**。

---

## 生命周期约束

```
构造顺序：
   WebContents构造时就必须传入BrowserContext，因此可以通过WebContents::GetBrowserContext拿到对象

销毁顺序（必须遵守）:

   WebContents.destroy()  ← 先死
        │
        ▼
   (WebContents 析构，释放 NavigationController / RenderFrameHost ...)
        │
        ▼
   BrowserContext 析构    ← 后死（storage/cookie 才能安全释放）
```

> 头文件原话（`web_contents.h:147`）："The owner of web_contents is responsible for ensuring that web_contents are destroyed *before* the corresponding browser_context is destroyed."

**为什么**：WebContents 运行中会访问 BrowserContext 的 storage/cookie。如果 BrowserContext 先死，WebContents 会访问已释放的资源 → UAF crash。

---


---

# Phase 2 — L2 设计层

> 四个「为什么」设计问题，每个附结论与易错点。

## 设计问题 1：为什么 WebContents 拥有 NavigationController，而不是反过来或平级？

**结论**：WebContents 是聚合体，NavigationController 是其中一个维度。拥有关系由两个维度决定：

1. **生命周期依附**：NC 的生死完全依附 WC——WC 创建时创建 NC，WC 销毁时销毁 NC。不存在没有 WC 的 NC。反过来 WC 可以短暂存在而没有活跃导航（刚创建还没 loadUrl）。**拥有者管被拥有者的生死**。
2. **功能包含**：WC 不只有导航，还有渲染、绘制、find、截图、打印……导航只是其一。如果反过来让 NC 拥有 WC，渲染/绘制等功能挂哪？**聚合体拥有部件，不是部件拥有聚合体**。

> 口诀：**谁的生命周期更长、谁聚合更多功能，谁就是拥有者。**

## 设计问题 2：为什么 RenderFrameHost 和 RenderProcessHost 分成两个类？

**结论**：frame 级 vs 进程级，分开由两个维度决定：

1. **数量关系（N:1）**：一个进程跑多个 frame。合并成一类 → 要么每个 frame 管一个进程（进程爆炸），要么每个进程只有一个 frame（违背 Site Isolation）。**N:1 天然要求两个类**。
2. **生命周期不同**：进程可能比某个 frame 活得久——frame 销毁但进程还在跑其他 frame。**不同生命周期的对象不能合并**。

> 口诀：**N:1 + 不同生命周期 = 必须分开。** RFH 是 frame 级通信管道（短命），RPH 是进程级管理者（长命）。

## 设计问题 3：为什么 NavigationRequest 用状态机，而不是顺序函数调用？

**结论**：导航是多阶段异步流程，状态机由三个维度决定：

1. **异步性**：每一步都是异步的（网络请求等响应、throttle 异步挂起、重定向重新请求）。顺序调用无法"暂停等回调"。状态机天然支持"在状态 X 等待，事件来了跳到状态 Y"。
2. **可观察**：每个状态转换点通知 embedder（通过 `NavigationHandle`），embedder 能在 `WILL_REDIRECT`、`WILL_COMMIT` 介入。
3. **可取消**：任何状态都能跳到 `CANCELED`/`FAILED`。顺序调用要中途取消得 unwind 整个调用栈，状态机只需切状态。

> 口诀：**异步 + 可观察 + 可取消 = 状态机。**

## 设计问题 4：为什么 public/ / browser/ / renderer/ 三层分离？

**结论**：两层分离，原因不同：

| 分离 | 真正原因 |
|---|---|
| public ↔ browser | **API 契约 vs 实现**：embedder 依赖 public（稳定），content 可自由重构 browser（可变）。Interface/Impl 分离。 |
| browser ↔ renderer | **进程隔离**：browser/ 跑在 Browser 进程，renderer/ 跑在 Renderer 进程。物理隔离，代码不共享，通过 Mojo/IPC 通信。 |
| common | 两边都能跑的共享代码（消息定义、常量等）。 |

> ⚠️ 易错点：browser/renderer 分离**不是**"概念不同"或"界面区分"，而是**跑在不同进程**。进程隔离是物理的。

---

## L2 易错点速记

1. **拥有关系**：生命周期更长 + 聚合更多功能的一方拥有另一方。WC 拥有 NC，不是反过来。
2. **RFH/RPH 分开**：N:1 数量关系 + 不同生命周期。不是"概念不同"。
3. **状态机**：异步 + 可观察 + 可取消。顺序调用搞不定"暂停等回调"和"中途取消"。
4. **三层分离**：public↔browser = API 契约分离；browser↔renderer = **进程隔离**（不是概念区分）。

---

# Phase 3 — L3 对象关系

## 创建链（谁创建谁）

```
embedder 调 WebContents::Create(CreateParams(browser_context))
  → WebContentsImpl::Create(params)                    (web_contents_impl.cc:759)
    → CreateWithOpener(params, opener_rfh)              (:762)
      → new WebContentsImpl(browser_context)            ← 第一步：裸构造
      → Init(params, ...)                                ← 第二步：延迟初始化
        → SiteInstanceImpl::Create(browser_context)      (:3821)
        → primary_frame_tree_.Init(site_instance, ...)   (:3837)  ← 创建 FrameTree
          → root_.render_manager()->InitRoot(...)         (frame_tree.cc:947)
          → controller().CreateInitialEntry()            (:966)  ← 创建初始导航历史
        → CreateWebContentsView(...)                      (:3845)  ← 平台视图
```

**关键认知**（和 AwContents 一样的两步构造模式）：
- 第一步 `new WebContentsImpl(browser_context)`：裸对象，只有 browser_context 指针。
- 第二步 `Init(params)`：创建 SiteInstance → FrameTree → 初始 NavigationEntry → 平台视图。
- **为什么两步**：构造时需要 `this` 指针传给子对象（FrameTree 需要 delegate=this），构造函数里 `this` 还没完全 ready，所以延迟到 Init。

## 持有关系图（FrameTree 结构——L3 核心）

```
WebContentsImpl (public API + 实现)
  ├─ owns → NavigationControllerImpl (1:1)
  ├─ owns → primary_frame_tree_ : FrameTree (1:1)
  │         │
  │         └─ root_ : FrameTreeNode (主 frame)
  │              │
  │              ├─ render_manager_ : RenderFrameHostManager
  │              │     ├─ render_frame_host_ : RenderFrameHostImpl (current)  ← 当前显示
  │              │     └─ speculative_render_frame_host_ : RenderFrameHostImpl (speculative) ← 投机预创建
  │              │
  │              ├─ navigation_request_ : NavigationRequest (当前活跃导航, 可空)
  │              │
  │              └─ children_ : vector<FrameTreeNode> (iframe 树)
  │                   └─ 每个 child 有自己的 render_manager_ + current/speculative RFH
  │
  ├─ owns → WebContentsView (平台视图: HWND/NSView/Android View)
  └─ weak ref → BrowserContext (不拥有，BC 由 embedder 管理)
```

**FrameTree 是树形结构**：
- 每个 WebContents 有一个 `primary_frame_tree_`（MPArch 下可能有多个，先不管）。
- `root_` 是主 frame 的 FrameTreeNode。
- 每个 FrameTreeNode 有 `children_`（iframe），形成树。
- **每个 FrameTreeNode 都有自己的 `RenderFrameHostManager`**，管理 current + speculative 两个 RFH。

**iframe 的 FrameTreeNode 谁创建**：
- 不是 `FrameTree::Init` 创建的（Init 只创建 root）。
- 页面加载过程中，Renderer 侧遇到 iframe → IPC `OnCreateChildFrame` → `FrameTree::AddChild` → `new FrameTreeNode`。
- 证据：`render_frame_host_impl.cc:4699` → `frame_tree.cc:416`。

## RenderFrameHost 的 current / speculative 切换（L3 重点）

```
RenderFrameHostManager (每个 FrameTreeNode 一个)
  │
  ├─ render_frame_host_ (current)
  │     ↑ 用户当前看到的页面
  │     ↑ 绑定到一个 RenderProcessHost
  │
  └─ speculative_render_frame_host_ (speculative, 可空)
        ↑ 导航时投机创建的新 RFH
        ↑ 可能绑到不同 RenderProcessHost（跨 site 时）
        ↑ commit 成功后 → 升格为 current，旧 current 被销毁
        ↑ commit 失败 → 丢弃 speculative，保留旧 current
```

**切换时机**（证据：`render_frame_host_manager.cc:1400-1469`）：
1. **导航开始**：`DidCreateNavigationRequest` 被调用，判断：
   - same-document / BFCache → **强制用 current RFH**，清理 speculative
   - 其他导航 → `GetFrameHostForNavigation` 决定，**跨 site 时创建 speculative RFH**
2. **Commit 成功**：speculative 升格为 current，旧 current 销毁（或进 BackForwardCache）。
3. **导航取消/失败**：speculative 丢弃，current 不变。

**为什么要有 speculative**：导航是异步的（网络请求要等），等响应回来再创建 RFH 太慢。提前创建 speculative RFH（甚至提前 warm up 进程），commit 时直接切换，用户无感。这就是 **Site Isolation 的 speculative render frame 机制**。

## 销毁链

```
WebContents 析构 (embedder 释放 unique_ptr)
  → ~WebContentsImpl()
    → primary_frame_tree_ 析构
      → root_ (FrameTreeNode) 析构
        → render_manager_ 析构
          → render_frame_host_ (current) 销毁
          → speculative_render_frame_host_ 销毁
        → children_ 递归析构
    → NavigationControllerImpl 析构
  → (BrowserContext 不在这里销毁，由 embedder 管)
```

**生命周期约束**（L1 已说，这里补充实现机制）：
- WebContents 析构时，FrameTree → FrameTreeNode → RFH 逐层析构。
- BrowserContext 由 embedder 管理（Chrome: ProfileManager；WebView: AwBrowserContextStore），WebContents 不拥有它。
- 约束：WebContents 必须先于 BrowserContext 销毁——否则 FrameTree 析构时访问已死的 BC 的 storage → UAF。

---

# Phase 4 — L4 执行流程

## 选定调用链：`loadUrl()` → Navigation → Commit → Renderer

这是 road.md 要求追的「Chromium 最经典的一条调用链」。

```
① embedder 调
   NavigationController::LoadURL(url, ...)
     │
     ▼
② NavigationControllerImpl::LoadURL                     (navigation_controller_impl.cc:1328)
     → LoadURLWithParams(params)                         (:1337 → :1340)
       → NavigateWithoutEntry(params)                    (:1379 → :3644)
         │
         ▼
③ 找 FrameTreeNode（默认 root = main frame）             (:3655)
   创建 NavigationEntry (pending entry)                  (:3681)
   CreateNavigationRequestFromLoadParams(node, params)   (:3771)
     → NavigationRequest::CreateBrowserInitiated(...)   (navigation_request.cc:998)
       ← 创建 NavigationRequest，状态 = STARTED
         │
         ▼
④ node->navigator().Navigate(std::move(request), ...)   (navigation_controller_impl.cc:3796)
   → Navigator::Navigate(request, ...)                  (navigator.cc:822)
     → 重复导航检测
     → frame_tree_node->TakeNavigationRequest(request)   (frame_tree_node.cc:578)
       → render_manager()->DidCreateNavigationRequest()  ← 可能创建 speculative RFH
       → DidStartLoading()
         │
         ▼
⑤ beforeunload 检查                                     (navigator.cc:936)
   ├─ 需要 beforeunload → DispatchBeforeUnload → (回调后) BeginNavigation
   └─ 不需要 → NavigationRequest::BeginNavigation()      (navigator.cc:942)
         │
         ▼
⑥ NavigationRequest::BeginNavigation()                  (navigation_request.cc:2136)
   → BeginNavigationImpl()                               (:2428)
     → 发起网络请求 (URLLoader / mojo)
     → NavigationThrottle 检查（安全策略、SafeBrowsing...）
         │
         ▼
⑦ OnStartChecksComplete()                                (navigation_request.cc:4961)
   ← 所有 throttle 通过
   ├─ 有重定向 → OnRedirectChecksComplete() (:5325) → 回到 ⑥ 发新请求
   └─ 无重定向 → 等响应
         │
         ▼
⑧ OnResponseStarted()                                    (navigation_request.cc:4066)
   ← 收到 HTTP 响应头
   → 选择 RenderFrameHost（current 或 speculative）
   → 准备 commit
         │
         ▼
⑨ CommitNavigation()                                     (navigation_request.cc:5897)
   → 准备 commit_params, response_head 等
   → GetRenderFrameHost()->CommitNavigation(...)         (:6267)  ← Mojo/IPC 通知 Renderer
         │
         ▼ (跨进程 IPC)
⑩ Renderer 进程
   → RenderFrameImpl::CommitNavigation()  (content/renderer/)
     → Blink 执行 commit
       → 创建/更新 Document
       → 解析 HTML → DOM → ...
         │
         ▼ (IPC 回调)
⑪ Browser 进程
   → RenderFrameHostImpl::OnDidCommitNavigation()
   → NavigationRequest 状态 → COMMITTED
   → speculative RFH 升格为 current
   → NavigationController 更新历史条目
   → 通知 embedder（WebContentsObserver::DidFinishNavigation）
```

## 状态机流转图

```
STARTED
  │ BeginNavigation → 发起网络请求
  ▼
OnStartChecksComplete (throttle 通过)
  │
  ├─ 重定向 ──→ OnRedirectChecksComplete ──→ 回到 STARTED（发新请求）
  │
  ▼ 无重定向
OnResponseStarted (收到响应头)
  │
  ▼ 选择 RFH + 准备 commit
CommitNavigation
  │ IPC → Renderer → Blink commit
  ▼
COMMITTED ← 最终状态

  任意状态可跳到:
  → CANCELED (throttle 取消 / 用户取消)
  → FAILED (网络错误 → CommitErrorPage)
```

## 调用链图（跨进程视角）

```
┌─ Browser 进程 ──────────────────────────────────────────────────┐
│                                                                │
│  NavigationController::LoadURL                                 │
│    ↓                                                           │
│  NavigateWithoutEntry                                           │
│    ↓                                                           │
│  NavigationRequest::CreateBrowserInitiated  [状态: STARTED]    │
│    ↓                                                           │
│  Navigator::Navigate                                           │
│    ↓                                                           │
│  FrameTreeNode::TakeNavigationRequest                          │
│    ↓                                                           │
│  RenderFrameHostManager::DidCreateNavigationRequest            │
│    ↓ (跨 site 时创建 speculative RFH)                           │
│  NavigationRequest::BeginNavigation  [发网络请求 + throttle]    │
│    ↓                                                           │
│  OnStartChecksComplete → OnResponseStarted  [收到响应]          │
│    ↓                                                           │
│  CommitNavigation  [选择 RFH, 准备 commit]                      │
│    ↓ IPC/Mojo                                                  │
│    │←──────────────────────────────────────────────────────────│
│    │                                                           │
│    ↓                                                           │
│  OnDidCommitNavigation  [状态: COMMITTED]                      │
│    ↓                                                           │
│  speculative → current (升格)                                   │
│  NavigationController 更新历史                                  │
│                                                                │
└────────────────────────────────────────────────────────────────┘
         │ IPC/Mojo                          ↑ IPC/Mojo
         ▼                                  │
┌─ Renderer 进程 ─────────────────────────────┐
│                                            │
│  RenderFrameImpl::CommitNavigation         │
│    ↓                                       │
│  Blink: 创建 Document → 解析 HTML → DOM    │
│    ↓                                       │
│  commit 完成 → 回调 Browser                 │
│                                            │
└────────────────────────────────────────────┘
```

**读图要点**：
- 整条链从 `NavigationController::LoadURL` 到 `COMMITTED`，跨越 Browser → Renderer 两个进程。
- `NavigationRequest` 的状态机驱动整个流程：每一步异步完成后推进到下一状态。
- `RenderFrameHostManager` 在导航开始时决定是否创建 speculative RFH（跨 site 需要新进程）。
- commit 在 Renderer 侧执行（Blink 写 DOM），完成后 IPC 回调 Browser，speculative 升格为 current。
- **embedder 全程通过 `NavigationHandle`（public API）观察节点**——不直接碰 `NavigationRequest`。

---

# Phase 5 — L5 实现入口（按需）

> L5 不用长期记忆，能找到就行。八个关键类的 `file:line` 速查。

## 实现入口速查表

### 1. BrowserContext（会话上下文）

| 入口 | file:line | 说明 |
|---|---|---|
| 公开 API | `public/browser/browser_context.h:108` | embedder 调的接口 |
| 实现 | `browser/browser_context_impl.cc` | 内部实现 |

### 2. WebContents（页面容器）

| 入口 | file:line | 说明 |
|---|---|---|
| 公开 API | `public/browser/web_contents.h:131` | "the core class in content/" |
| Create | `browser/web_contents/web_contents_impl.cc:754` | 构造入口 |
| Init | `browser/web_contents/web_contents_impl.cc:3780` | 延迟初始化（建 FrameTree） |
| FromFrameTreeNode | `browser/web_contents/web_contents_impl.cc:228` | 反查 |

### 3. NavigationController（导航控制器）

| 入口 | file:line | 说明 |
|---|---|---|
| 公开 API | `public/browser/navigation_controller.h:74` | embedder 调的接口 |
| LoadURL | `browser/renderer_host/navigation_controller_impl.cc:1328` | 入口 |
| LoadURLWithParams | `browser/renderer_host/navigation_controller_impl.cc:1340` | 带参数 |
| NavigateWithoutEntry | `browser/renderer_host/navigation_controller_impl.cc:3644` | 找 FrameTreeNode + 创建 NavigationRequest |

### 4. NavigationRequest（导航状态机）

| 入口 | file:line | 说明 |
|---|---|---|
| 声明 | `browser/renderer_host/navigation_request.h:104` | 内部实现（非 public） |
| CreateBrowserInitiated | `browser/renderer_host/navigation_request.cc:998` | 创建 |
| BeginNavigation | `browser/renderer_host/navigation_request.cc:2136` | 状态机启动 |
| **OnStartChecksComplete** | `browser/renderer_host/navigation_request.cc:4961` | throttle 通过 → **发起网络请求**（`NavigationURLLoader::Create`） |
| OnResponseStarted | `browser/renderer_host/navigation_request.cc:4066` | 收到响应头 |
| CommitNavigation | `browser/renderer_host/navigation_request.cc:5897` | 准备 commit |
| **→ IPC** | `browser/renderer_host/navigation_request.cc:6267` | `GetRenderFrameHost()->CommitNavigation(...)` |

### 5. RenderFrameHost（frame 代理）

| 入口 | file:line | 说明 |
|---|---|---|
| 公开 API | `public/browser/render_frame_host.h:144` | "communication conduit with a frame" |
| CommitNavigation | `browser/renderer_host/render_frame_host_impl.cc:11918` | 准备 commit 参数 |
| **→ Mojo IPC** | `browser/renderer_host/render_frame_host_impl.cc:15923` | `navigation_client->CommitNavigation(...)` ← 发给 Renderer |
| OnCreateChildFrame | `browser/renderer_host/render_frame_host_impl.cc:4699` | iframe 创建入口 |
| DidCommitNavigation | `browser/renderer_host/render_frame_host_impl.cc:5935` | Renderer 回调 commit 完成 |

### 6. RenderProcessHost（进程管理者）

| 入口 | file:line | 说明 |
|---|---|---|
| 公开 API | `public/browser/render_process_host.h:129` | embedder 调的接口 |
| 构造 | `browser/renderer_host/render_process_host_impl.cc:1448` | 创建进程管理者 |
| Init | `browser/renderer_host/render_process_host_impl.cc:1634` | 启动 Renderer 进程 |
| GetProcessHostForSiteInstance | `browser/renderer_host/render_process_host_impl.cc:4810` | 按 site 分配进程 |

### 7. NavigationURLLoader（网络请求层，补充）

| 入口 | file:line | 说明 |
|---|---|---|
| Create（分派） | `browser/loader/navigation_url_loader.cc` | 按 LoaderType 分派 |
| Impl 构造 | `browser/loader/navigation_url_loader_impl.cc:1712` | kRegular 路径 |
| Start | `browser/loader/navigation_url_loader_impl.cc:735` | 开始请求 |
| CreateThrottlingLoaderAndStart | `browser/loader/navigation_url_loader_impl.cc:1142` | Mojo → network service |

### 8. URLLoader（network service 内部，补充）

| 入口 | file:line | 说明 |
|---|---|---|
| CreateLoaderAndStart | `services/network/url_loader_factory.cc:158` | 创建 URLLoader |
| 构造 | `services/network/url_loader.cc:625` | 创建 `net::URLRequest` |
| **StartRequest** | `services/network/url_loader.cc:1378` | `url_request_->Start()` ← 真正发 HTTP |
| OnResponseStarted | `services/network/url_loader.cc:2100` | net 栈回调 → Mojo 转发 |

> **网络栈架构**：net 栈是独立的网络库，通过 `mojom::URLLoader` 统一抽象接口暴露给所有上层使用者（导航、子资源、ServiceWorker）。对上层是黑盒（进程隔离 + Mojo IPC），但有"观察窗"——interceptor/throttle/observer 让上层能在关键节点介入。URLLoader 桥接 `net::URLRequest`（底层网络栈）和 `mojom::URLLoaderClient`（上层 Mojo 回调）。

---

# Phase 7 — Bug 演练（掌握度测试）

## 六个 Bug 场景 × 第一步打开哪个文件

| 场景 | 第一步打开 | file:line | 理由 |
|---|---|---|---|
| 导航不发生 | `navigation_controller_impl.cc` | `:3644` NavigateWithoutEntry | LoadURL 执行点，看是否走到创建 NavigationRequest |
| 渲染进程 crash | `render_process_host_impl.cc` | `:1634` Init | 进程启动 + crash 恢复逻辑 |
| iframe 不显示 | `render_frame_host_impl.cc` | `:4699` OnCreateChildFrame | iframe FrameTreeNode 创建入口 |
| 跨 site 内容错乱 | `render_frame_host_manager.cc` | `:1400` DidCreateNavigationRequest | speculative RFH 决策点（找**决策者**不找代理） |
| Cookie 隔离异常 | `browser_context_impl.cc` | — | BC → StoragePartition 映射 |
| 网络请求没发出 | `navigation_request.cc` | `:4961` OnStartChecksComplete | throttle 通过后才发请求，看是否卡在 throttle/interceptor |

## Bug 演练口诀

```
导航不发生 → navigation_controller_impl.cc:3644
进程 crash → render_process_host_impl.cc:1634
iframe 不显示 → render_frame_host_impl.cc:4699
跨 site 错乱 → render_frame_host_manager.cc:1400（决策者，不是代理）
Cookie 隔离 → browser_context_impl.cc
请求没发出 → navigation_request.cc:4961（throttle/interceptor 卡住？）
```

## 掌握度结论

- 六个场景答对 5.5/6
- 唯一需记牢：跨 site 问题找 `RenderFrameHostManager`（决策者），不找 `RenderFrameHostImpl`（代理）——"找决策者不找执行者"思维模式
## 后续阶段（待推进）

- [x] **L2 设计层** ✅：为什么 WebContents 拥有 NavigationController 而不是反过来？为什么 RenderFrameHost 和 RenderProcessHost 要分开？为什么 NavigationRequest 用状态机？为什么 public/browser/renderer 三层分离？
- [x] **L3 对象关系** ✅：谁创建/持有/销毁？WebContents 的构造链？RenderFrameHost 的 current/speculative 切换？FrameTree 的结构？
- [x] **L4 执行流程** ✅：追一条 loadUrl() → Navigation → Commit → Renderer 的完整调用链。
- [x] **L5 实现入口** ✅：按需，指路 `file:line`。
- [x] **Bug 演练** ✅：「content/ 出 Bug 第一步打开哪个文件？」


---

# 补完：SiteInstance 体系 + FrameTree 深入（Navigation 部分补完）

> 第二阶段 · 第一部分补完 | 代码源：/Volumes/externelSSD/chromium/src（Chrome 146）

## SiteInstance 四层概念（146 版结构）

```
BrowsingInstance（浏览实例——"会话隔离边界"）
  │   一个 tab 及其 window.open 的子孙 tab 共享同一个 BrowsingInstance
  │   （opener 链内可互相访问 window.opener，必须同边界）
  ▼
SiteInstanceGroup（进程组——"一个组 = 一个 RenderProcessHost"）     ← 146 新层
  │   真正持有 RenderProcessHost 的一层
  │   进程 crash 时 group 销毁，SiteInstance 可活下来重新关联新 group
  ▼
SiteInstance（site 抽象——"哪些 URL 是一家人"）
  │   refcounted，生命周期比 frame 长（被 NavigationEntry 持有）
  ▼
SiteInfo（site 数据——site_url + process_lock_url + 属性集）
```

**为什么拆 Group 层**：身份（长命）与资源（短命）解耦。
- 进程 crash → Group 销毁，SiteInstance 完好 → NavigationEntry 依然有效
- 不拆的话：SiteInstance 跟进程死 → 历史记录失效；或攥着死进程引用 → 状态混乱
- 头文件证据：`site_instance_group.h:60-66` "SiteInstances in the group may outlive this... they will get a new SiteInstanceGroup the next time one is needed. Currently, each SiteInstanceGroup has exactly one SiteInstance."

## site URL 计算规则（Site Isolation 判定依据）

| URL 类型 | site URL | 例 |
|---|---|---|
| 普通 http/https | **scheme + eTLD+1** | `https://a.example.com` → `https://example.com` |
| WebUI / effective URL | 整个 effective URL | `chrome://settings` |
| `file://` | 整个 scheme 共享 | 所有 file:// 同 site |
| error page | 专用 error site URL | `SiteInfo::CreateForErrorPage` |
| sandboxed iframe | 可能 per-origin 隔离 | `IsolateSandboxedIframesGrouping::kPerOrigin` |

**site ≠ origin**：origin 精确到子域+端口；site 是 eTLD+1。`a.example.com` 与 `b.example.com` 不同 origin 但同 site。
**eTLD+1 例**：`example.co.uk` 中 `co.uk` 是公共后缀（eTLD），所以 eTLD+1 = `example.co.uk`。

**注意**：同 site 是**可以共享进程**的必要条件，不是充分条件——中间还有 `RequiresDedicatedProcess()` 闸门（强隔离列表里的 site 如 google.com 即使同 site 也各占进程）。

## 导航时的进程决策（衔接 speculative RFH）

```
NavigationRequest → GetFrameHostForNavigation
  → 计算 target URL 的 SiteInfo（site URL）
  → IsSameSite(当前 RFH 的 SiteInstance, target URL)?
      ├─ 同 site → 复用 current RFH（同进程）
      └─ 跨 site → 新 SiteInstance
           ├─ RequiresDedicatedProcess()? → 必须专属进程
           ├─ BrowsingInstance 内已有同 site Group → 复用进程
           └─ 没有 → 新 SiteInstanceGroup → 新 RenderProcessHost（或 spare 池）
                → 创建 speculative RFH（commit 成功后升格 current）
```

## FrameTree 深入

**头文件核心注释**（`frame_tree.h:38-52`）：
- "With the exception of the main frame, all FrameTreeNodes will be created/deleted in response to frame attach and detach events in the DOM."
- "**The main frame's FrameTreeNode is special in that it is reused.**"——跨顶级导航持久存在，作为需要保留状态的锚点
- 只在 UI 线程使用

**FrameTreeNode 复用的作用域 = 同一个 FrameTree 内 = 同一个 WebContents 内**：
- 同 tab 导航（site A → site B）：root FrameTreeNode 复用，只换下面的 RFH（speculative 升格）
- 跨 tab（window.open / 新 tab）：全新 WebContents → 全新 FrameTree → 全新 root FrameTreeNode，无复用概念
- 跨 tab 之间唯一可能共享的是**进程**（BrowsingInstance → 同 site Group），FrameTree 各自独立

**FrameTree 三种类型**（146 版）：`kPrimary`（主树）/ `kPrerender`（预渲染）/ `kFencedFrame`（fenced frame 树）

**FrameTree 职责**：持有 root FrameTreeNode（跨导航复用）、持有 NavigationController、管理 FrameTreeNode 生灭（响应 DOM attach/detach）、聚合 loading 状态、树类型标记。

## "两个 Frame 树"——Browser 控制面 vs Renderer 数据面

| | Browser 侧（控制面） | Renderer 侧（数据面） |
|---|---|---|
| 对象 | FrameTree / FrameTreeNode | Blink LocalFrame / RemoteFrame |
| 维护 | frame 层级、URL/origin/sandbox/进程归属/导航状态 | DOM 树、CSSOM、Layout、JS 堆 |
| 懂 HTML | ❌ 不解析、不持有内容 | ✅ HTMLParser → DOM 全在这 |

**Browser 不碰 HTML，甚至 body 字节流都不经过它**（转交模式：URLLoaderClientEndpoints 转交后，数据 network service → Renderer 直达）。

**但 Browser 必须维护骨架镜像的三个理由**：
1. 导航决策（每 frame 导航需 Browser 发起：安全检查、进程分配）
2. 安全权威（Browser 是安全边界，不能信任被攻破的 Renderer 的报告——自己维护权威 frame 结构 + process lock 校验）
3. Crash 恢复（Renderer 死了 DOM 全没，Browser 侧 FrameTree 还在，知道页面原本结构，可重建）

**记忆口诀**：Renderer 懂**内容**（DOM），Browser 懂**结构**（frame 骨架）。DOM 是内容树，FrameTree 是管理树。

## Check 验收记录（Sep 6）

- Group 层意义：✅ Group 随进程死、SiteInstance 活、新 Group 关联（补：不拆则 NavigationEntry 失效的反向论证）
- site URL 计算：✅ google.com / example.co.uk 全对（补：同 site 是必要条件，RequiresDedicatedProcess 是闸门）
- 主 frame 复用：✅ + 用户准确补充作用域——**仅同 tab（同 FrameTree）内成立，跨 tab 是全新 FrameTree**
