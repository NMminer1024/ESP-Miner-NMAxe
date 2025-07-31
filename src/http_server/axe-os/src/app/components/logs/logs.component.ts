import {AfterViewChecked, Component, ElementRef, OnDestroy, OnInit, ViewChild} from '@angular/core';
import {interval, map, Observable, shareReplay, startWith, Subscription, switchMap} from 'rxjs';
import {SystemService} from 'src/app/services/system.service';
import {WebsocketService} from 'src/app/services/web-socket.service';
import {ISystemInfo} from 'src/models/ISystemInfo';

@Component({
  selector: 'app-logs',
  templateUrl: './logs.component.html',
  styleUrl: './logs.component.scss'
})
export class LogsComponent implements OnInit, OnDestroy, AfterViewChecked {

  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;
  public info$: Observable<ISystemInfo>;

  public logs: string[] = [];

  private websocketSubscription?: Subscription;

  constructor(
    private websocketService: WebsocketService,
    private systemService: SystemService
  ) {


    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(1));
        info.current = parseFloat((info.current / 1000).toFixed(1));
        info.coreVoltageActual = parseFloat((info.coreVoltageActual / 1000).toFixed(2));
        info.coreVoltage = parseFloat((info.coreVoltage / 1000).toFixed(2));
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
        this.logs.push(val);
        if (this.logs.length > 1000) {
          this.logs.shift();
        }
      }
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
      const hardwareModel = info.boardVersion || 'Unknown';
      
      // 生成文件名：硬件型号+日期时间.log
      const filename = `${hardwareModel} ${dateTimeStr}.log`;
      
      // 将所有日志内容合并为一个字符串，每行前面加上 ₿ 字符
      const logContent = this.logs.map(log => `₿ ${log}`).join('\n');
      
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
