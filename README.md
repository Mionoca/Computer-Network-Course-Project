# UDP 可靠文件传输实验项目说明

## 项目功能

本项目是一个基于 UDP 的可靠文件传输实验程序，用来演示在不可靠传输环境下，如何通过应用层协议实现可靠传输。

项目实现了两种滑动窗口协议：

- Go-Back-N，简称 GBN
- Selective Repeat，简称 SR

程序会模拟数据包丢失和 ACK 丢失，并通过超时重传保证文件最终能够完整传输。

## 传输方向

文件传输方向固定为：

```text
GBN_client/test.txt  ->  GBN_sever/data.txt
```

也就是说：

- 客户端读取 `GBN_client/test.txt`
- 服务端接收后写入 `GBN_sever/data.txt`

## 目录结构

```text
code4/
  GBN_client/
    client.cpp              客户端源码
    test.txt                待发送文件
    GBN_client.sln          客户端 Visual Studio 解决方案
    GBN_client.vcxproj      客户端工程文件
    x64/Debug/GBN_client.exe

  GBN_sever/
    sever.cpp               服务端源码
    data.txt                接收结果文件
    GBN_sever.sln           服务端 Visual Studio 解决方案
    GBN_sever.vcxproj       服务端工程文件
    x64/Debug/GBN_sever.exe

  scripts/
    build-cl.ps1            命令行编译脚本
    run-server.ps1          UTF-8 控制台启动服务端
    run-client.ps1          UTF-8 控制台启动客户端
    test-transfer.ps1       自动端到端验证脚本
```

## 运行方式

推荐用脚本启动，脚本会自动切换控制台到 UTF-8，避免中文乱码。

先打开一个 PowerShell 窗口运行服务端：

```powershell
cd D:\大学\code4
powershell -ExecutionPolicy Bypass -File .\scripts\run-server.ps1
```

再打开另一个 PowerShell 窗口运行客户端：

```powershell
cd D:\大学\code4
powershell -ExecutionPolicy Bypass -File .\scripts\run-client.ps1
```

客户端菜单中可以输入：

```text
-testgbn
```

使用 Go-Back-N 方式传输文件。

也可以输入：

```text
-testsr
```

使用 Selective Repeat 方式传输文件。

其他命令：

```text
-time
-quit
```

`-time` 用来获取服务端时间，`-quit` 用来退出客户端。

## 编译方式

如果修改了源码，需要重新编译：

```powershell
cd D:\大学\code4
.\scripts\build-cl.ps1 -Target all
```

只编译客户端：

```powershell
.\scripts\build-cl.ps1 -Target client
```

只编译服务端：

```powershell
.\scripts\build-cl.ps1 -Target server
```

注意：编译前要先停止正在运行或正在调试的 `GBN_client.exe` 和 `GBN_sever.exe`，否则链接器无法覆盖 exe 文件。

## Visual Studio 打开方式

不要直接打开 `.exe` 文件。`.exe` 是编译后的可执行文件，不是源码。

要看客户端源码，打开：

```text
D:\大学\code4\GBN_client\GBN_client.sln
```

要看服务端源码，打开：

```text
D:\大学\code4\GBN_sever\GBN_sever.sln
```

或者直接打开：

```text
D:\大学\code4\GBN_client\client.cpp
D:\大学\code4\GBN_sever\sever.cpp
```

## 自动验证

可以运行自动验证脚本，脚本会分别测试 GBN 和 SR，并比较发送文件与接收文件是否一致：

```powershell
cd D:\大学\code4
.\scripts\test-transfer.ps1 -Mode all -TimeoutSeconds 90
```

看到以下输出表示通过：

```text
PASS gbn
PASS sr
```

## 关键参数

客户端和服务端使用相同的协议参数：

```text
windowSize = 5
orderCount = 12
maxPayloadSize = 1024
TimerCount = 2
```

客户端模拟数据包丢失：

```text
sendLossRate = 0.3
```

服务端模拟 ACK 丢失：

```text
ackLossRate = 0.2
```

## 常见问题

### 控制台中文乱码

优先使用 `scripts/run-server.ps1` 和 `scripts/run-client.ps1` 启动程序。脚本会自动执行 `chcp 65001`。

### Visual Studio 报中文相关语法错误

工程文件已经加入 `/utf-8` 编译选项。若 Visual Studio 仍显示旧错误，请执行：

1. 停止调试
2. 重新加载项目
3. 清理解决方案
4. 重新生成解决方案

### 编译时报无法打开 exe

说明程序正在运行，或者 Visual Studio 正在调试该 exe。先停止程序或关闭调试，再重新编译。
