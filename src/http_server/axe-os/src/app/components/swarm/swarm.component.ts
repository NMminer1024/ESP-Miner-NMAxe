import {HttpClient} from '@angular/common/http';
import {Component, OnInit, OnDestroy, Input} from '@angular/core';
import {ToastrService} from 'ngx-toastr';
import {Subscription, interval} from 'rxjs';
import {BehaviorSubject, startWith, switchMap} from 'rxjs';
import {HashSuffixPipe} from "../../pipes/hash-suffix.pipe";
import {DiffSuffixPipe} from "../../pipes/diff-suffix.pipe";

interface NMDevice {
  ip: string;
  BoardType: string;
  HashRate: string;
  Share: string;
  PoolDiff: string;
  NetDiff: string;
  LastDiff: string;
  BestDiff: string;
  Valid: number;
  Power: string;
  Temp: number;
  RSSI: number;
  FreeHeap: number;
  Version: string;
  Uptime: string;
  Lastseen: number;
}

interface SwarmSummary {
  HashRate: string;
  BestDiffSession: string;
  BestDiffEver: string;
}

@Component({
  selector: 'app-swarm',
  templateUrl: './swarm.component.html',
  styleUrls: ['./swarm.component.scss'],
  standalone: false
})
export class SwarmComponent implements OnInit, OnDestroy {

  public refresh$: BehaviorSubject<null> = new BehaviorSubject(null);

  public selectedAxeOs: any = null;
  public showEdit = false;
  public swarmData: NMDevice[] = [];
  public swarmSummary: SwarmSummary | undefined = undefined;

  public logs: string[] = [];

  private subscription: Subscription = new Subscription();

  public currentDate: string = '';
  public currentTime: string = '';
  private intervalId: any;

  @Input() uri = '';

  constructor(
    private http: HttpClient,
    private toastr: ToastrService
  ) {

  }

  ngOnInit(): void {

    this.updateTime();
    this.intervalId = setInterval(() => {
      this.updateTime();
    }, 1000);

    this.subscription = interval(10000).pipe(
      startWith(0),
      switchMap(() => {
        this.logs.push(`Request sent ${this.uri}`)
        return this.http.get<{ devices: NMDevice[] }>(`${this.uri}/api/swarm`);
      })
    ).subscribe(
      data => {
        this.swarmData = data.devices;
        this.swarmSummary = calculateSwarmSummary(this.swarmData);
        this.logs.push(`Request received ${this.uri}`);
      },
      error => console.error('Error fetching swarm data', error)
    );
  }

  ngOnDestroy(): void {
    if (this.subscription) {
      this.subscription.unsubscribe();
    }

    if (this.intervalId) {
      clearInterval(this.intervalId);
    }
  }

  private updateTime() {
    const now = new Date();
    this.currentDate = now.toLocaleDateString();
    this.currentTime = now.toLocaleTimeString();
  }

  public refresh() {
    this.refresh$.next(null);
  }

  public edit(axe: any) {
    this.selectedAxeOs = axe;
    this.showEdit = true;
  }

  public restart(axe: any) {
    if (axe.BoardType === 'NMAxe') {
      // this.toastr.info(`[v0.3]Trying to restart device at ${axe.ip}`, 'Info');
      this.http.post(`http://${axe.ip}/api/system/restart`, {}, {
        headers: {
          'Content-Type': 'text/plain'
        },
      }).subscribe(res => {

      });
      this.toastr.success('NMAxe restarted', 'Success');

    } else {
      this.toastr.warning('Warning!', 'Device is not NMAxe');
    }
  }

}

function calculateSwarmSummary(devices: NMDevice[]): SwarmSummary {
  return {
    HashRate: getHashRateSummary(devices),
    BestDiffSession: getBestDiffSession(devices),
    BestDiffEver: getBestDiffEver(devices)
  }
}

function getHashRateSummary(devices: NMDevice[]): string {
  let totalHashRate = 0;
  devices.forEach(device => {
    totalHashRate += HashSuffixPipe.revert(device.HashRate);
  });
  return HashSuffixPipe.transform(totalHashRate);
}

function getBestDiffSession(devices: NMDevice[]): string {
  const sessionDiffs = devices.map(device => device.BestDiff.slice(0, device.BestDiff.indexOf('\r')));
  return getBestDiffSummary(sessionDiffs);
}

function getBestDiffEver(devices: NMDevice[]): string {
  const sessionDiffs = devices.map(device => device.BestDiff.slice(device.BestDiff.indexOf('\r')));
  return getBestDiffSummary(sessionDiffs);
}

function getBestDiffSummary(diffs: string[]): string {
  let max = 0;
  diffs.forEach(item => {
    const diff = DiffSuffixPipe.revert(item);
    if (diff > max) {
      max = diff;
    }
  });
  return DiffSuffixPipe.transform(max);
}
