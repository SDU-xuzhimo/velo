# android_webview 核心五类 源码导读

> 模块：`AwBrowserProcess` / `AwContents` / `AwContentsClientBridge` / `AwSettings` / `AwContentsIoThreadClient`
> 方法论：架构优先（Architecture First），按 L1→L5 逐层推进。

---

## 定位

- **根目录**：
  - Native 侧：`android_webview/browser/`
  - Java 侧：`android_webview/java/src/org/chromium/android_webview/`
- **入口文件**：

| 类 | Native 头文件 | Java 对等体 | Lifetime |
|---|---|---|---|
| `AwBrowserProcess` | `android_webview/browser/aw_browser_process.h:48` | `AwBrowserProcess.java` | **Singleton**（进程级） |
| `AwContents` | `android_webview/browser/aw_contents.h:60` | `AwContents.java` | **WebView**（每个 WebView 一个） |
| `AwContentsClientBridge` | `android_webview/browser/aw_contents_client_bridge.h:48` | `AwContentsClientBridge.java` | **WebView**（owned by AwContents） |
| `AwSettings` | `android_webview/browser/aw_settings.h:33` | `AwSettings.java` | **WebView** |
| `AwContentsIoThreadClient` | `android_webview/browser/aw_contents_io_thread_client.h:43` | `AwContentsIoThreadClient.java` | **短命 handle**（1:1 with WebContents） |

---

# Phase 1 — L1 架构层

## 根本使命：为什么会有 `android_webview` 这一层

Chromium 内核入口是 `content::WebContents`，但 `content` 层**是为 Chrome 浏览器设计的**，默认了一堆"浏览器环境假设"：有完整 `BrowserProcess`、有 Chrome UI 层处理弹窗/证书、配置直达 blink、网络栈在 IO 线程跑。

而 Android Framework 的 `android.webkit.WebView` API 是**固定不可改**的（App 调的是 Framework 接口），且 WebView 跑在**任意 App 进程**里，没有 Chrome 那套浏览器外壳。

`android_webview` 这一层存在的根本原因：**content 层不认识 Android、不认识 Java、不假设 App 进程；而 WebView API 又是 Java 的、跑在 App 进程里。中间必须有人翻译和填坑。**

这五个类，就是分别填 content 层五个"假设落空点"上的五块补丁。

## 架构链（粗粒度）

```
App → android.webkit.WebView → WebViewChromium(glue) → AwContents(Java)
    → AwContents(Native) → content::WebContents → NavigationController
    → NavigationRequest → Renderer → Blink
```

## 架构图 1：纵向链路（一个 WebView 从 App 到 Blink）

```
┌──────────────────────────────────────────────────────────┐
│  Android App 代码                                         │
└────────────────────────┬─────────────────────────────────┘
                         │ 调 API (loadUrl / setWebSettings ...)
                         ▼
┌──────────────────────────────────────────────────────────┐
│  android.webkit.WebView          ◄── Framework 接口(不可改)│
└────────────────────────┬─────────────────────────────────┘
                         │ 适配
                         ▼
┌──────────────────────────────────────────────────────────┐
│  WebViewChromium                 ◄── glue 层             │
│  (WebViewChromium.java:887 new AwContents)               │
└────────────────────────┬─────────────────────────────────┘
                         │ JNI 构造 (aw_contents.cc:457)
                         ▼
╔══════════════════════════════════════════════════════════╗
║                  android_webview 层                       ║
║                                                          ║
║   AwContents (Java) ──────JNI──────► AwContents (Native) ║
║                                        │                 ║
║                                        │ 拥有            ║
║                                        ▼                 ║
║                              ┌─────────────────────┐     ║
║                              │ content::WebContents│     ║
║                              └──────────┬──────────┘     ║
╚═════════════════════════════════════════╪════════════════╝
                                          │ IPC
                                          ▼
                              ┌─────────────────────┐
                              │   Renderer 进程      │
                              └──────────┬──────────┘
                                         ▼
                                    ┌────────┐
                                    │  Blink │
                                    └────────┘
```

**读图要点**：
- `android_webview` 夹在「Framework 接口」与「content 内核」之间，**不直接碰 Blink**（Blink 在 Renderer 进程）。
- `AwContents(Native)` **拥有** `WebContents`（Ownership 核心方向：AwContents → WebContents）。
- JNI 边界在 `AwContents(Java) ↔ AwContents(Native)`。

## 架构图 2：AwContents 周围的横向关系（三个通道 + 底座）

```
                        AwContents (Native)
                       ┌─────┬─────┬─────┐
                       │     │     │     │
              ┌────────┘     │     │     └────────┐
              │              │     │              │
              ▼              ▼     ▼              ▼
        ┌───────────┐  ┌─────────┐ ┌──────────┐ ┌────────────┐
        │AwContents │  │AwSettings│ │AwContents│ │content::   │
        │Client     │  │         │ │IoThread  │ │WebContents │
        │Bridge     │  │         │ │Client    │ │            │
        └─────┬─────┘  └────┬────┘ └────┬─────┘ └────────────┘
              │             │           │
   ╔══════════╪═════════════╪═══════════╪═══════════════════════╗
   ║  通道方向 │             │           │                       ║
   ║          ▼             ▼           ▼                       ║
   ║   ┌──────────┐  ┌──────────┐  ┌───────────┐               ║
   ║   │ 回调通道  │  │ 配置通道  │  │ IO拦截通道 │               ║
   ║   │Native→Java│  │Java→blink│  │IO线程→Java │               ║
   ║   └────┬─────┘  └────┬─────┘  └─────┬─────┘               ║
   ║        ▼             ▼              ▼                      ║
   ║   App 的         blink          shouldIntercept           ║
   ║  WebViewClient   WebPrefs       Request                   ║
   ║  (JS dialog/    (JS开关/        (每请求触发,              ║
   ║   证书/auth/     mixed...)      高频,不切线程)             ║
   ║   onReceivedError)                                      ║
   ╚═══════════════════════════════════════════════════════════╝

   ╔═══════════════════════════════════════════════════════════╗
   ║  AwBrowserProcess   ◄── 进程级 Singleton 底座              ║
   ║    ├ local_state (PrefService)                            ║
   ║    ├ SafeBrowsing (DB / UI / Allowlist Manager)           ║
   ║    ├ BrowserPolicyConnector                              ║
   ║    └ VisibilityMetricsLogger                             ║
   ║         ▲ GetInstance() 被上面所有人调用                    ║
   ║         (AwContentBrowserClient / AwBrowserContext /      ║
   ║          AwContents ...)                                  ║
   ╚═══════════════════════════════════════════════════════════╝
```

**读图要点**：
- `AwContents(Native)` 是**枢纽**，但只**拥有** `WebContents`；三个通道类是它绑定的 peer / observer：
  - `AwContentsClientBridge`：**owned by AwContents**，weak ref Java peer。
  - `AwSettings`：`WebContentsObserver`，通过 `FromWebContents` 取，per-WebView。
  - `AwContentsIoThreadClient`：**短命 handle**，1:1 with WebContents，通过 `Associate` 注册成 UserData。
- 三个通道的**方向是关键区分点**（回调 / 配置 / 拦截），这是 L1 必须记住的。
- `AwBrowserProcess` 在最底下**托着所有人**，**不属于任何 WebView**——进程级，和上面四个 per-WebView 的类是不同 scope。

## 缺口—填补对照表（L1 核心结论）

```
content 层的假设              WebView 的现实            填补的类              方向/范围
─────────────────────────  ─────────────────────  ──────────────────────  ─────────────
有完整 BrowserProcess   →   跑在 App 进程        →  AwBrowserProcess       进程级单例
WebContents 不识 Android →  WebView API 要能力   →  AwContents (Native)     包装+JNI，每 WebView
有 Chrome UI 层         →   事件要回 App          →  AwContentsClientBridge  Native→Java 回调
配置直达 blink          →   配置在 Java           →  AwSettings              Java→blink 配置
网络栈在 IO 线程        →   拦截要回 Java(高频)   →  AwContentsIoThreadClient IO线程 Native→Java
```

**记住这张表，就记住了这五个类为什么存在。**

## 五个类速查

| 类 | 角色（一句话） | Lifetime | 关键关系 |
|---|---|---|---|
| `AwBrowserProcess` | content 层 `BrowserProcess` 的**瘦身替身**，提供 WebView 需要的全局服务子集 | 进程级 Singleton | `GetInstance()` 被所有人调 |
| `AwContents` (Native) | Java AwContents 的 Native 对等体，**拥有 WebContents**，所有功能汇聚点 | per-WebView | 拥有 WebContents；`WebContentsObserver` + `UserData` 双身份 |
| `AwContentsClientBridge` | 把 content 的「需 App 决策的 UI 事件」回调到 Java | per-WebView，owned by AwContents | weak ref Java peer；`Associate` 成 WebContents UserData |
| `AwSettings` | 把 Java `WebSettings` **映射**到 `blink::web_pref::WebPreferences` | per-WebView | `WebContentsObserver`；通过 `AwRenderViewHostExt` 推给 renderer |
| `AwContentsIoThreadClient` | IO 线程上**直接调 Java**的短命句柄，服务 `shouldInterceptRequest` / `CacheMode` | 短命 handle，1:1 with WebContents | `Associate` 成 WebContents UserData；hold Global ref 到 Java |

> ⚠️ **回调链路区分**（易混点）：
> - `AwContentsClientBridge` 专管「**需 App 决策**」：JS dialog、证书错误、客户端证书、HTTP auth、`onReceivedError`。
> - **页面状态回调**（`onPageStarted` / `onPageFinished`）走 `AwWebContentsObserver`（`WebContentsObserver` 链路），**不走** `AwContentsClientBridge`。

---

## L1 验收：`loadUrl` 调用流程

```
App webView.loadUrl(url)
  → android.webkit.WebView.loadUrl          (Framework, 不可改)
  → WebViewChromium.loadUrl                 (glue 层)
  → AwContents.loadUrl                      (Java, AwContents.java:2291)
  → mNavigationController.loadUrl(params)   (AwContents.java:2564, Java NavigationController)
  → [JNI/IPC] content::NavigationController::LoadURLWithParams   (content 层 native)
  → NavigationRequest → Renderer → Blink
```

**五个类在 loadUrl 全过程的介入时机**：

| 类 | 是否在正向链路 | 介入时机 |
|---|---|---|
| `AwContents` (Java) | ✅ 正向入口 | `AwContents.java:2291` 处理 loadUrl，调 NavigationController |
| `AwContents` (Native) | ⚠️ 不直接参与调用，但在链路周围 | 作为 WebContents 拥有者 + `WebContentsObserver`，接收导航回调（`DidStartNavigation` 等经 `AwWebContentsObserver` 转发） |
| `AwContentsIoThreadClient` | ✅ 网络阶段介入 | IO 线程发起 `URLRequest` 时，问 `shouldInterceptRequest` / 拿 `CacheMode` |
| `AwSettings` | ⚠️ 不在正向链路 | 导航开始时同步 `WebPreferences` 给 renderer，确保新页面用对的配置 |
| `AwContentsClientBridge` | ❌ 仅异常时介入 | 出错时（`onReceivedError` 由 `aw_proxying_url_loader_factory` 触发）或需 App 决策时 |
| `AwBrowserProcess` | ⚠️ 后台全局服务 | SafeBrowsing 检查通过它拿 UI/DB manager；全程提供 prefs/policy |

**关键认知**：loadUrl 这条路径上，**只有 `AwContents(Java)` 是正向必经**，`AwContentsIoThreadClient` 在网络阶段介入；其余三个类（`AwBrowserProcess` / `AwSettings` / `AwContentsClientBridge`）分别在「全局服务」「配置生效」「错误回调」时才登场。**`AwContents(Native)` 不直接参与 loadUrl 调用**（Java 侧直接调 NavigationController），但作为拥有者和 Observer 在链路周围。

---

## 500 字总结

`android_webview` 是 Android Framework 的 `android.webkit.WebView` API 与 Chromium `content` 内核之间的桥接层。它存在的根本原因：content 层为 Chrome 浏览器设计，假设了完整 `BrowserProcess`、Chrome UI 层、配置直达 blink、IO 线程网络栈；而 WebView 跑在 App 进程、API 在 Java、无浏览器外壳。中间必须有人翻译填坑。

五个类分别填五个坑。`AwBrowserProcess` 是进程级 Singleton，作为 content `BrowserProcess` 的**瘦身替身**，提供 local_state prefs、SafeBrowsing、Policy 等全局服务——它不属于任何 WebView。`AwContents`(Native) 是每个 WebView 的核心，**拥有** `content::WebContents`，以 `WebContentsObserver` + `UserData` 双身份挂在 WebContents 上，全代码库通过 `FromWebContents` 反查它；它负责生命周期、绘制对接、WebView API 能力封装和 JNI 桥接。`AwContentsClientBridge` owned by AwContents，专管「需 App 决策的 UI 事件」回调（JS dialog、证书错误、HTTP auth、`onReceivedError`），方向 Native→Java；注意页面状态回调（`onPageStarted`）走 `AwWebContentsObserver`，不走这里。`AwSettings` 把 Java `WebSettings` **映射**到 `blink::web_pref::WebPreferences`，跨 JNI+IPC 同步给 renderer，不是纯透传。`AwContentsIoThreadClient` 是 IO 线程上**直接调 Java**的短命句柄，服务 `shouldInterceptRequest` 和 `CacheMode`，避免高频路径切 UI 线程。

`WebView.loadUrl()` 路径：App → Framework WebView → WebViewChromium → `AwContents.loadUrl`(Java) → `mNavigationController.loadUrl` → content NavigationController → Renderer → Blink。正向链路只有 `AwContents(Java)` 必经，`AwContentsIoThreadClient` 在网络阶段介入，其余三类分别在全局服务、配置生效、错误回调时登场。

---


---

# Phase 2 — L2 设计层

> 四个「为什么」设计问题，每个附结论与易错点。

## 设计问题 1：为什么 AwContents 拥有 WebContents，而不是反过来？

**结论**：所有权方向 = 依赖方向。`WebContents` 是平台无关的内核门户，`content` 层（下层）不该认识 `android_webview`（上层）。让 `AwContents` 拥有 `WebContents`，保证 content 层对 android_webview 一无所知。

**销毁顺序**（⚠️ 易错点）：拥有者先析构，被拥有者后析构。
- `AwContents` 先析构（`~AwContents()` 函数体执行，清理 observers / 绘制等）
- `web_contents_` 成员后析构 → `WebContents` 被释放
- 即：AwContents 死时**带走**它拥有的 WebContents，**不是**"从 WebContents 开始析构"。

**证据**：`aw_contents.h:68` 构造函数 `AwContents(std::unique_ptr<content::WebContents>)`，`unique_ptr` 传参 = 所有权转移给 AwContents。

## 设计问题 2：为什么 AwContentsIoThreadClient 是短命 handle，而不是常驻？

**结论**：常驻 native handle = 持有 Global ref 到 Java 对象 = Java 对象无法被 GC = **绑架 WebView 生命周期**。

短命 handle 工作模式：「**按需获取 + 用完即弃**」
- IO 线程要调 Java 时，通过静态 getter（`FromWebContents` / 按 frame token）**新建** handle
- 调完立刻析构，Global ref 立刻释放
- 既满足"IO 线程直接调 Java"，又不阻止 WebView 被 GC

**证据**：`aw_contents_io_thread_client.h:53` 注释——"By keeping the native AwContentsIoThreadClient instance alive you're also prolonging the lifetime of the Java instance, so don't keep one if you don't need to."

## 设计问题 3：为什么回调分两条链路（AwContentsClientBridge vs AwWebContentsObserver）？

**核心区分维度**：**内核是否需要等待 Java 的决策**

| | AwContentsClientBridge | AwWebContentsObserver |
|---|---|---|
| 性质 | **阻塞型**，需等 App 决策 | **通知型**，发出即完 |
| 例子 | JS alert 等 App 返回"用户点了确定" | onPageStarted 不需 App 回答 |
| 接口 | 有回执/回调 | 单向通知 |

**补充维度**：来源层不同。Bridge 触发自网络栈/安全模块（`aw_proxying_url_loader_factory`、`aw_url_checker_delegate_impl`），Observer 触发自 content 导航事件。

**结论**：两类回调本质不同（需决策 vs 纯通知），合并会导致接口设计混乱——同一类既要有回执又要无回执。

## 设计问题 4：为什么 AwBrowserProcess 是单例，而不是 per-WebView？

**两个决定维度**：

1. **资源属性**：SafeBrowsing DB、Policy、`local_state` prefs 天然是进程级共享。一个 App 有多个 WebView，该共享一份，而非各存一份。
2. **content 层 API 假设**：content 代码到处 `BrowserProcess::Get()->local_state()`，`Get()` 假设返回**进程唯一实例**。per-WebView 会让 `Get()` 不知道返回哪个——content 层不知道"多个 WebView"的存在。

**结论**：单例 = 资源属性（该共享）+ content API 假设（必须唯一）共同决定，**缺一不可**。

---

## L2 易错点速记

1. **销毁顺序**：拥有者（AwContents）先析构，带走被拥有者（WebContents）。不是从内核开始析构。
2. **短命 handle 的本质**：避免 Global ref 阻止 Java GC。按需获取、用完即弃。
3. **两条链路的分界**：是否需要等待 Java 决策（阻塞 vs 通知）。
4. **单例的两个维度**：资源该共享 + content API 要求唯一。只说"资源是进程级"不够，还要说 content 代码写死了 `Get()`。

---

# Phase 3 — L3 对象关系

## 创建链（谁创建谁）

```
Java AwContents.<init>
  → JNI_AwContents_Init                    (aw_contents.cc:455)
    → content::WebContents::Create(...)     ← 先建 WebContents
    → new AwContents(std::move(web_contents))  ← AwContents 拥有 WebContents
    → 返回 native 指针给 Java（此时是"未初始化"实例）
  → SetJavaPeers                           (aw_contents.cc:318)  ← 延迟初始化汇合点
    → java_ref_ = weak ref(Java AwContents)
    → make_unique<AwWebContentsDelegate>    ← AwContents 拥有 delegate
    → make_unique<AwContentsClientBridge>   ← AwContents 拥有 bridge
    → AwContentsClientBridge::Associate(WebContents, bridge)   ← 注册 UserData
    → AwContentsIoThreadClient::Associate(WebContents, java_io_client)  ← 注册 Java 对象
    → InterceptNavigationDelegate::Associate(...)

AwSettings：独立 JNI 创建
  → JNI_AwSettings_Init                     (aw_settings.cc:840)
    → new AwSettings(env, obj, web_contents)

AwBrowserProcess：进程启动时创建
  → AwBrowserMainParts::PreMainMessageLoopRun  (aw_browser_main_parts.cc:160)
```

**关键认知**：
- `AwContents` 的构造分**两步**：`Init` 建对象（拥有 WebContents），`SetJavaPeers` 绑定 Java peer + 创建通道类。注释原话："Return an 'uninitialized' instance; most work is deferred until the subsequent SetJavaPeers() call."
- `AwContentsClientBridge` / `AwWebContentsDelegate` 都是 **AwContents 在 SetJavaPeers 里 `make_unique` 创建并拥有**。
- `AwSettings` 走**独立 JNI 入口**，不是 AwContents 创建的。
- `AwContentsIoThreadClient::Associate` **不创建 native 实例**，只把 Java 对象存成 WebContents UserData。

## 持有关系图

```
AwContents (Native)                         ← per-WebView 核心
  ├─ owns (unique_ptr) → WebContents        ← L2 讨论的所有权核心
  ├─ owns (unique_ptr) → AwWebContentsDelegate
  ├─ owns (unique_ptr) → AwContentsClientBridge
  ├─ owns (unique_ptr) → AwRenderViewHostExt
  ├─ owns → FindHelper, IconHelper, PermissionRequestHandler, ...
  ├─ weak ref → Java AwContents (java_ref_)
  └─ 双身份挂在 WebContents：WebContentsObserver + UserData

AwContentsClientBridge                      ← per-WebView，回调通道
  ├─ owned by AwContents
  ├─ weak ref → Java AwContentsClientBridge
  └─ WebContents UserData（via Associate）

AwSettings                                  ← per-WebView，配置通道
  ├─ created by JNI（Java 驱动，非 AwContents 创建）
  ├─ WebContents UserData（via FromWebContents）
  └─ WebContentsObserver

AwContentsIoThreadClient                    ← 短命 handle，IO 拦截通道
  ├─ 无常驻 native 实例！
  ├─ Associate 只把 Java 对象存成 WebContents UserData
  └─ native 实例按需 new，用完即弃（L2 已讨论）

AwBrowserProcess                            ← 进程级 Singleton
  ├─ 被 AwContentBrowserClient / AwBrowserContext / AwContents 等通过 GetInstance() 拿
  └─ 不持有任何 WebView，反过来被所有人共享
```

## 销毁链（谁销毁谁，顺序）

```
Java AwContents.destroy()
  → AwContents::Destroy                     (aw_contents.cc:445)
    → java_ref_.reset()                     ← 先断开 Java ref
    → delete this                           ← 触发析构
      → ~AwContents()                       (aw_contents.cc:372)
        ① RemoveUserData(kAwContentsUserDataKey)   ← 摘掉自己的 UserData
        ② AwContentsClientBridge::Dissociate       ← 摘掉 bridge 的 UserData
        ③ 清理 FindHelper / IconHelper listeners
        ④ visibility_metrics_logger->RemoveClient(this)
        ⑤ WebContentsObserver::Observe(nullptr)
        ⑥ 成员逆序析构：
           - contents_client_bridge_ 析构   ← bridge 销毁
           - web_contents_delegate_ 析构    ← delegate 销毁
           - web_contents_ 析构             ← WebContents 销毁（拥有者带走被拥有者）
```

**关键认知**（呼应 L2）：
- 销毁是 **Java 驱动**的：Java 调 `destroy()` → native `delete this`。
- 析构函数体先清理（摘 UserData、断 observer），**然后**成员才析构。
- `web_contents_` 作为成员最后析构——**AwContents 先死，带走 WebContents**。这正是 L2 说的"拥有者先析构"。
- 最后一个 WebView 销毁时（`instance_count == 0`），触发 `MemoryPressureListener` 通知——因为 App 进程可能继续跑很久不再用 WebView，该释放 Chromium 占的内存。

## Popup Rebinding（L3 最复杂场景）

```
正常流程：
  Java AwContents → JNI_Init → new AwContents#1(默认) → SetJavaPeers(绑定 Java ↔ #1)

Popup 流程：
  1. WebView 要开 popup 新窗口
  2. AwContents#1.SetPendingWebContentsForPopup(pending_web_contents)  (aw_contents.cc:1212)
     → pending_contents_ = make_unique<AwContents#2>(pending)   ← 创建第二个 native 实例
  3. Java AwContents 丢弃 #1（构造时建的"默认"实例）
  4. Java AwContents.SetJavaPeers 绑定到 #2（携带 popup 内容的实例）
```

**关键规则**（头文件注释 `aw_contents.h:52-58` 原话）：
- "the java instance that hosts a popup will be **rebound** to a second native instance (carrying the popup content) and **discard** the 'default' native instance it made on construction"
- "A native instance is only bound to **at most one Java peer** over its entire lifetime"
- 构造点：`Init()` 和 `SetPendingWebContentsForPopup()`
- 汇合点：`SetJavaPeers()`

**理解要点**：方向是 **Java 实例换 native 实例**，不是 native 换 Java。一个 Java AwContents 可能先后绑定两个 native 实例（默认 #1 → popup #2），但一个 native 实例一生只认一个 Java peer。

---

# Phase 4 — L4 执行流程

## 选定调用链：`shouldInterceptRequest` 端到端

选这条链的原因：它贯穿 **IO 线程 → 后台线程 → Java → 回 IO 线程**，最能体现 `AwContentsIoThreadClient` 的桥接价值，且是 WebView 特有的高频路径。

```
1. [IO 线程]  content 网络栈发起 URLRequest
2. [IO 线程]  AwProxyingURLLoaderFactory 拦截请求
              (aw_proxying_url_loader_factory.cc)
3. [IO 线程]  按 frame token 取 AwContentsIoThreadClient（短命 handle）
4. [IO 线程]  io_thread_client->ShouldInterceptRequestAsync(request, callback)
              (aw_contents_io_thread_client.cc:583)
5. [IO 线程]  获取 bg_thread_client_object_，PostTask 到 sequenced_task_runner_
6. [后台线程] StartShouldInterceptRequest
              (aw_contents_io_thread_client.cc:498)
              ├─ request 转 Java 格式（AwWebResourceRequest::ConvertToJava）
              └─ Java_AwContentsBackgroundThreadClient_shouldInterceptRequestFromNative  ← JNI 调 Java
7. [Java 后台线程] App 的 shouldInterceptRequest 执行，返回 WebResourceResponse（或 null）
8. [Java]     调回 JNI_AwContentsIoThreadClient_FinishShouldInterceptRequest
              (aw_contents_io_thread_client.cc:532)
              ├─ 按 request_id 取出 callback
              ├─ 构造 InterceptResponseData（含 InputStream）
              └─ PostTask 回 IO 线程
9. [IO 线程]  OnShouldInterceptRequestAsyncResult
              (aw_proxying_url_loader_factory.cc:523)
10.[IO 线程]  根据 response 分叉：
              ├─ 有响应 → 构造 InputStream 给网络栈（请求被拦截）
              └─ 无响应 → 继续原网络请求（不拦截）
```

## ⚠️ 关键发现：修正 L1 的"IO 线程直接调 Java"

L1 说 `AwContentsIoThreadClient` 让"IO 线程**直接**调 Java"。追完调用链发现这个说法**不够准确**：

- `shouldInterceptRequest`（重操作）：IO 线程 → **后台线程** → Java → 回 IO 线程。**不直接在 IO 线程调 Java**，因为 App 的拦截逻辑可能耗时，不能阻塞 IO 线程。
- `GetCacheMode`（轻操作）：这类才是 IO 线程直接调 Java。

**准确表述**：`AwContentsIoThreadClient` 的设计目标是"让 IO 线程能拿到与 WebContents 1:1 关联的 Java 句柄"，但**具体是直接调还是 post 到后台线程，取决于操作轻重**。短命 handle 解决的是"怎么拿到 Java 句柄"，线程策略是另一个维度。

## 调用链图

```
┌─────────────────────────────────────────────────────────────┐
│ IO 线程                                                      │
│  URLRequest                                                  │
│    ↓                                                         │
│  AwProxyingURLLoaderFactory 拦截                              │
│    ↓                                                         │
│  io_thread_client->ShouldInterceptRequestAsync  ──①──┐      │
│    ↓ PostTask                                         │      │
│    │              ┌───────────────────────────────────┘      │
│    │              ↓                                         │
│    │      ┌──────────────────────────┐                      │
│    │      │ 后台线程 (sequenced)      │                      │
│    │      │  StartShouldIntercept     │                      │
│    │      │    Request                │                      │
│    │      │      ↓ JNI                │                      │
│    │      │  shouldInterceptRequest   │                      │
│    │      │  FromNative ──②──┐        │                      │
│    │      └───────────────────┘       │                      │
│    │                                ┌────────────────────┐   │
│    │                                │ Java 后台线程       │   │
│    │                                │  App 拦截逻辑       │   │
│    │                                │  返回 Response      │   │
│    │                                │      ↓              │   │
│    │                                │ FinishShouldIntercept│   │
│    │                                │  Request ──③──┐     │   │
│    │                                └────────────────┘    │   │
│    │                                              ┌───────┘   │
│    ↓ ① PostTask 回 IO                              │          │
│  OnShouldInterceptRequestAsyncResult ◄─────────────┘          │
│    ↓                                                         │
│  有响应 → InputStream 给网络栈 / 无响应 → 继续原请求            │
└─────────────────────────────────────────────────────────────┘
```

**读图要点**：
- ①②③ 三次线程跳转：IO → 后台 → Java → 回 IO。
- `AwContentsIoThreadClient` 是**横跨三线程的桥梁**，但 native handle 本身是短命的（每次取一个新的）。
- 回调机制用 `request_id` + callback map（`AddCallback` / `LookupAndRemove`）实现异步关联。

## L3+L4 待验证点

1. **L3 验证**：`AwContents::Destroy` 里 `delete this` 后，`web_contents_` 是在析构函数体里手动释放，还是作为成员自动析构？（提示：看 `~AwContents` 有没有显式 `web_contents_.reset()`，如果没有，就是成员自动析构——印证 L2 的"拥有者先析构带走成员"）
2. **L4 验证**：`GetCacheMode` 是不是真的在 IO 线程直接调 Java，不走后台线程？（提示：看 `aw_contents_io_thread_client.cc` 里 `GetCacheMode` 的实现）
3. **Popup 验证**：`SetPendingWebContentsForPopup` 创建的 `pending_contents_`，后来是怎么"移交"给 Java 侧的？（提示：搜 `pending_contents_` 的读取点，看它怎么被 release 出去）

---

# Phase 5 — L5 实现入口（按需）

> L5 不用长期记忆，能找到就行。每个类给关键 `file:line` +「为什么这么写」点拨。

## 五个类的实现入口速查

### 1. AwBrowserProcess（进程级单例）

| 入口 | file:line | 说明 |
|---|---|---|
| `GetInstance` | `aw_browser_process.cc:127` | 单例获取，全代码库入口 |
| 构造函数 | `aw_browser_process.cc:131` | 初始化全局服务（prefs/safe browsing/policy） |
| 创建时机 | `aw_browser_main_parts.cc:160` | `PreMainMessageLoopRun` 时创建 |

**为什么这么写**：单例 + content API 假设（L2）。`GetInstance()` 返回进程唯一实例，满足 content 代码 `BrowserProcess::Get()` 的契约。

### 2. AwContents（per-WebView 核心）

| 入口 | file:line | 说明 |
|---|---|---|
| `JNI_AwContents_Init` | `aw_contents.cc:455` | 构造入口，建 WebContents + new AwContents |
| `SetJavaPeers` | `aw_contents.cc:318` | 延迟初始化汇合点，绑定 Java peer + 创建通道类 |
| `FromWebContents` | `aw_contents.cc:213` | UserData 反查（全代码库 20+ 处调用） |
| `~AwContents` | `aw_contents.cc:372` | 析构，拥有者先死带走成员 |
| `Destroy` | `aw_contents.cc:445` | Java 驱动销毁，`delete this` |
| `SetPendingWebContentsForPopup` | `aw_contents.cc:1212` | popup 场景，创建第二个 native 实例 |

**为什么这么写**：两步构造（Init + SetJavaPeers）——构造时 Java peer 还没准备好，延迟到 SetJavaPeers 才绑定。`FromWebContents` 用 UserData 模式，让 content 层代码能反查到 AwContents 而不直接依赖它。

### 3. AwContentsClientBridge（回调通道）

| 入口 | file:line | 说明 |
|---|---|---|
| `Associate` | `aw_contents_client_bridge.cc:87` | 注册成 WebContents UserData |
| `Dissociate` | `aw_contents_client_bridge.cc:94` | 析构时摘除 UserData |
| `OnReceivedError` | `aw_contents_client_bridge.cc:449` | 网络错误回调，转发到 Java |

**为什么这么写**：`Associate`/`Dissociate` 用 UserData 模式挂到 WebContents，这样网络栈/安全模块能通过 `FromWebContents` 拿到 bridge，而不需要直接持有 AwContents。

### 4. AwSettings（配置通道）

| 入口 | file:line | 说明 |
|---|---|---|
| `PopulateFixedWebPreferences` | `aw_settings.cc:75` | WebView 固定配置（viewport/shrink 等） |
| `UpdateWebkitPreferencesLocked` | `aw_settings.cc:304` | 配置变更，调 `OnWebPreferencesChanged` |
| `JNI_AwSettings_Init` | `aw_settings.cc:840` | 构造入口（Java 驱动） |
| `FromWebContents` | `aw_settings.cc:160` | UserData 反查 |

**为什么这么写**：`PopulateFixedWebPreferences` 写死了一批 WebView 不可改的 blink 配置（如 `viewport_meta_enabled = true`），因为 WebView 的渲染行为和 Chrome 不同。`UpdateWebkitPreferencesLocked` 通过 `OnWebPreferencesChanged` 触发 renderer 重新拉取配置。

### 5. AwContentsIoThreadClient（IO 拦截通道）

| 入口 | file:line | 说明 |
|---|---|---|
| `Associate` | `aw_contents_io_thread_client.cc:424` | 把 Java 对象存成 WebContents UserData |
| `GetCacheMode` | `aw_contents_io_thread_client.cc:438` | **轻操作**：IO 线程直接调 Java |
| `ShouldInterceptRequestAsync` | `aw_contents_io_thread_client.cc:583` | **重操作**：post 到后台线程再调 Java |
| `StartShouldInterceptRequest` | `aw_contents_io_thread_client.cc:498` | 后台线程执行，JNI 调 Java |

**为什么这么写**：轻/重操作分线程策略——`GetCacheMode` 只读一个值，IO 线程直接调；`shouldInterceptRequest` App 可能做大量处理，必须 post 到后台线程避免阻塞 IO 线程。短命 handle 模式（按需 new、用完弃）避免 Global ref 阻止 GC（L2）。

> ⚠️ **重要区分**：`AwContentsIoThreadClient` 是"调 Java 的**管道**"；`aw_proxying_url_loader_factory.cc` 才是"拦截**逻辑**"（决定拦不拦、怎么处理 response）。Bug 在逻辑层先开 factory。

---

# Phase 7 — Bug 演练（掌握度测试）

## 五个 Bug 场景 × 第一步打开哪个文件

| 场景 | 第一步打开 | file:line | 理由 |
|---|---|---|---|
| 网络拦截失效 | `aw_proxying_url_loader_factory.cc` | `:632`（跳过逻辑）/ `:523`（response 处理） | 拦截**逻辑**在这里，io_thread_client 只是管道 |
| 设置不生效 | `aw_settings.cc` | `:304` `UpdateWebkitPreferencesLocked` | 配置变更通知入口；再看 `:75` 是否被覆盖 |
| 回调丢失（onReceivedError/JS dialog/证书） | `aw_contents_client_bridge.cc` | `:449` `OnReceivedError` | 回调通道入口；顺藤摸瓜到上游触发点 |
| WebView crash（popup/跳转） | `aw_contents.cc` | `:1212` popup / `:372` 析构 / `:445` Destroy | 生命周期问题，popup rebinding + 销毁链都在这 |
| 全局功能异常（SafeBrowsing/Policy） | `aw_browser_process.cc` | `:131` 构造 / `:127` GetInstance | 全局服务单例，初始化/获取入口 |

## Bug 演练口诀

```
拦截逻辑 → factory，调 Java → io_thread_client
设置不生效 → aw_settings.cc:304
回调丢失 → aw_contents_client_bridge.cc:449
crash/生命周期 → aw_contents.cc（popup/析构/Destroy）
全局服务 → aw_browser_process.cc:131
```

## 掌握度结论

- `AwSettings` / `AwContentsClientBridge` / `AwBrowserProcess`：Bug 入口秒答 ✅
- `AwContentsIoThreadClient`：知道方向但需区分"逻辑层(factory)"和"管道层(client)" ⚠️
- `AwContents` 生命周期 crash：popup rebinding + 销毁链的实战应用，记牢 ⚠️
## 后续阶段（待推进）

- [x] **L2 设计层** ✅：为什么不用别的方法？为什么 AwContents 拥有 WebContents 而不是反过来？为什么 IO 线程要短命句柄而非常驻？为什么回调分两条链路？
- [x] **L3 对象关系** ✅：谁创建/持有/销毁？AwContents 的构造点与销毁点？popup 场景的 rebinding？
- [x] **L4 执行流程** ✅：选一条主调用链（如 `shouldInterceptRequest` 端到端）自己追画。
- [x] **L5 实现入口** ✅：按需，指路 `file:line`。
- [x] **Bug 演练** ✅：「WebView 出 Bug 第一步打开哪个文件？」
