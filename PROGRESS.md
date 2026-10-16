# Chromium 学习进度（重启后读此文件恢复上下文）

> 最后更新：Sep 6, 2026（Renderer 完成；Blink Pipeline 讲义已给，Check 待答）
> 方法论：read-module skill（L1架构 → L2设计 → L3对象 → L4执行流 → L5入口 → Bug演练）
> 笔记目录：/Users/orionxie/chromium笔记/

## 当前位置

**第二阶段：深入核心（★★★★★）—— 进行中**
- 当前部分：**第四部分 Rendering Pipeline**（HTML → DOM → Style → Layout → Paint → Compositor → Tile → Raster → GPU；不硬啃 cc/）
- 学习方式：待定（进入时更新）

## 已完成

### 第一阶段：建立骨架（★★★★★）✅ 全部完成
- [x] 第一站：android_webview（embedder 层）→ `android_webview note.md`（572 行，Jul 20）
  - 五类：AwBrowserProcess / AwContents / AwContentsClientBridge / AwSettings / AwContentsIoThreadClient
- [x] 第二站：content/（内核层）→ `content note.md`（704 行，Aug 26）
  - 六对象：BrowserContext / WebContents / NavigationController / NavigationRequest / RenderFrameHost / RenderProcessHost
  - L4 已追完 loadUrl → Navigation → Commit → Renderer 完整调用链
  - 深入过 NavigationURLLoader / network service / URLLoader
- [x] 第三站：Browser Architecture（进程层）→ `browser architecture note.md`（356 行，Sep 1）
  - 五种进程 / 为什么多进程 / Site Isolation / Mojo / 单一二进制启动分派

### 第二阶段进度
- [x] 第一部分：Navigation —— ✅ 彻底闭环（主链 L4 + SiteInstance/FrameTree 补完已归档 content note.md）
  - 剩余可补：Navigator / FrameTree / SiteInstance 深入（低优先级，SiteInstance 第三站已触及）
- [x] 第二部分：Mojo IPC ✅ → `mojo note.md`（191 行）
  - 上轮对话（Sep 1）进行到 check 未归档，本次重讲：重使用模式
- [x] 第三部分：Renderer ✅ → `renderer note.md`（206 行）
- [ ] 第四部分：Rendering Pipeline —— 讲义已归档 `blink pipeline note.md`（243 行），Check 两题待答
  - road.md 提醒：工作不涉及 Tile/LayerTreeHost/Compositor 就不硬啃 cc/

## 代码源

- **当前（第二阶段起）**：`/Volumes/externelSSD/chromium/src`（Chrome 146.0.7680.178）
- 前两站笔记的 file:line 基于 meco-kernel（Chrome 134），146 版行号可能偏移，按类名+方法名搜

## 恢复指令

重启后：
1. 读本文件恢复进度
2. 上一轮对话内容不可见，以本文件 + 各 note.md 为准
3. 用户偏好：架构图/调用链图由 AI 给，用户写总结/答 Bug 演练；L2 设计层用 Socratic（AI 抛为什么，用户答，AI 评判）
