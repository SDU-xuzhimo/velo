第一阶段：建立骨架（★★★★★）

这一阶段的目标只有一个：

知道 Chromium 每个模块在整个系统中的位置。

不要一开始钻实现。

先建立地图。

第一站：Android WebView

这是你的入口。

建议阅读：

android_webview/

AwBrowserProcess

AwContents

AwContentsClientBridge

AwSettings

AwContentsIoThreadClient

目标：

能够画出来：

Java WebView

↓

AwContents

↓

WebContents

↓

content/

知道：

Java什么时候进入Native。

Native什么时候进入content。

第二站：content/

这是整个 Chromium 最重要的目录。

建议：

content/browser/

content/public/

content/common/

content/renderer/

重点不是函数。

而是对象。

例如：

BrowserContext

WebContents

NavigationController

NavigationRequest

RenderFrameHost

RenderProcessHost

这里建议：

自己画：

对象关系图。

第三站：Browser Architecture

理解：

Browser Process

↓

Renderer Process

↓

GPU Process

↓

Network Service

↓

Utility Process

以及：

为什么：

要多进程。

为什么：

Site Isolation。

为什么：

Mojo。

第一阶段结束以后。

你脑子里面应该已经有：

整个浏览器长什么样。
第二阶段：深入核心（★★★★★）

这一阶段开始深入：

真正工作中最容易碰到的模块。

第一部分：Navigation

建议重点：

NavigationController

NavigationRequest

Navigator

FrameTree

SiteInstance

建议：

追完整个：

loadUrl()

↓

Navigation

↓

Commit

↓

Renderer

这是：

Chromium最经典的一条调用链。

第二部分：Mojo IPC

重点：

不要背API。

理解：

Remote

Receiver

PendingRemote

PendingReceiver

以及：

Browser

↓

Renderer

之间：

消息：

怎么走。

以后：

Debug。

非常重要。

第三部分：Renderer

重点：

RenderFrameHost

RenderFrameImpl

RenderWidgetHost

WebContents

理解：

Browser里的Frame。

Renderer里的Frame。

为什么：

两个Frame。

第四部分：Rendering Pipeline

开始：

真正进入：

Blink。

但是：

不要看全部。

建议：

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

Compositor

↓

Tile

↓

Raster

↓

GPU

如果你工作涉及 Tile、LayerTreeHost、Compositor，就顺着实际问题深入；如果没有涉及，不需要为了"学完 Chromium"而硬啃整个 cc/。

第三阶段：形成自己的方向（★★★★☆）

Chromium 太大了。

三年以后。

一定要开始选方向。

路线一（最推荐）

Browser Runtime

content/

Navigation

WebContents

Mojo

Process

Performance

这是：

Android WebView

Electron

CEF

Browser

共同能力。

也是我最推荐你的方向。

路线二

Rendering

Blink

Layout

Paint

cc

viz

Skia

适合：

性能优化。

GPU。

动画。

路线三

Network

net/

Cookie

Cache

URLLoader

QUIC

适合：

浏览器。

代理。

安全。

路线四

JavaScript Runtime

V8

GC

TurboFan

Ignition

技术很深。

但是：

优先级最低。

除非以后：

专门做V8。