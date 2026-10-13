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
