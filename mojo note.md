# Mojo IPC 源码导读（怎么用，不钻内部实现）

> 第二阶段 · 第二部分 | 代码源：/Volumes/externelSSD/chromium/src（Chrome 146）
> 学习方式：使用模式 + 真实代码示例 + Debug 视角

---

## 心智模型：四个对象 = 电话系统

| 对象 | 角色 | 一句话 |
|---|---|---|
| `Remote<T>` | 打电话的人 | 持有它就能调 T 的方法（异步） |
| `Receiver<T>` | 接线员 | 收到消息分发给实现对象 |
| `PendingRemote<T>` | 未接通的线（主叫端） | 可跨进程/线程传递 |
| `PendingReceiver<T>` | 未接通的线（被叫端） | 可跨进程/线程传递 |

**核心规则**：`Pending*` 未接通可传；`Remote`/`Receiver` 已接通才能用。接口定义在 `.mojom` 文件。

---

## 管道的三种创建方向（关键 API 表）

| API | 谁调用 | 谁留哪端 | 给出去什么 |
|---|---|---|---|
| `Remote::BindNewPipeAndPassReceiver()` | 调用方 | 自己留 Remote | `PendingReceiver` 给实现方 |
| `Receiver::BindNewPipeAndPassRemote()` | 实现方 | 自己留 Receiver | `PendingRemote` 给调用方 |
| `PendingRemote::InitWithNewPipeAndPassReceiver()` | **纯 Broker** | **两端都给出去** | 分发给两个进程 |

---

## 四种使用模式

### 模式 1：发起方直连（80% 场景）

```cpp
// Browser 侧（发起方）
mojo::Remote<blink::mojom::Foo> remote;                          // 1. 创建空 Remote
mojo::PendingReceiver<blink::mojom::Foo> receiver =
    remote.BindNewPipeAndPassReceiver();                         // 2. 接通，另一端变 PendingReceiver
GetProcess()->BindReceiver(std::move(receiver));                 // 3. 发给对方进程
remote->DoSomething();                                           // 4. 直接调（异步）

// Renderer 侧（实现方）
mojo::Receiver<blink::mojom::Foo> receiver_{&impl_, std::move(pending_receiver)};
```

口诀：**BindNewPipeAndPassReceiver = "我建管道，你拿听筒"**。
真实代码：`render_frame_host_impl.cc:1965`。

### 模式 2a：介绍人模式（Browser 创建管道，Renderer↔Utility 直连）

```cpp
// Browser 进程（纯协调者，从头到尾不碰消息流）
mojo::PendingRemote<mojom::Foo> pending_remote;
mojo::PendingReceiver<mojom::Foo> pending_receiver =
    pending_remote.InitWithNewPipeAndPassReceiver();   // ① 一对端点都在我手里
SendToUtility(std::move(pending_receiver));           // ② 听筒给 Utility（实现方）
SendToRenderer(std::move(pending_remote));            // ③ 话筒给 Renderer（调用方）
// 之后 Renderer ↔ Utility 直接通信

// Utility:  mojo::Receiver<mojom::Foo> receiver_{&impl_, std::move(pr)};
// Renderer: mojo::Remote<mojom::Foo> remote(std::move(pending_remote));
```

真实代码：`cache_storage_cache_entry_handler.cc:401-404`。
最大应用：`CommitNavigation` 发的 `subresource_loader_factories`——Browser 打包一堆 `PendingRemote<URLLoaderFactory>` 给 Renderer，之后**页面所有子资源请求直达 Network Service，Browser 不参与**。

### 模式 2b：中转退位模式（先中转，后退位）

```
初始：Browser(NavigationURLLoaderImpl) ↔ NetworkService 中转
转交：URLLoaderClientEndpoints{ pending_remote<URLLoader>, pending_receiver<URLLoaderClient> }
     Browser Unbind() 打包两端 → 跟 CommitNavigation 发给 Renderer
之后：Renderer ↔ NetworkService 直连，Browser 退出
```

真实代码：`navigation_url_loader_impl.cc:1073`（Unbind）→ `navigation_request.cc:6183`（转交）。
口诀：**Unbind() = "我要退出了，把管道让给别人"**。

### 模式 3：Receiver 端两种写法

```cpp
mojo::Receiver<mojom::Foo> receiver_{this, std::move(pr)};   // 一对一
mojo::ReceiverSet<mojom::Foo> receivers_;                    // 一对多
receivers_.Add(this, std::move(pr_1));
```

共享服务用 Set；连接与实现一一对应用 Receiver。真实：`render_frame_impl.h:1450`。

### 模式 4：Associated（复用已有管道）

```cpp
mojo::AssociatedRemote<blink::mojom::Frame> frame;   // 挂在主管道上
mojo::PendingAssociatedReceiver<mojom::Frame> frame_receiver;
```

场景：接口与已有对象强绑定 + 需与主管道消息保序（如 CommitNavigation 与 Frame 消息有序）。
真实：`render_frame_host_impl.h:1714` GetAssociatedLocalFrame()；`render_frame_impl.h:234`。

---

## is_bound() vs is_connected()（本地状态 vs 对端状态）

```cpp
// remote.h:116
bool is_bound() const { return internal_state_.is_bound(); }
explicit operator bool() const { return is_bound(); }

// remote.h:132
bool is_connected() const {
  DCHECK(is_bound());                        // 必须先 bound 才能问
  return !internal_state_.encountered_error();
}
```

| | `is_bound()` | `is_connected()` |
|---|---|---|
| 问什么 | 本地：手里有没有管道端点 | 对端：对面的 Receiver 活着吗 |
| 何时变 false | **只有自己显式** reset()/Unbind() | 对端被动死亡：对端 crash / Receiver 析构 / **PendingReceiver 未消费就被析构** |
| 被动变吗 | **永远不会**（"A Remote is NEVER passively unbound"） | 随时会断 |
| 类比 | 电话拿没拿在手里 | 对面有没有人接 |

**四象限**：

| is_bound | is_connected | 状态 |
|---|---|---|
| false | （DCHECK 禁问） | 未绑定：空 Remote / 自己 reset 了 |
| true | true | ✅ 管道完整 |
| true | false | ⚠️ 断连：对端 crash / Receiver 析构 / PendingReceiver 半路被丢 |

**最关键 Debug 认知**：调用 `is_connected() == false` 的 Remote **不会 crash，消息静默丢弃**——"调了没反应"的最常见根因。不是"失败返回"，是**石沉大海**。

**三种 DCHECK 崩法**：
1. Remote 未 bind 就调 `is_connected()` → 内部 `DCHECK(is_bound())` 崩（本地 bug）
2. 对端 crash → `encountered_error` → 外层 `DCHECK(is_connected())` 崩
3. PendingReceiver 未消费被析构 → 同上

---

## 心跳线模式（对端进程死亡探测器）

真实代码（146 版 `render_frame_host_impl.cc:13907-13926`）：

```cpp
if (IsOutOfProcessNetworkService() &&
    (!network_service_disconnect_handler_holder_ ||                  // ① 没绑过
     !network_service_disconnect_handler_holder_.is_connected())) {    // ② 绑了但对端死了
  network_service_disconnect_handler_holder_.reset();                   // ③ 显式解绑
  storage_partition->GetNetworkContext()->CreateURLLoaderFactory(
      network_service_disconnect_handler_holder_.BindNewPipeAndPassReceiver(),  // ④ 重建
      std::move(monitoring_factory_params));
  network_service_disconnect_handler_holder_.set_disconnect_handler(    // ⑤ 断连回调
      base::BindOnce(&RenderFrameHostImpl::UpdateSubresourceLoaderFactories, ...));
}
```

**设计**：持有一个"监控用 factory"管道，`request_initiator_origin_lock` 锁死 invalid URL——**永远不发真请求，纯当心跳线**。Network Service crash → disconnect handler → 重建所有子资源 factory。

**恢复流程**：
```
Network Service crash → 对端 Receiver 死
  → Browser 心跳线 disconnect handler 触发
  → UpdateSubresourceLoaderFactories
  → 创建全新 factory PendingRemote → 发给 Renderer → Bind 替换旧的
```
注意：Renderer 侧旧 Remote 是**被动断连**（disconnected），不是需要主动 unbind；恢复靠**换新管道**，不是复用旧的。

---

## Debug 视角速查表

| 看到什么 | 立刻想到什么 |
|---|---|
| `mojo::Remote<T>` | 调用方，搜 T 的实现在哪 |
| `BindNewPipeAndPassReceiver()` | 调用方建管道，听筒给出去 |
| `BindNewPipeAndPassRemote()` | **实现方**建管道，话筒给出去 |
| `InitWithNewPipeAndPassReceiver()` | **Broker 建管道**，两端分发，自己不碰消息 |
| `Unbind()` | 中转退位前兆 |
| `is_bound()` 检查 | 本地状态：忘 Bind？自己 reset 过？ |
| `is_connected()` 检查 | 对端状态：对端 crash？PendingReceiver 半路丢了？ |
| `set_disconnect_handler()` | 对端死亡探测器（心跳线） |
| 调了没反应 | 先查 `is_connected()`——断连调用**静默丢弃** |
| 找接口定义 | `grep -rn "interface T" --include="*.mojom"` |

---

## Check 验收记录（Sep 6）

- is_connected 断连路径：✅ 对端 crash + PendingReceiver 未消费被析构（补记：未 bind 调 is_connected 的内部 DCHECK 崩法）
- 介绍人模式代码：✅ 结构全对（API 名笔误：InitWithNewPipeAndPassReceiver）
- 心跳线恢复：✅ 大方向对。精确化两点：① 旧 Remote 是被动断连不是主动 unbind，恢复=换新管道；② 断连调用不是"失败返回"而是"静默丢弃、永远没响应"
