import {AfterViewChecked, Component, ElementRef, OnDestroy, OnInit, ViewChild} from '@angular/core';
import {interval, map, Observable, shareReplay, startWith, Subscription, switchMap} from 'rxjs';
import {SystemService} from 'src/app/services/system.service';
import {WebsocketService} from 'src/app/services/web-socket.service';
import {ISystemInfo} from 'src/models/ISystemInfo';
import {DomSanitizer, SafeHtml} from '@angular/platform-browser';

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

  private websocketSubscription?: Subscription;

  constructor(
    private websocketService: WebsocketService,
    private systemService: SystemService,
    private sanitizer: DomSanitizer
  ) {


    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      map(info => {
        // Map new field names to legacy names for backward compatibility
        info.temp = info.asicTemp;
        info.vrTemp = info.vcoreTemp;
        info.coreVoltage = info.vcoreReq;
        info.coreVoltageActual = info.vcoreActual;
        
        // Process numeric values
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(1));
        info.current = parseFloat((info.current / 1000).toFixed(1));
        info.coreVoltageActual = parseFloat((info.vcoreActual / 1000).toFixed(2));
        info.coreVoltage = parseFloat((info.vcoreReq / 1000).toFixed(2));
        return info;
      }),
      shareReplay({refCount: true, bufferSize: 1})
    );
  }

  ngOnInit(): void {
    // 进入页面自动开始日志输出
    this.startLogs();
  }

  ngOnDestroy(): void {
    // 离开页面停止日志输出
    this.stopLogs();
  }

  private startLogs(): void {
    this.websocketSubscription = this.websocketService.ws$.subscribe({
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
  }

  ngAfterViewChecked(): void {
    // 始终自动滚动到底部
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
      info.boardVersion = info.hwModel || info.boardVersion;
      
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
