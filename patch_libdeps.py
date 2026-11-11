"""Local patch for ESPAsyncWebServer: bound the WebSocket close-reason scan.

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
_MARKER = "strnlen(reasonString, datalen - 2)"

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

    if _MARKER in src:
        return "already-patched"

    new_src, n = _PATTERN.subn(
        lambda m: _PATCHED.format(i=m.group("indent")), src
    )

    if n == 0:
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

    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
        f.write(new_src)

    print("[patch_libdeps] patched %s (%d site%s)" % (path, n, "" if n == 1 else "s"))
    return "patched"


def _patch_now(target=None, source=None, env=None):
    _apply(_target_path())


# 1) Try immediately (works when the library is already installed, i.e. every
#    build after the first one).
_status = _apply(_target_path())
if _status == "already-patched":
    print("[patch_libdeps] ESPAsyncWebSocket close-reason scan already bounded")
elif _status is None:
    print("[patch_libdeps] library not unpacked yet, will patch before compiling")

# 2) Also hook right before the program is built, which is after PlatformIO has
#    installed lib_deps on a fresh checkout but before the sources are compiled.
env.AddPreAction("buildprog", _patch_now)
