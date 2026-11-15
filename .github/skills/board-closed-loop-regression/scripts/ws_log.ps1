# WS log capture for one board. Keeps retrying so it survives device reboots
# (OTA / watchdog) and grabs the stream as soon as WiFi is back.
# Run in a dedicated terminal BEFORE flashing; do not stop it across reboots.
#
# -- Frames are validated, not trusted ------
#
# A WebSocket message boundary is the ONLY boundary that matters: the firmware
# emits one LOG_* line per text frame, so a frame that does not look like one is
# evidence about the DEVICE, not about this client.
#
# The previous version decoded every ReceiveAsync chunk independently. That had
# two consequences, and both are fixed here:
#
#   1. A receive chunk can end in the middle of a multi-byte character, so the
#      split produced a replacement character at the end of one "line" and
#      another at the start of the next - a purely client-side artefact that
#      looked exactly like device corruption.
#   2. Real corruption was indistinguishable from (1), because
#      Encoding.UTF8.GetString() substitutes U+FFFD instead of failing.
#
# So: accumulate to EndOfMessage, then decode the COMPLETE frame with a decoder
# that THROWS on invalid UTF-8. A frame is written as log lines only when it
# passes every check:
#
#   * decodes as strict UTF-8 (no replacement characters),
#   * contains no NUL byte (a printf-formatted log line never does),
#   * starts with an ANSI escape (0x1B) - every LOG_* and log_* frame does,
#     including the ones that carry no section tag.
#
# Anything else is reported as ONE "# BADFRAME" line carrying the length, a
# SHA-256 prefix and the first bytes in hex, and the raw bytes are appended to
# badframes_<ip>.bin so the frame can be analysed byte-exactly afterwards.
# A "# BADFRAME" in the output is therefore proof that the garbage came off the
# wire, with no decoding step in between that could have invented it.
#
# Background: field report on v3.2.02h where frames arrived whose HEAD was
# binary garbage (literal NUL bytes) while the TAIL was a correct log line with
# a matching timestamp - i.e. the payload was modified during copy/send.
# See Memory/ws_log_binary_garbage_torn_frames.md.
param(
    [string]$Ip = $(throw 'Ip required'),
    [string]$Out = "$PSScriptRoot\ws_$($Ip -replace '\.','_').log",
    [int]$RetrySec = 3,

    # Default 80, which is what every board serves. Only useful for pointing the
    # capturer at a local stand-in server when testing the capturer itself.
    [int]$Port = 80,

    # Bytes of a bad frame echoed as hex into the text log. The full frame always
    # goes to the .bin file, so this only controls how much is readable inline.
    [int]$BadHexBytes = 256
)
$ErrorActionPreference = 'Continue'

$hostPart = if ($Port -and $Port -ne 80) { "${Ip}:${Port}" } else { $Ip }
$uri = "ws://$hostPart/ws"

$fs = [System.IO.File]::Open($Out, 'Append', 'Write', 'ReadWrite')
$w  = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true

# Raw bytes of every rejected frame, so a capture can be re-examined without
# asking the user to reproduce. Opened lazily: a healthy run never creates it.
$badPath = [System.IO.Path]::Combine(
    [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($Out)),
    "badframes_$($Ip -replace '\.','_').bin")
$badFs = $null
$badW  = $null

# Strict: throws on a malformed sequence instead of substituting U+FFFD.
$strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
$ct = [System.Threading.CancellationToken]::None

$frames = 0
$bad    = 0
$bytes  = 0

Write-Host "capturing $uri -> $Out (auto-reconnect every ${RetrySec}s; Ctrl-C to stop)"

while ($true) {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    try {
        $ws.ConnectAsync([Uri]$uri, $ct).Wait()

        # A reconnect resets the per-connection counters: the summary is meant to
        # describe one session, and mixing sessions hides which one went bad.
        $frames = 0; $bad = 0; $bytes = 0
        $w.WriteLine("# ---- ws connected $(Get-Date -Format 'MM-dd HH:mm:ss') ----")

        $buf = New-Object byte[] 8192
        $ms  = New-Object System.IO.MemoryStream
        $connFaulted = $false

        while ($ws.State -eq 'Open') {
            $ms.SetLength(0)
            $msgType = $null

            # -- One complete message ------
            # Loop until EndOfMessage. Without this a multi-byte character can
            # straddle two chunks and produce U+FFFD that means nothing.
            while ($true) {
                $seg = New-Object System.ArraySegment[byte] -ArgumentList @(,$buf)

                # The client library VALIDATES UTF-8 on a Text frame and faults
                # when the payload is malformed - and it does so without handing
                # over the bytes, so the evidence is unreachable through this
                # API. Left unhandled that is the worst possible outcome: a
                # corrupt frame becomes an ordinary "ws dropped" and the
                # reconnect loop makes the capture look healthy.
                #
                # `$task.Result` is NOT usable for this: on a faulted or
                # cancelled task it yields $null here instead of throwing, with
                # the socket already Aborted - i.e. the failure would be
                # invisible. Inspect the task state explicitly.
                #
                # Field relevance: a torn frame off the wire is very likely to
                # be invalid UTF-8 exactly because it is a fragment. So this is
                # reported as its own event, not as a transport error.
                $task = $ws.ReceiveAsync($seg, $ct)
                try { [void]$task.Wait() } catch { }

                if ($task.IsFaulted) {
                    $inner = if ($task.Exception) { $task.Exception.GetBaseException() } else { $null }
                    $bad++
                    if ($inner -is [System.Net.WebSockets.WebSocketException]) {
                        # Build the template first: `"a" + "b" -f x, y` binds -f to
                        # the last literal only, which silently leaves the
                        # placeholders in the output.
                        $tpl = "# BADFRAME-STREAM {0:MM-dd HH:mm:ss.fff} reason=library rejected the payload ({1}); " +
                               "payload unavailable through ClientWebSocket, so no hex is recorded"
                        $w.WriteLine(($tpl -f (Get-Date), $inner.Message))
                    } else {
                        $connFaulted = $true
                        $w.WriteLine("# ---- receive failed: $(if($inner){$inner.GetType().Name}else{'unknown'}): $(if($inner){$inner.Message}) ----")
                    }
                    $msgType = 'Fault'
                    break
                }
                if ($task.IsCanceled) {
                    $connFaulted = $true
                    $w.WriteLine("# ---- receive cancelled (state=$($ws.State)) ----")
                    $msgType = 'Fault'
                    break
                }

                $res = $task.Result
                if ($res -eq $null) {
                    $connFaulted = $true
                    $w.WriteLine("# ---- receive returned no result (state=$($ws.State)) ----")
                    $msgType = 'Fault'
                    break
                }
                if ($res.MessageType -eq 'Close') { $msgType = 'Close'; break }

                $msgType = $res.MessageType
                if ($res.Count -gt 0) { $ms.Write($buf, 0, $res.Count) }

                if ($res.EndOfMessage) { break }

                # A log line is at most ~1.1 KB; a message that never ends means
                # the framing is broken. Drop what we have rather than growing
                # without bound, and let the validation below report it.
                if ($ms.Length -gt (1MB)) {
                    $w.WriteLine("# ---- message exceeded 1 MB, framing is broken; dropping partial frame ----")
                    $bad++
                    break
                }
            }
            if ($msgType -eq 'Close') { break }
            if ($msgType -eq 'Fault') {
                if (-not $connFaulted) { continue }   # a rejected payload: the socket may still be usable
                break
            }

            if ($ms.Length -eq 0) { continue }
            $bytes += $ms.Length
            $frames++

            $frame = $ms.ToArray()

            # -- Validate before decoding ------
            # Every one of these is cheap and each rules out a different failure
            # that would otherwise be silently "repaired" into plausible text.
            $why     = $null
            $hasNul  = [Array]::IndexOf($frame, [byte]0) -ge 0
            $hasEsc  = ($frame[0] -eq 0x1B)
            $decoded = $null

            if ($hasNul) {
                $why = 'NUL byte in payload'
            } elseif (-not $hasEsc) {
                $why = ("no leading ANSI escape (first byte 0x{0:X2})" -f $frame[0])
            } else {
                try {
                    $decoded = $strictUtf8.GetString($frame)
                } catch {
                    $why = 'invalid UTF-8'
                }
            }

            if ($why) {
                $bad++
                $sha = [System.Security.Cryptography.SHA256]::Create()
                try {
                    $hashHex = ([BitConverter]::ToString($sha.ComputeHash($frame)) -replace '-','').Substring(0,16)
                } finally { $sha.Dispose() }

                $n    = [Math]::Min($BadHexBytes, $frame.Length)
                $hex  = ([BitConverter]::ToString($frame, 0, $n) -replace '-',' ')
                $more = if ($n -lt $frame.Length) { " ... (+$($frame.Length - $n) B)" } else { '' }

                $w.WriteLine(("# BADFRAME {0:MM-dd HH:mm:ss.fff} len={1} sha256={2} reason={3}" -f `
                              (Get-Date), $frame.Length, $hashHex, $why))
                $w.WriteLine("#   hex: $hex$more")

                # Keep the bytes. Written with a length prefix so several frames
                # can share one file and still be separated later.
                if ($badFs -eq $null) {
                    $badFs = [System.IO.File]::Open($badPath, 'Append', 'Write', 'ReadWrite')
                    $badW  = New-Object System.IO.BinaryWriter($badFs)
                }
                $badW.Write([uint32]$frame.Length)
                $badW.Write($frame)
                # BinaryWriter has no AutoFlush; flush explicitly so a capture that
                # is killed mid-run still leaves its evidence on disk.
                $badW.Flush()
                continue
            }

            # -- Valid frame: emit as log lines ------
            # Strip SGR colour codes only; everything else is kept verbatim so a
            # later diff against the serial console stays meaningful.
            foreach ($line in ($decoded -split "`r?`n")) {
                if ($line.Trim()) {
                    $w.WriteLine(("{0:MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), ($line -replace "\x1b\[[0-9;]*m", '')))
                }
            }
        }
    } catch {
        $w.WriteLine("# ---- capture error: $($_.Exception.Message) ----")
    } finally {
        $ws.Dispose()
        if ($badW) { $badW.Dispose(); $badW = $null }
        if ($badFs) { $badFs.Dispose(); $badFs = $null }
    }

    # The summary is what makes a multi-board run comparable: "bad=0" on one
    # board and "bad=7" on another localises the problem immediately.
    $w.WriteLine(("# ---- ws dropped {0}, reconnecting (frames={1} bad={2} bytes={3}) ----" -f `
                  (Get-Date -Format 'MM-dd HH:mm:ss'), $frames, $bad, $bytes))
    Start-Sleep -Seconds $RetrySec
}
