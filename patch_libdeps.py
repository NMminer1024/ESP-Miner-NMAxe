"""Local patches for ESPAsyncWebServer WebSocket correctness.

WHY
---
`AsyncWebSocketClient::_onData()` handles a received close frame like this:

    uint16_t reasonCode  = (uint16_t)(data[0] << 8) + data[1];
    char    *reasonString = (char *)(data + 2);
    if (reasonCode > 1001)
      _server->_handleEvent(this, WS_EVT_ERROR, &reasonCode,
                            (uint8_t *)reasonString, strlen(reasonString));

`data` points into the receive buffer and `data[datalen]` is NOT NUL-terminated
at that moment — the library itself restores `data[datalen]` only *after* this
branch ("restore byte as _handleEvent may have added a null terminator"). So
`strlen()` scans past the end of the close frame into whatever bytes happen to
follow it in the TCP buffer / heap.

This is real and field-observed: coredumps from two boards show the panic PC
inside ROM `strlen` on the `async_tcp` task, with `exc_cause = 28`
(LoadProhibited). It is reachable from the network by any client that sends an
abnormal close code (> 1001) without reason text.

Still present in v3.10.3 (the version we build) and in v3.12.1; fixed upstream on
`main` only. The `_ws_clients_lock` work in v3.10.3 addressed a different defect
(the `_clients` list race) and does not touch this line.

WHAT THIS DOES
--------------
Replaces `strlen(reasonString)` with `strnlen(reasonString, datalen - 2)`, i.e.
the scan is bounded to the bytes that actually belong to this frame.

It also makes an outgoing WebSocket frame one TCP enqueue transaction. Upstream
queues the frame header and payload with separate `AsyncClient::add()` calls. If
the header succeeds but the payload hits lwIP's pbuf queue limit, the orphaned
header remains queued while `AsyncWebSocketMessage::send()` rolls its counters
back and retries the whole frame. The wire then contains header, header, payload.

The patched form builds header + payload in one temporary buffer and calls
`add()` once. Normal log frames use a small stack buffer; only larger frames
allocate. A failed add queues nothing; a successful add is the commit point.
`send()` is only a best-effort `tcp_output()` kick after that point, because its
failure does not undo the preceding successful `tcp_write()`.

HOW IT IS APPLIED
-----------------
Registered from `platformio.ini` as `extra_scripts = post:patch_libdeps.py`.
It patches the copy of the library that PlatformIO unpacked under
`.pio/libdeps/<env>/ESPAsyncWebServer/src/AsyncWebSocket.cpp` for *this* build
environment. Re-running is safe and a no-op once applied; `pio run -t clean` does
not delete `.pio/libdeps`, and a reinstall of the library simply re-applies it.
"""

import os
import re

Import("env")

# Marker present only in our patched form; used to make the operation idempotent.
_CLOSE_MARKER = "strnlen(reasonString, datalen - 2)"
_ATOMIC_FRAME_MARKER = "local patch: queue each WebSocket frame atomically"

# The upstream call, whitespace-tolerant so it matches v3.6.0 (10 spaces indent)
# as well as v3.9.0+ (12 spaces) without pinning an exact revision.
_PATTERN = re.compile(
    r"^(?P<indent>[ \t]*)_server->_handleEvent\(this,\s*WS_EVT_ERROR,\s*"
    r"\(void\s*\*\)&reasonCode,\s*\(uint8_t\s*\*\)reasonString,\s*"
    r"strlen\(reasonString\)\);",
    re.MULTILINE,
)

_PATCHED = (
    "{i}// --- local patch: bound the close-reason scan (patch_libdeps.py) --------\n"
    "{i}// data[datalen] is NOT NUL-terminated here; the library restores it only\n"
    "{i}// after this branch, so strlen() ran past the close frame into whatever\n"
    "{i}// bytes followed it. Field symptom: LoadProhibited in ROM strlen on the\n"
    "{i}// async_tcp task. Bound the scan to this frame's bytes instead.\n"
    "{i}const size_t reason_len = (datalen > 2) ? strnlen(reasonString, datalen - 2) : 0;\n"
    "{i}_server->_handleEvent(this, WS_EVT_ERROR, (void *)&reasonCode, "
    "(uint8_t *)reasonString, reason_len);"
)

_FRAME_PATTERN = re.compile(
    r"(?P<indent>[ \t]*)uint8_t \*buf = \(uint8_t \*\)malloc\(headLen\);.*?"
    r"(?P=indent)// Serial\.println\(\"SF\"\);\n"
    r"(?P=indent)return len;",
    re.DOTALL,
)

_FRAME_PATCHED = r"""\g<indent>// --- local patch: queue each WebSocket frame atomically -------------
\g<indent>// Two separate add() calls can leave an orphaned header in lwIP when
\g<indent>// the payload write hits the pbuf queue limit. The caller then rolls
\g<indent>// back and retries, producing header + header + payload on the wire.
\g<indent>const size_t frameLen = headLen + len;
\g<indent>uint8_t stackBuf[256];
\g<indent>uint8_t *buf = frameLen <= sizeof(stackBuf) ? stackBuf : (uint8_t *)malloc(frameLen);
\g<indent>if (buf == NULL) {
\g<indent>  async_ws_log_e("Failed to allocate");
\g<indent>  return 0;
\g<indent>}

\g<indent>buf[0] = opcode & 0x0F;
\g<indent>if (final) {
\g<indent>  buf[0] |= 0x80;
\g<indent>}
\g<indent>if (len < 126) {
\g<indent>  buf[1] = len & 0x7F;
\g<indent>} else {
\g<indent>  buf[1] = 126;
\g<indent>  buf[2] = (uint8_t)((len >> 8) & 0xFF);
\g<indent>  buf[3] = (uint8_t)(len & 0xFF);
\g<indent>}
\g<indent>if (len && mask) {
\g<indent>  buf[1] |= 0x80;
\g<indent>  memcpy(buf + (headLen - 4), mbuf, 4);
\g<indent>  for (size_t i = 0; i < len; i++) {
\g<indent>    buf[headLen + i] = data[i] ^ mbuf[i % 4];
\g<indent>  }
\g<indent>} else if (len) {
\g<indent>  memcpy(buf + headLen, data, len);
\g<indent>}

\g<indent>const size_t queued = client->add((const char *)buf, frameLen);
\g<indent>if (buf != stackBuf) {
\g<indent>  free(buf);
\g<indent>}
\g<indent>if (queued != frameLen) {
\g<indent>  return 0;
\g<indent>}

\g<indent>// add()/tcp_write() is the commit point. tcp_output() failure leaves
\g<indent>// the bytes queued for a later poll/ACK, so it must not trigger retry.
\g<indent>(void)client->send();
\g<indent>return len;"""


def _target_path():
    """Path of the AsyncWebSocket source for the environment being built."""
    proj = env.subst("$PROJECT_DIR")
    pioenv = env.subst("$PIOENV")
    return os.path.join(
        proj, ".pio", "libdeps", pioenv, "ESPAsyncWebServer", "src", "AsyncWebSocket.cpp"
    )


def _apply(path):
    if not os.path.isfile(path):
        # Library not unpacked yet (fresh checkout / first build of this env).
        # The build-prog pre-action retries once PlatformIO has installed deps.
        return None

    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        src = f.read()

    close_done = _CLOSE_MARKER in src
    frame_done = _ATOMIC_FRAME_MARKER in src
    if close_done and frame_done:
        return "already-patched"

    new_src = src
    close_n = 0
    frame_n = 0
    if not close_done:
        new_src, close_n = _PATTERN.subn(
            lambda m: _PATCHED.format(i=m.group("indent")), new_src
        )
    if not frame_done:
        new_src, frame_n = _FRAME_PATTERN.subn(_FRAME_PATCHED, new_src)

    if not close_done and close_n == 0:
        # Not the shape we expect -> upstream changed. Fail loudly instead of
        # silently shipping an unpatched (still buggy) library.
        print(
            "[patch_libdeps] ERROR: could not find the unbounded "
            "strlen(reasonString) call in %s" % path
        )
        print(
            "[patch_libdeps] The library probably changed. Re-audit "
            "AsyncWebSocketClient::_onData() and update patch_libdeps.py."
        )
        env.Exit(1)
    if not frame_done and frame_n == 0:
        print(
            "[patch_libdeps] ERROR: could not find the split WebSocket "
            "header/payload enqueue in %s" % path
        )
        print(
            "[patch_libdeps] The library probably changed. Re-audit "
            "webSocketSendFrame() and update patch_libdeps.py."
        )
        env.Exit(1)

    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
        f.write(new_src)

    n = close_n + frame_n
    print("[patch_libdeps] patched %s (%d site%s)" % (path, n, "" if n == 1 else "s"))
    return "patched"


def _patch_now(target=None, source=None, env=None):
    _apply(_target_path())


# 1) Try immediately (works when the library is already installed, i.e. every
#    build after the first one).
_status = _apply(_target_path())
if _status == "already-patched":
    print("[patch_libdeps] ESPAsyncWebSocket correctness patches already applied")
elif _status is None:
    print("[patch_libdeps] library not unpacked yet, will patch before compiling")

# 2) Also hook right before the program is built, which is after PlatformIO has
#    installed lib_deps on a fresh checkout but before the sources are compiled.
env.AddPreAction("buildprog", _patch_now)
