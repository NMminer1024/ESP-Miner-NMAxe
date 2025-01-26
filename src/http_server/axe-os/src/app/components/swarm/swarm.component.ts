import {HttpClient} from '@angular/common/http';
import {Component, OnInit, OnDestroy, Input} from '@angular/core';
import {ToastrService} from 'ngx-toastr';
import {Subscription, interval} from 'rxjs';
import {BehaviorSubject, startWith, switchMap} from 'rxjs';
import {HashSuffixPipe} from "../../pipes/hash-suffix.pipe";
import {DiffSuffixPipe} from "../../pipes/diff-suffix.pipe";
import { DateAgoPipe } from '../../pipes/date-ago.pipe'; 

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

enum SortIndex {
  IP,
  BoardType,
  HashRate,
  Share,
  PoolDiff,
  NetDiff,
  LastDiff,
  BestDiff,
  Valid,
  Power,
  Temp,
  RSSI,
  FreeHeap,
  Version,
  UpTime,
  LastSeen
}

enum SortOrder {
  Asc,
  Desc
}

const StorageSwarmSortKey = "StorageSwarmSort";

interface StorageSwarmSort {
  index: SortIndex;
  order: SortOrder;
}

function setStorageSwarmSort(index: SortIndex, order: SortOrder) {
  localStorage.setItem(StorageSwarmSortKey, JSON.stringify({index, order}));
}

function getStorageSwarmSort(): StorageSwarmSort | null {
  const item = localStorage.getItem(StorageSwarmSortKey);
  if (item) {
    return JSON.parse(item);
  }
  return null;
}

const TableSortFunctions = {
  [SortIndex.IP]: (a: NMDevice, b: NMDevice) => a.ip.localeCompare(b.ip),
  [SortIndex.BoardType]: (a: NMDevice, b: NMDevice) => a.BoardType.localeCompare(b.BoardType),
  [SortIndex.HashRate]: (a: NMDevice, b: NMDevice) => HashSuffixPipe.revert(a.HashRate) - HashSuffixPipe.revert(b.HashRate),
  [SortIndex.Share]: (a: NMDevice, b: NMDevice) => parseFloat(a.Share.split('/')[1]) - parseFloat(b.Share.split('/')[1]),
  [SortIndex.PoolDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.PoolDiff) - DiffSuffixPipe.revert(b.PoolDiff),
  [SortIndex.NetDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.NetDiff) - DiffSuffixPipe.revert(b.NetDiff),
  [SortIndex.LastDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.LastDiff) - DiffSuffixPipe.revert(b.LastDiff),
  [SortIndex.BestDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.BestDiff) - DiffSuffixPipe.revert(b.BestDiff),
  [SortIndex.Valid]: (a: NMDevice, b: NMDevice) => a.Valid - b.Valid,
  [SortIndex.Power]: (a: NMDevice, b: NMDevice) => a.Power.localeCompare(b.Power),
  [SortIndex.Temp]: (a: NMDevice, b: NMDevice) => a.Temp - b.Temp,
  [SortIndex.RSSI]: (a: NMDevice, b: NMDevice) => a.RSSI - b.RSSI,
  [SortIndex.FreeHeap]: (a: NMDevice, b: NMDevice) => a.FreeHeap - b.FreeHeap,
  [SortIndex.Version]: (a: NMDevice, b: NMDevice) => a.Version.localeCompare(b.Version),
  [SortIndex.UpTime]: (a: NMDevice, b: NMDevice) => a.Uptime.localeCompare(b.Uptime),
  [SortIndex.LastSeen]: (a: NMDevice, b: NMDevice) => a.Lastseen - b.Lastseen,
}

@Component({
  selector: 'app-swarm',
  templateUrl: './swarm.component.html',
  styleUrls: ['./swarm.component.scss']
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
  public sortIndex: SortIndex | undefined = 0;
  public sortOrder: SortOrder = SortOrder.Asc;

  private intervalId: any;

  @Input() uri = '';

  constructor(
    private http: HttpClient,
    private toastr: ToastrService
  ) {

  }

  ngOnInit(): void {

    const storageSort = getStorageSwarmSort();
    if (storageSort) {
      this.sortIndex = storageSort.index;
      this.sortOrder = storageSort.order
    }
    this.updateTime();
    this.intervalId = setInterval(() => {
      this.updateTime();
    }, 1000);

    this.subscription = interval(3000).pipe(
      startWith(0),
      switchMap(() => {
        this.logs.push(`Request sent ${this.uri}`)
        return this.http.get<{ devices: NMDevice[] }>(`${this.uri}/api/swarm`);
      })
    ).subscribe(
      data => {
        this.swarmData = this.sort(data.devices);

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
    if (axe.BoardType.includes('NMAxe')) {
      this.http.post(`http://${axe.ip}/api/system/restart`, {}, {
        headers: {
          'Content-Type': 'text/plain'
        },
      }).subscribe(res => {

      });
      this.toastr.success('NMAxe restarted', 'Success');

    } else {
      this.toastr.warning('Warning!', 'Device is not NMAxe series');
    }
  }

  public sort(data: NMDevice[]): NMDevice[] {
    if (this.sortIndex === undefined) {
      return data;
    }
    let result = data.sort(TableSortFunctions[this.sortIndex]);
    if (this.sortOrder === SortOrder.Desc) {
      result = result.reverse();
    }
    return result;
  }

  public sortTable(index: SortIndex) {
    if (this.sortIndex === index) {
      this.sortOrder = this.sortOrder === SortOrder.Asc ? SortOrder.Desc : SortOrder.Asc;
    } else {
      this.sortOrder = SortOrder.Asc;
      this.sortIndex = index;
    }
    this.swarmData = this.sort(this.swarmData);
    setStorageSwarmSort(this.sortIndex, this.sortOrder);
  }

  protected readonly SortIndex = SortIndex;
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
