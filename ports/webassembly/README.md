# MicroPython WebAssembly — 浏览器专用构建

本目录基于用户上传的最新 WebAssembly 端口及定制版 v1.23 迁移结果，
**仅支持浏览器页面 / Dedicated Web Worker**。不提供 Node.js 运行时、
Node CLI、REPL 或依赖 `npx` 的压缩目标。

## 保留

- `loadMicroPython`, `runPython`, `runPythonAsync`, `pyimport`, `registerJsModule` 和 Python 对象代理
- `FS` 虚拟文件系统，用于现有 `esptool`、`kflash`、`avrdude` 等 Python CLI **代码**的运行（删除的是 Node 命令行入口，并非这些 Python 工具）
- `stdout`、`stderr` 回调，适配网页终端；可选 `stdin` 回调
- 浏览器 `asyncio` 调度、Asyncify、VM 协作让出、JS 中断请求
- 自动分段 GC 堆、WASM 线性内存动态增长、`python3` 可选变体

## 构建

将 `webassembly` 目录放到 MicroPython 仓库的 `ports/` 下，在根仓库准备好 `mpy-cross`、submodules 后：

```bash
cd ports/webassembly
make VARIANT=standard -j4
# 需要冻结更多纯 Python 标准库时：
make VARIANT=python3 -j4
```

产物在 `build-standard/` 或 `build-python3/`：`micropython.mjs` 与 `micropython.wasm`。
无需 Node.js 运行时或 Node CLI；依然需要构建工具 Emscripten / GNU Make。
`-s ENVIRONMENT=web,worker` 使输出不包含 Emscripten 的 Node 环境兼容分支。

## FATFS / LittleFS 支持

所有变体（`standard`、`python3`、`pyscript`）均默认包含下列 VFS 类，
同时保留 `VfsPosix`，不改变浏览器的 Emscripten FS 行为：

| VFS 类 | 编译开关 | 底层代码 |
| --- | --- | --- |
| `vfs.VfsFat` | `MICROPY_VFS_FAT=1` | `lib/oofatfs` |
| `vfs.VfsLfs1` | `MICROPY_VFS_LFS1=1` | `lib/littlefs/lfs1*` |
| `vfs.VfsLfs2` | `MICROPY_VFS_LFS2=1` | `lib/littlefs/lfs2*` |

`Makefile` 在 `extmod/extmod.mk` 之前设置这三个变量。后者负责
加入源文件及 `-D` 预处理宏，**不要只在头文件中定义 VFS 宏**，
否则相关文件系统底层实现可能没有参与链接。

FATFS 启用了长文件名（LFN）、512～4096 字节扇区及相对路径，
`fatfs_port.c` 利用浏览器运行时的时间接口提供 FAT 文件时间戳。
这些设置支持普通 FAT 镜像，**不负责 ESP-IDF FATFS Wear Leveling
容器的解析**。读取这类 ESP32 分区前仍需处理 Wear Leveling 层。

构建命令：

```bash
cd ports/webassembly
make VARIANT=standard -j4
make VARIANT=python3 -j4
```

按需关闭某一文件系统（修改编译选项后需清理对应构建目录）：

```bash
make VARIANT=standard MICROPY_VFS_LFS1=0 -j4
```

运行时首先检查是否成功注册：

```python
import vfs
for name in ("VfsPosix", "VfsFat", "VfsLfs1", "VfsLfs2"):
    print(name, hasattr(vfs, name))
```

`port-tests/vfs-smoke.py` 提供三个文件系统的 RAM 块设备格式化、
挂载、文件读写和卸载冒烟测试，可加载至浏览器的 MicroPython 运行环境执行。
需要先将 `lib/littlefs` 等 MicroPython 依赖源文件准备完整，
本目录本身不是完整的 MicroPython 仓库。

## 浏览器 Worker 示例

```javascript
import { loadMicroPython } from './micropython.mjs';

const mp = await loadMicroPython({
  heapsize: 32 * 1024 * 1024,
  stdout: line => postMessage({ type: 'stdout', text: line }),
  stderr: line => postMessage({ type: 'stderr', text: line }),
});
await mp.runPython('import gc, asyncio; print(gc.mem_free())');
// 每次任务运行完成，由主线程决定是否终止当前 Worker。
```

**注意：** 由于 Asyncify，`runPython()` / `runPythonAsync()` 都返回 Promise，必须 `await`。
内存最大值 2GiB 是配置上限，并非实际可用内存的保证。
避免跨异步挂起缓存 WASM 内存 TypedArray 视图。

## 验证

使用 HTTP 本地服务访问 `port-tests/browser-smoke.html`，点击页面测试按钮即可在真实浏览器和 Worker 中验证 asyncio、动态堆扩容及桥接。
`file://` 协议通常无法加载 WASM 模块，请用 localhost 或 HTTPS。

## 浏览器 REPL 与 Node CLI 的区别

只移除 Node.js 的 `runCLI()`、`process.stdin`、`node_run.sh` 和 `make repl` 等启动入口。
浏览器在线 REPL 必须保留 `shared/runtime/pyexec.c`、`shared/readline/readline.c`、
`MICROPY_REPL_EVENT_DRIVEN=1`，以及 `replInit()` / `replProcessCharWithAsyncify()`。

典型浏览器 Worker 接口（输出建议 `linebuffer:false`，否则 `>>> ` 提示符不即时显示；主线程可使用 xterm.js）：

```js
const mp = await loadMicroPython({
    heapsize: 32 * 1024 * 1024,
    linebuffer: false,
    stdout: bytes => postMessage({ type: "stdout", bytes }),
    stderr: bytes => postMessage({ type: "stderr", bytes }),
});
mp.replInit();
const encoder = new TextEncoder();
self.onmessage = async ({ data }) => {
    if (data?.type !== "terminal-input") return;
    for (const byte of encoder.encode(data.text)) {
        await mp.replProcessCharWithAsyncify(byte);
    }
};
```

`replProcessChar()` 为同步兼容接口；可能触发 Asyncify 的 Python 代码请使用异步 API。
`input()` 虽会重新参与链接，但上游 `mphalport.h` 的 `mp_hal_stdin_rx_chr()`
目前仍为 `(0)` 占位实现；REPL 中运行 `input()` 还需要独立的浏览器 stdin
桥接或 Worker 输入队列，`replProcessChar()` 并不等同于程序运行期间的 stdin。

## J-Link / libjaylink / pylink 分阶段集成（新增）

项目现在包含 `usermods/jlink_native`（MicroPython C 内置模块）、
`compat/libusb_mpy.c`（libjaylink 用的 libusb API 兼容层，实际 USB I/O 经由已有
Python `usb.core.Device.read/write`）、`python/pylink_integration/jlink_backend.py`
（pylink 风格的阶段性适配入口）。这些源文件被纳入当前 WebAssembly Make 构建。

**重要：压缩包内不包含 libjaylink 的上游 C 源码及 SEGGER DLL。**
构建环境可联网后，需要先执行：

```bash
cd ports/webassembly
./usermods/jlink_native/fetch-libjaylink.sh
make VARIANT=standard clean
make VARIANT=standard -j4
```

`fetch-libjaylink.sh` 默认获取上游仓库。为可重复构建，建议设置
`LIBJAYLINK_REV=<具体提交哈希>`；源码也可以手工放到
`usermods/jlink_native/vendor/libjaylink`。此脚本不会修改已有的上游 checkout。

如果**没有**下载上游源码，原生模块仍能编译为 **transport-only** 模式，
可以测试 C ↔ Python USB 调用，但执行 `jaylink_native.firmware_version()` 会
明确抛出 `NotImplementedError`。上游源码存在时，构建会编入核心协议和
USB transport，并尝试通过 Python USB 调用查询真实 J-Link 固件版本。
**这段完整链路尚未在 Emscripten 或真实 J-Link 设备上验证。**

Python 侧使用方法（`usb.core` 的设备需已在网页设备管理器中配对，
由原有 WebUSB 桥接实现同步读写）：

```python
import usb.core
import jaylink_native

usb = usb.core.find(idVendor=0x1366)
if usb is None:
    raise RuntimeError("J-Link not found")

# 下列端点只是示例，必须从实际活动配置描述符获取。
# 当前桥接仅针对 J-Link vendor interface 0、Bulk IN/OUT。
jaylink_native.bind_usb(usb, 0x81, 0x02, 0, None)
try:
    print(jaylink_native.backend())
    print(jaylink_native.firmware_version())
finally:
    jaylink_native.unbind_usb()
```

`python/pylink_integration/jlink_backend.py` 不会覆盖原本的 `pylink-square`
（也不会试图在 WASM 中执行 SEGGER 的闭源 DLL）。原版 pylink 在 MicroPython 下
若因 `ctypes` 导入失败，请直接使用冻结好的 `from jlink_backend import JLink`，
不能靠 monkeypatch 修复一个尚未成功导入的包。兼容模式可将适配层复制至
`lib/site-packages/jlink_backend.py`（此构建已经同时将该模块冻结），然后：

```python
import pylink
from jlink_backend import install_into_pylink

install_into_pylink(pylink)  # 显式启用浏览器兼容入口
link = pylink.JLink()
link.open(usb_device=usb, ep_in=0x81, ep_out=0x02)
try:
    print(link.firmware_version())
finally:
    link.close()
```

**目前并不支持** pylink `connect`、SWD 内存读写或 Flash 擦写；相关接口明确
返回 `NotImplementedError`。后续要将原 pylink 所调用的 `JLINKARM_*`
函数逐一映射到 libjaylink / CoreSight；libjaylink 本身不完整代替 SEGGER API。
当前 libusb 兼容层特意只管理**单个已配对设备**：无浏览器设备发现、
不抢占 WebUSB 权限、不关闭 Python 所拥有的 USB 设备，也不支持网络 J-Link。

单元检查：

```bash
python3 port-tests/test-jlink-backend.py
gcc -std=c99 -Wall -Wextra -Werror \
    -Iusermods/jlink_native/compat \
    port-tests/test-libusb-shim.c \
    usermods/jlink_native/compat/libusb_mpy.c \
    -o /tmp/jlink-shim-test
/tmp/jlink-shim-test
```

MicroPython 编译后还可运行 `port-tests/jaylink-native-smoke.py` 检验 C →
Python USB fake 设备的调用链。上游代码版本的许可证以 checkout 为准。
如需恢复原始端口功能： `make JLINK_NATIVE=0 VARIANT=standard clean all`。


### libjaylink WebAssembly USB-only 修复（2026-10-14）

在浏览器中只使用通过 Python USB 接口连接的 J-Link，不需要 libjaylink 的
TCP/IP 发现和 socket 通道。旧版 `micropython.mk` 将 `socket.c`、
`discovery_tcp.c`、`transport_tcp.c` 也纳入编译，可能因 `fd_set`、
`FD_ZERO`、`select` 等 POSIX Socket 符号编译失败。

本版从 `JLINK_SOURCES` 排除了这三个 TCP 源文件，同时通过
`compat/tcp_disabled.c` 为原有 libjaylink USB/TCP 分发器提供明确返回
`JAYLINK_ERR` 的 TCP 占位函数，以解决后续可能出现的链接符号缺失。
上游 libjaylink 源码无须修改。

使用补丁时，只覆盖 `micropython.mk`、`compat/tcp_disabled.c`；保留已克隆的
`vendor/libjaylink` 目录，然后执行：

```bash
make VARIANT=standard clean
make VARIANT=standard -j4
```

已检查：TCP 兼容代码的 GCC `-Wall -Wextra -Werror` 语法，以及现有
Python backend 和 libusb shim 的宿主机模拟测试。缺少 emcc 与真实
libjaylink checkout，尚未执行完整 WASM 构建及 J-Link 实机测试。
