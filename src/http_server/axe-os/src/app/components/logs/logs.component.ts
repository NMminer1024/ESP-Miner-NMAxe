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

  public showLogs = true; // 默认显示日志

  public stopScroll: boolean = false;

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
    if (this.stopScroll == true) {
      return;
    }
    if (this.scrollContainer?.nativeElement != null) {
      this.scrollContainer.nativeElement.scrollTo({
        left: 0,
        top: this.scrollContainer.nativeElement.scrollHeight,
        behavior: 'smooth'
      });
    }
  }

}
