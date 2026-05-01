import {AfterViewChecked, Component, ElementRef, OnDestroy, OnInit, ViewChild} from '@angular/core';
import {interval, map, Observable, shareReplay, startWith, Subscription, switchMap} from 'rxjs';
import {SystemService} from 'src/app/services/system.service';
import {WebsocketService} from 'src/app/services/web-socket.service';
import {ISystemInfo} from 'src/models/ISystemInfo';
import {IRebootRecord} from 'src/models/IRebootRecord';
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

  // Tabs: 0 = Realtime Logs (default), 1 = Reboot History
  public activeTab: 0 | 1 = 0;

  // Reboot history state
  public rebootList: IRebootRecord[] = [];
  public rebootLoading = false;
  public rebootError: string | null = null;

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

  public exportReboots(): void {
    const payload = JSON.stringify(this.rebootList, null, 2);
    const blob = new Blob([payload], {type: 'application/json'});
    const url  = window.URL.createObjectURL(blob);
    const a    = document.createElement('a');
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    a.href     = url;
    a.download = `reboot-history-${stamp}.json`;
    document.body.appendChild(a); a.click();
    document.body.removeChild(a); window.URL.revokeObjectURL(url);
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
    // Only auto-scroll while the realtime-logs tab is visible.
    if (this.activeTab !== 0) return;
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
