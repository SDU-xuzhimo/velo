# Browser Architecture（多进程架构）源码导读

> 第三站：理解 Chromium 的多进程架构。
> 代码源：`/Volumes/externelSSD/chromium/src/`（新版内核）
> 前置：android_webview（embedder 层）+ content/（内核层）

---

## 定位

**关键文件**：

| 文件 | 作用 |
|---|---|
| `content/public/common/process_type.h` | 进程类型枚举 |
| `content/browser/browser_main_loop.cc` | Browser 进程主循环 |
| `content/browser/child_process_launcher.cc` | 子进程启动器 |
| `content/browser/renderer_host/render_process_host_impl.cc` | Renderer 进程管理 |
| `content/browser/site_instance_impl.cc` | Site Isolation 决策 |
| `services/network/` | Network Service（跑在 Utility 进程里） |

**进程类型枚举**：

```cpp
enum ProcessType {
  PROCESS_TYPE_BROWSER,     // Browser 进程（主进程）
  PROCESS_TYPE_RENDERER,    // Renderer 进程（跑 Blink）
  PROCESS_TYPE_UTILITY,     // Utility 进程（各种服务）
  PROCESS_TYPE_GPU,         // GPU 进程
  PROCESS_TYPE_ZYGOTE,       // Zygote（Android/Linux 预热 fork 源）
  PROCESS_TYPE_SANDBOX_HELPER,
  // 注：Network Service 不在这里！它是 Utility 进程里的 Mojo 服务
};
```

---

# Phase 1 — L1 架构层

## 根本使命：为什么要多进程？

Chromium 把浏览器拆成多个进程，核心解决**三个问题**：

### 1. 安全隔离（最重要的原因）

网页内容是不可信的（可能含恶意 JS）。多进程 + 沙箱：
- Renderer 进程被沙箱化（无文件系统访问、无网络访问、受限系统调用）
- 即使被攻破也无法直接影响 Browser 进程或其他 Renderer
- Spectre/Meltdown 缓解：跨 site 数据不共享进程

### 2. 稳定性

一个 tab crash 不影响其他 tab。Renderer 进程挂了，Browser 进程检测到后重新启动一个新的 Renderer，用户只看到一次页面刷新。

### 3. 性能

多进程并行——一个 Renderer 的 GC/JS 执行不阻塞其他 tab 的 UI。每个进程有自己的 V8 实例、自己的内存空间。

---

## 五种进程各自的角色

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Browser 进程 (PROCESS_TYPE_BROWSER)                │
│                                                                     │
│  "主进程" — 唯一不可重启的进程，管理所有子进程                         │
│                                                                     │
│  跑在这里的对象（前两站学的）：                                       │
│    ├─ BrowserContext / BrowserContextImpl                            │
│    ├─ WebContents / WebContentsImpl                                  │
│    ├─ NavigationController / NavigationRequest                       │
│    ├─ RenderFrameHost / RenderFrameHostManager                        │
│    ├─ RenderProcessHost（管理 Renderer 进程的代理）                   │
│    ├─ BrowserMainLoop（主循环）                                       │
│    └─ UI 线程                                                        │
│                                                                     │
│  职责：管理 tab/窗口生命周期、管理导航、管理子进程、书签/历史/下载/扩展 │
└─────────────────────────────────────────────────────────────────────┘
        │                          │                        │
        │ Mojo IPC                 │ Mojo IPC               │ Mojo IPC
        ▼                          ▼                        ▼
┌───────────────┐    ┌───────────────┐           ┌───────────────────┐
│ Renderer 进程  │    │ Renderer 进程  │   ...     │ GPU 进程           │
│                │    │                │           │                   │
│ 跑 Blink:      │    │ 不同 site 的    │           │ 唯一，所有         │
│ ├─ RenderFrame │    │ 页面跑在不同   │           │ Renderer 共享      │
│ │   Impl       │    │ Renderer 进程  │           │                   │
│ ├─ V8          │    │ (Site          │           │ GPU 加速渲染:      │
│ ├─ Blink DOM   │    │  Isolation)    │           │ GL/Vulkan 命令    │
│ ├─ Layout      │    │                │           │ 合成最终帧         │
│ ├─ Paint       │    │ 沙箱化:        │           │                   │
│ └─ cc (合成)   │    │ 无文件/网络     │           │ 不沙箱（特权进程） │
│                │    │ 受限系统调用    │           │ GPU 驱动易 crash， │
│ 沙箱化:        │    │                │           │ 隔离避免影响主进程 │
│ 无文件/网络    │    │                │           └───────────────────┘
│ 受限系统调用   │    │                │
└───────────────┘    └───────────────┘
        │
        │ Mojo IPC
        ▼
┌─────────────────────────────────────────────────────────────────────┐
│              Network Service (跑在 Utility 进程里)                    │
│              (PROCESS_TYPE_UTILITY)                                  │
│                                                                     │
│  跑在这里的对象：                                                    │
│    ├─ URLLoaderFactory → URLLoader                                  │
│    ├─ net::URLRequest（DNS/TCP/TLS/HTTP）                            │
│    ├─ CookieManager / CacheManager / NetworkContext                  │
│                                                                     │
│  注：可以 in-process 或 out-of-process，生产默认 out-of-process      │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│              Utility 进程 (PROCESS_TYPE_UTILITY)                     │
│                                                                     │
│  "万能打工进程" — 跑各种 Mojo 服务的通用进程                           │
│    ├─ Network Service（网络）                                       │
│    ├─ Audio Service（音频）                                          │
│    ├─ Storage Service（存储）                                        │
│    └─ 其他 embedder 自定义服务                                       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 为什么 Site Isolation？

**Site Isolation = 不同 site 的页面跑在不同 Renderer 进程**。

```
没有 Site Isolation:                    有 Site Isolation:

┌─ Renderer 进程 #1 ──────────┐         ┌─ Renderer 进程 #1 ──────┐
│  tab A: google.com          │         │  tab A: google.com      │
│  tab B: evil.com            │         └─────────────────────────┘
│  iframe: bank.com           │
│  iframe: evil.com           │         ┌─ Renderer 进程 #2 ──────┐
│                             │         │  tab B: evil.com         │
│  ⚠️ evil.com 能通过 Spectre  │         └─────────────────────────┘
│     读 bank.com 的数据！     │
└─────────────────────────────┘         ┌─ Renderer 进程 #3 ──────┐
                                        │  iframe: bank.com       │
                                        └─────────────────────────┘
```

**核心 tradeoff（粒度平衡）**：
- 粒度过大 → 起不到数据隔离效果
- 粒度过小 → Renderer 进程数目膨胀，影响性能（内存开销）
- `SiteInstance`（`site_instance_impl.cc`）做粒度决策：同 site 共享进程，跨 site 强制新进程，有进程上限

---

## 为什么 Mojo？

Mojo 是 Chromium 的**进程间通信（IPC）系统**。所有跨进程通信都走 Mojo。

**双向抽象**：
- 对上：隐藏底层 IPC 细节（Linux: Unix domain socket；Android: binder；Windows: named pipe）
- 对下：隐藏上层调用方细节（Remote/Receiver 模式）

**为什么不用其他 IPC**：
- **类型安全**：Mojo 用 IDL（`.mojom` 文件）定义接口，自动生成 C++/Java/JS 绑定，编译时类型检查
- **异步**：默认异步（PostTask 模式），不阻塞调用方
- **跨平台**：同一套 API，底层自动适配
- **可扩展**：支持跨进程、同进程、跨线程

**和前两站的衔接**：
- 第一站：`AwContentsIoThreadClient` 在 IO 线程调 Java → 同进程 JNI，不是 Mojo
- 第二站：`NavigationURLLoaderImpl` → `URLLoaderFactory` → network service → **跨进程 Mojo**
- `RenderFrameHost::CommitNavigation` → `RenderFrameImpl` → **跨进程 Mojo**

---

## 衔接三站的完整架构图

```
┌─ Browser 进程 ──────────────────────────────────────────────────────┐
│                                                                     │
│  android_webview (embedder)     content/ (内核)                     │
│  ┌────────────────────┐        ┌──────────────────────────────┐    │
│  │ AwBrowserProcess    │        │ BrowserContext               │    │
│  │ AwContents          │──owns──│ WebContents                  │    │
│  │ AwSettings          │        │  ├─ NavigationController     │    │
│  │ AwContentsClient     │        │  │   └─ NavigationRequest    │    │
│  │ Bridge              │        │  └─ FrameTree                │    │
│  │ AwContentsIoThread  │        │      └─ FrameTreeNode         │    │
│  │ Client              │        │          └─ RFHManager       │    │
│  └────────────────────┘        │              ├─ current RFH   │    │
│                                │              └─ speculative   │    │
│                                │                  RFH          │    │
│                                │ RenderProcessHost (进程代理)   │    │
│                                └──────────────────────────────┘    │
│                                                                     │
└───────────┬───────────────────────────┬─────────────────────────────┘
            │                           │
            │ Mojo IPC                  │ Mojo IPC
            ▼                           ▼
┌─ Network Service ──────┐    ┌─ Renderer 进程 ──────────────────────┐
│ (Utility 进程)          │    │                                      │
│ URLLoaderFactory        │    │ RenderFrameImpl ←→ RenderFrameHost   │
│ URLLoader               │    │   (Mojo IPC 对应)                    │
│ net::URLRequest         │    │                                      │
│ Cookie / Cache          │    │ V8 / Blink (DOM/Layout/Paint)        │
└─────────────────────────┘    │                                      │
                               └──────────┬───────────────────────────┘
                                          │ Mojo IPC
                                          ▼
                               ┌─ GPU 进程 ─────────────────────────┐
                               │ GL/Vulkan 合成 → 输出屏幕           │
                               └────────────────────────────────────┘
```

---

---

# 深入：多进程架构在代码上如何形成

> 回答三个关键问题：代码怎么知道跑在哪个进程？Renderer 对象怎么被创建？单例怎么处理进程？

## 1. 单一二进制 + 启动分派表

**桌面 Chrome 是单一二进制**：`content/browser/` 和 `content/renderer/` 的代码**都链接进同一个 chrome 可执行文件**（`chrome/BUILD.gn` → `chrome_dll` → `content/public/app`，注释原话："support the browser/child process split"）。

**进程身份由启动参数决定**（`content/app/content_main_runner_impl.cc:553`）：

```cpp
// 进程类型 → main 函数的分派表
static const MainFunction kMainFunctions[] = {
  {switches::kGpuProcess,      GpuMain},
  {switches::kRendererProcess, RendererMain},
  {switches::kUtilityProcess,  UtilityMain},
};
```

```
同一个 chrome 二进制启动
  ├─ 无 --process-type 参数      → RunBrowserProcessMain → BrowserMainLoop（Browser 进程）
  ├─ --process-type=renderer    → RendererMain（Renderer 进程）
  ├─ --process-type=gpu         → GpuMain（GPU 进程）
  └─ --process-type=utility     → UtilityMain（Utility 进程）
```

**代码组织（BUILD.gn）决定"哪些文件属于哪组"，启动分派（--process-type）决定"执行哪条路径"**。两步配合，保证 browser 代码只在 Browser 进程执行、renderer 代码只在 Renderer 进程执行。日常判断"跑在哪"，看目录位置依然有效——因为组织和执行一一对应。

## 2. Renderer 进程的"被动召唤"模式

**Renderer 进程启动时是"空"的**——只有消息泵，没有任何业务对象：

```
--process-type=renderer → RendererMain()
  → RenderProcessImpl（ChildProcess 基础设施）
    → RenderThreadImpl（主线程 + Mojo 消息泵）
    
⚠️ 此时没有 RenderFrameImpl！只有空壳，等 Browser 发命令。
```

**RenderFrameImpl 是被 Mojo 消息"召唤"出来的**：

```
Browser 进程                                Renderer 进程
──────────                                ──────────
RenderFrameHostImpl 创建后
(render_frame_host_impl.cc:4475)
  → 构造 CreateFrameParams
  → GetAgentSchedulingGroup().CreateFrame(params)
     (:4595)  ← Mojo Remote 调用
  │
  │  Mojo IPC（跨进程）
  └──────────────────────────────────→    AgentSchedulingGroup::CreateFrame()
                                            (agent_scheduling_group.cc:379)
                                            ← Mojo Receiver 收到消息
                                            ↓
                                          RenderFrameImpl::CreateFrame(...)
                                            (render_frame_impl.cc:1685)
                                            ↓
                                          new RenderFrameImpl
                                          + 创建 Blink WebLocalFrame
```

**对象初始化链**（每一环只在 Renderer 进程存在）：

```
--process-type=renderer
  → RendererMain()
    → RenderProcessImpl → RenderThreadImpl
      → AgentSchedulingGroup (Mojo Receiver)
        → [等 Browser 的 CreateFrame 消息]
          → RenderFrameImpl
            → Blink WebLocalFrame → DOM → ...
```

**类自己不知道进程身份**——它只是被这条路径创建，而这条路径天然只在 Renderer 进程走。

## 3. Mojo Remote/Receiver 配对：跨进程对象的"公共接口"

```mojom
// content/common/agent_scheduling_group.mojom（接口定义，两进程共享）
interface AgentSchedulingGroup {
  CreateFrame(CreateFrameParams params);
};
```

- **Browser 侧**：拿到 **Remote**（代理，调方法 = 发 IPC）
- **Renderer 侧**：实现 **Receiver**（收 IPC = 执行本地方法）
- **同一份接口，两边各一半**：Browser 有"遥控器"，Renderer 有"接收器"

这就是"公共接口创建对象"的机制——`mojom::AgentSchedulingGroup::CreateFrame` 是跨进程的对象创建协议。

## 4. 单例的进程隔离机制

**单例天然进程隔离，不需要"决定"**。`base::Singleton<T>::GetInstance()` 用静态存储（进程地址空间内的 static 变量），每个进程地址空间独立：

```
Browser 进程地址空间                     Renderer 进程地址空间
────────────────────                    ────────────────────
static T* instance = 【实例 A】           static T* instance = 【实例 B】
ChildProcessSecurityPolicyImpl           （同一类的独立实例）
::GetInstance() 返回 A                   ::GetInstance() 返回 B
```

**单例"在哪个进程"取决于哪条代码路径调用它**：

| 单例 | 谁调用 | 存在于哪个进程 |
|---|---|---|
| `ChildProcessSecurityPolicyImpl` | 只有 Browser 代码路径 | 只在 Browser 进程（Renderer 里从未被调 → nullptr） |
| `RenderThreadImpl::current()` | 只有 Renderer 代码路径 | 只在 Renderer 进程 |
| `base::FeatureList` | 两边都调用 | **两进程各有独立实例**（各自解析 flags） |

**关键认知**：单例不是"决定"在哪个进程，而是**每个进程各自惰性创建自己的实例**。谁先调用 GetInstance()，就在谁的进程里创建。地址空间隔离保证互不干扰。

## 5. 易错点补充

1. **"代码物理上没链接进 Renderer"是错的**——单一二进制，代码都在，只是初始化路径不执行它。
2. **Renderer 对象是被动创建的**——不是进程启动时就有，而是 Browser 通过 Mojo 消息逐个"召唤"。
3. **单例进程隔离靠地址空间**——不靠任何显式的"进程注册"机制。

## 易错点速记

1. **GPU 进程隔离的原因**：GPU 驱动易 crash（不是"容易被杀"），隔离避免影响主进程。GPU 进程**不沙箱**（特权进程）。
2. **Network Service 不是独立进程类型**：它跑在 Utility 进程里，是 Mojo 服务。
3. **Site Isolation 粒度**：过大失去隔离效果，过小进程膨胀。`SiteInstance` 做平衡。
4. **Mojo 双向抽象**：对上隐藏底层 IPC，对下隐藏调用方细节。

---

## 第一阶段完成清单

- [x] 第一站：android_webview（embedder 层）→ `android_webview note.md` (572 行)
- [x] 第二站：content/（内核层）→ `content note.md` (704 行)
- [x] 第三站：Browser Architecture（进程层）→ `browser architecture note.md`
- [x] 脑子里已有：整个浏览器长什么样 ✅

> 第一阶段目标达成：知道 Chromium 每个模块在整个系统中的位置，建立了完整地图。
> 下一阶段（第二阶段）：深入核心 — Navigation / Mojo IPC / Renderer / Rendering Pipeline。
