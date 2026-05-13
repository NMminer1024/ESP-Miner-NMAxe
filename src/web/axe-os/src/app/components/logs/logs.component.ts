import {AfterViewChecked, Component, ElementRef, OnDestroy, OnInit, ViewChild} from '@angular/core';
import {interval, map, Observable, shareReplay, startWith, Subscription, switchMap} from 'rxjs';
import {SystemService} from 'src/app/services/system.service';
import {WebsocketService} from 'src/app/services/web-socket.service';
import {ISystemInfo} from 'src/models/ISystemInfo';
import {IRebootRecord} from 'src/models/IRebootRecord';
import {ICoredumpInfo} from 'src/models/ICoredumpInfo';
import {DomSanitizer, SafeHtml} from '@angular/platform-browser';
import {ToastrService} from 'ngx-toastr';

// ── Friendly-text translation tables for reboot history ────────────────────
// Keep these in sync with reboot_log.cpp::reboot_intent_str() / reset_reason_str().
const INTENT_LABEL: Record<string, string> = {
  none:                  '—',
  user_web_reboot:       'User clicked Restart',
  ota_finished:          'Firmware update finished',
  factory_reset:         'Factory reset',
  wifi_config_timeout:   'Wi-Fi setup timed out',
  wifi_reconnect_fail:   'Wi-Fi reconnection failed',
  low_hashrate:          'Hashrate too low',
  pool_timeout:          'Mining pool stopped responding',
  asic_frozen:           'ASIC stopped responding',
  config_changed:        'Configuration changed',
  selftest_triggered:    'Self-test',
  daemon_generic:        'Internal restart',
  overheat_vcore:        'VRM/Vcore overheated',
  overheat_asic:         'ASIC overheated',
  fan_stall:             'Fan stalled (low RPM)',
  power_low:             'Input power too low',
  ota_stall:             'OTA upload stalled',
  lcd_user_restart:      'User restarted via LCD',
  lcd_wifi_saved:        'Wi-Fi saved via on-screen wizard',
  force_config:          'Force-config (boot button held)',
};

const RESET_LABEL: Record<string, string> = {
  poweron:   'Cold power-on',
  ext_pin:   'External RST pin',
  sw:        'Software restart',
  panic:     'Firmware crash (Guru Meditation)',
  int_wdt:   'Interrupt watchdog timeout',
  task_wdt:  'Task watchdog timeout',
  wdt:       'Watchdog timeout',
  deepsleep: 'Wake from deep sleep',
  brownout:  'Power supply dropped (brown-out)',
  sdio:      'SDIO reset',
  unknown:   'Unknown reason',
};

const CLASS_META: Record<string, {label: string; icon: string; cssClass: string}> = {
  cold:       {label: 'Cold boot',     icon: '⚪', cssClass: 'cls-cold'},
  planned:    {label: 'Planned',       icon: '🟢', cssClass: 'cls-planned'},
  unknown_sw: {label: 'Unknown SW',    icon: '🟡', cssClass: 'cls-unknown'},
  crash:      {label: 'Crash',         icon: '🔴', cssClass: 'cls-crash'},
  power:      {label: 'Power issue',   icon: '🟠', cssClass: 'cls-power'},
  external:   {label: 'External',      icon: '🔵', cssClass: 'cls-external'},
};

@Component({
  selector: 'app-logs',
  templateUrl: './logs.component.html',
  styleUrl: './logs.component.scss'
})
export class LogsComponent implements OnInit, OnDestroy, AfterViewChecked {

  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;
  public info$: Observable<ISystemInfo>;

  public logs: SafeHtml[] = [];
  private rawLogs: string[] = []; // 保存原始日志用于下载

  // Auto-scroll toggle: when false the view stays at its current scroll position.
  public autoRefresh: boolean = true;

  // Tabs: 0 = Realtime Logs (default), 1 = Reboot History
  public activeTab: 0 | 1 = 0;

  // Reboot history state
  public rebootList: IRebootRecord[] = [];
  public rebootLoading = false;
  public rebootError: string | null = null;

  // Coredump state (Plan B). Polled lazily when entering tab 1.
  public coredump: ICoredumpInfo | null = null;
  public coredumpLoading = false;

  private websocketSubscription?: Subscription;

  constructor(
    private websocketService: WebsocketService,
    private systemService: SystemService,
    private sanitizer: DomSanitizer,
    private toastr: ToastrService
  ) {


    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      map(info => {
        // Hoist nested sub-objects for backward compatibility
        const pw  = (info as any).power as any;
        const tmp = (info as any).temps as any;
        const asc = (info as any).asic  as any;

        // power
        const rawPower = pw?.power ?? 0;
        const rawVbus  = pw?.vbus  ?? 0;
        const rawIbus  = pw?.ibus  ?? 0;
        info.power_w = parseFloat(rawPower.toFixed(1));
        info.voltage = parseFloat((rawVbus / 1000).toFixed(1));
        info.current = parseFloat((rawIbus / 1000).toFixed(1));

        // temp
        info.asicTemp  = tmp?.asic  ?? info.asicTemp;
        info.vcoreTemp = tmp?.vcore ?? info.vcoreTemp;
        info.temp   = info.asicTemp;
        info.vrTemp = info.vcoreTemp;

        // asic
        info.vcoreReq    = asc?.vcoreReq  ?? info.vcoreReq;
        info.vcoreActual = asc?.vcoreReal ?? info.vcoreActual;
        info.coreVoltageActual = parseFloat(((info.vcoreActual ?? 0) as number / 1000).toFixed(2));
        info.coreVoltage       = parseFloat(((info.vcoreReq    ?? 0) as number / 1000).toFixed(2));
        return info;
      }),
      shareReplay({refCount: true, bufferSize: 1})
    );
  }

  ngOnInit(): void {
    // Default tab is Realtime Logs; start the WebSocket immediately.
    this.startLogs();
  }

  ngOnDestroy(): void {
    // 离开页面停止日志输出
    this.stopLogs();
  }

  // ── Tabs ──────────────────────────────────────────────────────────────────
  public selectTab(idx: 0 | 1): void {
    if (this.activeTab === idx) return;
    this.activeTab = idx;
    if (idx === 1 && this.rebootList.length === 0 && !this.rebootLoading) {
      this.refreshReboots();
    }
    if (idx === 1 && this.coredump === null && !this.coredumpLoading) {
      this.refreshCoredump();
    }
  }

  // ── Reboot history actions ────────────────────────────────────────────────
  public refreshReboots(): void {
    this.rebootLoading = true;
    this.rebootError   = null;
    this.systemService.getRebootList().subscribe({
      next: list => {
        this.rebootList    = Array.isArray(list) ? list : [];
        this.rebootLoading = false;
      },
      error: err => {
        this.rebootError   = 'Failed to load reboot history';
        this.rebootLoading = false;
        // eslint-disable-next-line no-console
        console.error('[reboot] list error', err);
      }
    });
  }

  public clearReboots(): void {
    if (!confirm('Clear all reboot history records?')) return;
    this.systemService.clearRebootList().subscribe({
      next: () => {
        this.rebootList = [];
        this.toastr.success('Reboot history cleared', 'OK');
      },
      error: () => this.toastr.error('Failed to clear history', 'Error')
    });
  }

  // ── Coredump actions ───────────────────────────────────────────────────────────────────────────

  // Cached firmware/host so we can name the download file consistently,
  // and the rendered summary text shown in the read-only textarea.
  public coredumpSummary: string = '';
  public coredumpCopied  = false;        // brief check-mark on copy success
  public coredumpDownloading = false;
  private fwVersion: string = 'unknown';
  private hostName : string = 'device';

  public refreshCoredump(): void {
    this.coredumpLoading = true;
    this.systemService.getCoredumpInfo().subscribe({
      next: info => {
        this.coredump        = info ?? {present: false};
        this.coredumpLoading = false;
        if (this.coredump.present) this.refreshCoredumpSummary();
        else                       this.coredumpSummary = '';
      },
      error: () => {
        this.coredump        = {present: false};
        this.coredumpLoading = false;
        this.coredumpSummary = '';
      }
    });
  }

  // Pull host + fwVersion from /api/system/info, then build the textarea
  // contents. Done as a separate request so the coredump card paints quickly
  // on slow networks; on failure we still show a partial summary.
  private refreshCoredumpSummary(): void {
    const cd = this.coredump;
    if (!cd?.present) { this.coredumpSummary = ''; return; }
    this.systemService.getInfo().subscribe({
      next: (info: any) => {
        this.fwVersion = String(info?.identity?.fwVersion ?? info?.fwVersion ?? info?.version ?? 'unknown');
        this.hostName  = String(info?.identity?.hostName  ?? info?.hostName  ?? info?.hostname ?? 'device');
        this.coredumpSummary = this.buildCoredumpText(cd, this.fwVersion);
      },
      error: () => {
        this.coredumpSummary = this.buildCoredumpText(cd, this.fwVersion);
      }
    });
  }

  // Toggle for the custom Clear-coredump confirmation modal. We use a real
  // modal (not window.confirm) so we can remind the user to keep a local
  // backup before erasing.
  public showClearConfirm = false;

  public clearCoredump(): void {
    if (!this.coredump?.present) return;
    this.showClearConfirm = true;
  }

  public confirmClearCoredump(): void {
    this.showClearConfirm = false;
    if (!this.coredump?.present) return;
    this.systemService.clearCoredump().subscribe({
      next: () => {
        this.coredump        = {present: false};
        this.coredumpSummary = '';
        this.toastr.success('Coredump erased', 'OK');
      },
      error: () => this.toastr.error('Failed to erase coredump', 'Error')
    });
  }

  public cancelClearCoredump(): void {
    this.showClearConfirm = false;
  }

  // Save the rendered crash summary (the same text shown in the textarea)
  // as a .log file. The full ELF coredump is intentionally not exposed:
  // every actionable field (PC, backtrace, task, app SHA256) is already in
  // the summary, and a plain-text file is far easier to email / paste into
  // a GitHub issue.
  public downloadCoredump(): void {
    if (!this.coredump?.present || !this.coredumpSummary || this.coredumpDownloading) return;
    this.coredumpDownloading = true;
    try {
      const blob = new Blob([this.coredumpSummary], {type: 'text/plain;charset=utf-8'});
      this.saveBlob(blob, this.makeFilename('log'));
      this.toastr.success('Crash summary saved', 'OK');
    } catch {
      this.toastr.error('Failed to save crash summary', 'Error');
    } finally {
      this.coredumpDownloading = false;
    }
  }

  // Copy the textarea content (parsed summary) to the clipboard. Shows a
  // brief check-mark on the GitHub-style icon button.
  public copySummary(): void {
    if (!this.coredumpSummary) return;
    this.writeToClipboard(this.coredumpSummary).then(ok => {
      if (ok) {
        this.coredumpCopied = true;
        this.toastr.success(`Copied ${this.coredumpSummary.length} chars`, 'OK');
        setTimeout(() => this.coredumpCopied = false, 1500);
      } else {
        this.toastr.error('Clipboard write blocked by browser', 'Error');
      }
    });
  }

  // Build a sanitized "<host>-<fw>-yyyy-MM-dd-HH-mm-ss.<ext>" filename.
  private makeFilename(ext: string): string {
    const safe = (s: string) => s.replace(/[\\/:*?"<>|\s]+/g, '_').replace(/^_+|_+$/g, '') || 'x';
    const d  = new Date();
    const pad = (n: number) => String(n).padStart(2, '0');
    const stamp = `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}-` +
                  `${pad(d.getHours())}-${pad(d.getMinutes())}-${pad(d.getSeconds())}`;
    return `${safe(this.hostName)}-${safe(this.fwVersion)}-${stamp}.${ext}`;
  }

  // Trigger a browser download for an arbitrary Blob.
  private saveBlob(blob: Blob, filename: string): void {
    const url = window.URL.createObjectURL(blob);
    const a   = document.createElement('a');
    a.href = url; a.download = filename;
    document.body.appendChild(a); a.click();
    document.body.removeChild(a); window.URL.revokeObjectURL(url);
  }

  // Async clipboard with a textarea fallback for non-secure (http) contexts.
  private writeToClipboard(text: string): Promise<boolean> {
    if (navigator.clipboard && window.isSecureContext) {
      return navigator.clipboard.writeText(text).then(() => true, () => false);
    }
    return new Promise<boolean>(resolve => {
      try {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.left = '-9999px';
        document.body.appendChild(ta);
        ta.select();
        const ok = document.execCommand('copy');
        document.body.removeChild(ta);
        resolve(ok);
      } catch { resolve(false); }
    });
  }

  // Render the coredump summary into a multi-line plain-text block, mirroring
  // what the user sees on the card. Useful for pasting into bug reports.
  private buildCoredumpText(cd: ICoredumpInfo, fwVersion?: string): string {
    const lines: string[] = [];
    lines.push('=== ESP32 Coredump Summary ===');
    if (cd.crcOk === false) {
      lines.push('[WARNING] CRC check failed: dump may be truncated (common with TWDT crashes).');
      lines.push('[WARNING] Backtrace addresses below may be incomplete or incorrect.');
    }
    lines.push(`Firmware   : ${fwVersion || '(unknown)'}`);
    if (cd.size != null)        lines.push(`Size       : ${cd.size} bytes`);
    if (cd.task)                lines.push(`Crash Task : ${cd.task}`);
    if (cd.pc != null)          lines.push(`PC         : ${this.formatAddr(cd.pc)}`);
    if (cd.appSha256)           lines.push(`App SHA256 : ${cd.appSha256}`);
    if (cd.bt && cd.bt.length) {
      lines.push('');
      lines.push(`Backtrace${cd.btCorrupted ? ' (possibly partial/corrupted)' : ''}:`);
      lines.push(cd.bt.map(a => this.formatAddr(a)).join(' '));
    } else {
      lines.push('Backtrace  : (none captured)');
    }
    return lines.join('\n');
  }

  // Format a 32-bit address as the conventional 0x........ hex string.
  public formatAddr(v: number | undefined): string {
    if (v == null) return '—';
    return '0x' + (v >>> 0).toString(16).padStart(8, '0');
  }

  // ── Reboot field formatters (used by template) ────────────────────────────
  public formatTimestamp(rec: IRebootRecord): string {
    if (!rec.ts) return '—';
    const d = new Date(rec.ts * 1000);
    const pad = (n: number) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())} ` +
           `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
  }

  public formatUptime(sec: number): string {
    if (!sec || sec < 0) return '—';
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  }

  public formatHeap(bytes: number): string {
    if (!bytes) return '—';
    if (bytes >= 1024) return `${(bytes/1024).toFixed(1)} KB`;
    return `${bytes} B`;
  }

  public classMeta(cls: string) {
    return CLASS_META[cls] ?? CLASS_META['external'];
  }

  // Compose a single human-readable "reason" string for the table cell,
  // preferring intent (planned restarts) and falling back to the reset_reason
  // (crashes / power issues) when no intent was recorded.
  public reasonText(rec: IRebootRecord): string {
    if (rec.intent && rec.intent !== 'none' && INTENT_LABEL[rec.intent]) {
      const base = INTENT_LABEL[rec.intent];
      return rec.detail ? `${base} — ${rec.detail}` : base;
    }
    const base = RESET_LABEL[rec.reset] ?? rec.reset;
    return rec.detail ? `${base} — ${rec.detail}` : base;
  }

  // ── Logs ──────────────────────────────────────────────────────────────────
  private startLogs(): void {
    // connect() creates a fresh WebSocketSubject and opens the connection
    this.websocketSubscription = this.websocketService.connect().subscribe({
      next: (val) => {
        // 移除ANSI控制符用于下载
        const cleanText = this.removeAnsiCodes(val);
        this.rawLogs.push(cleanText); 
        
        // 转换ANSI为HTML用于显示
        const htmlText = this.convertAnsiToHtml(val);
        const safeHtml = this.sanitizer.bypassSecurityTrustHtml(htmlText);
        this.logs.push(safeHtml); 
        
        if (this.logs.length > 1000) {
          this.logs.shift();
          this.rawLogs.shift();
        }
      }
    });
  }

  private removeAnsiCodes(text: string): string {
    // 移除所有ANSI控制序列，只保留纯文本
    return text.replace(/\x1b\[\d+m/g, '');
  }

  private convertAnsiToHtml(text: string): string {
    // ANSI 颜色代码映射
    const colorMap: { [key: string]: string } = {
      '31': '#ff4444',     // 红色 - 错误
      '32': '#058005ff',     // 绿色 - 信息
      '33': '#be9c05ff',     // 黄色 - 警告
      '36': '#027691ff',     // 青色 - 调试
    };

    // 替换ANSI控制序列为HTML标签
    return text.replace(/\x1b\[(\d+)m/g, (match, code) => {
      if (code === '0') {
        return '</span>';
      } else if (colorMap[code]) {
        return `<span style="color: ${colorMap[code]}; font-weight: bold;Font-family: inherit">`;
      }
      return '';
    });
  }

  private stopLogs(): void {
    this.websocketSubscription?.unsubscribe();
    // complete() closes the underlying WebSocket — triggers WS_EVT_DISCONNECT on device
    this.websocketService.disconnect();
  }

  ngAfterViewChecked(): void {
    // Only auto-scroll while the realtime-logs tab is visible and autoRefresh is on.
    if (this.activeTab !== 0 || !this.autoRefresh) return;
    if (this.scrollContainer?.nativeElement != null) {
      this.scrollContainer.nativeElement.scrollTo({
        left: 0,
        top: this.scrollContainer.nativeElement.scrollHeight,
        behavior: 'smooth'
      });
    }
  }

  saveLog(): void {
    // 获取当前日期和时间并格式化为 YYYY-MM-DD HH:mm:ss
    const now = new Date();
    const year = now.getFullYear();
    const month = String(now.getMonth() + 1).padStart(2, '0');
    const day = String(now.getDate()).padStart(2, '0');
    const hours = String(now.getHours()).padStart(2, '0');
    const minutes = String(now.getMinutes()).padStart(2, '0');
    const seconds = String(now.getSeconds()).padStart(2, '0');
    const dateTimeStr = `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
    
    // 获取硬件型号
    this.systemService.getInfo().subscribe(info => {
      // Map new field names to legacy names for backward compatibility
      info.boardVersion = (info.identity as any)?.hwModel ?? info.hwModel ?? info.boardVersion;
      
      const hardwareModel = info.boardVersion || 'Unknown';
      
      // 生成文件名：硬件型号+日期时间.log
      const filename = `${hardwareModel} ${dateTimeStr}.log`;
      
      // 将所有日志内容合并为一个字符串
      const logContent = this.rawLogs.join('\n');
      
      // 创建 Blob 对象
      const blob = new Blob([logContent], { type: 'text/plain' });
      
      // 创建下载链接
      const url = window.URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = filename;
      
      // 触发下载
      document.body.appendChild(link);
      link.click();
      
      // 清理
      document.body.removeChild(link);
      window.URL.revokeObjectURL(url);
    });
  }

}
