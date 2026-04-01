import {HttpClient, HttpEventType} from '@angular/common/http';
import {Component, OnInit, OnDestroy, Input, ViewChild} from '@angular/core';
import {ToastrService} from 'ngx-toastr';
import {Subscription, interval, BehaviorSubject, startWith, catchError, EMPTY, of, timeout, switchMap} from 'rxjs';
import {ISystemInfo} from 'src/models/ISystemInfo';
import {HashSuffixPipe} from "../../pipes/hash-suffix.pipe";
import {DiffSuffixPipe} from "../../pipes/diff-suffix.pipe";
import {DateAgoPipe} from '../../pipes/date-ago.pipe';
import {FileUpload, FileUploadHandlerEvent} from "primeng/fileupload";
import {SystemService} from "../../services/system.service";

interface NMDevice {
  ip: string;
  Hostname: string;
  BoardType: string;
  PoolInUse: string;
  HashRate: string;
  Share: string;
  PoolDiff: string;
  NetDiff: string;
  LastDiff: string;
  BestDiff: string;
  Valid: number;
  Power: string;
  Temp: number;       // ASIC temp
  VcoreTemp: number;  // VCORE temp
  RSSI: number;
  FreeHeap: number;
  Version: string;
  Uptime: string;      // session uptime
  UptimeEver: string;  // ever uptime
  Lastseen: number;
  selected: boolean;
}

interface SwarmSummary {
  HashRate: string;
  BestDiffSession: string;
  BestDiffEver: string;
}

enum SortIndex {
  IP,
  Hostname,
  BoardType,
  PoolInUse,
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

interface UpdateDevice {
  ip: string;
  progress: string;
  result: boolean
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

// IP地址排序函数 - 将IP地址转换为数值进行比较
function ipToNumber(ip: string): number {
  const parts = ip.split('.').map(part => parseInt(part, 10));
  if (parts.length !== 4 || parts.some(part => isNaN(part) || part < 0 || part > 255)) {
    return 0; // 无效IP返回0
  }
  return (parts[0] << 24) + (parts[1] << 16) + (parts[2] << 8) + parts[3];
}

const TableSortFunctions = {
  [SortIndex.IP]: (a: NMDevice, b: NMDevice) => ipToNumber(a.ip) - ipToNumber(b.ip),
  [SortIndex.Hostname]: (a: NMDevice, b: NMDevice) => a.Hostname.localeCompare(b.Hostname),
  [SortIndex.BoardType]: (a: NMDevice, b: NMDevice) => a.BoardType.localeCompare(b.BoardType),
  [SortIndex.PoolInUse]: (a: NMDevice, b: NMDevice) => a.PoolInUse.localeCompare(b.PoolInUse),
  [SortIndex.HashRate]: (a: NMDevice, b: NMDevice) => HashSuffixPipe.revert(a.HashRate) - HashSuffixPipe.revert(b.HashRate),
  [SortIndex.Share]: (a: NMDevice, b: NMDevice) => parseFloat(a.Share.split('/')[1]) - parseFloat(b.Share.split('/')[1]),
  [SortIndex.PoolDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.PoolDiff) - DiffSuffixPipe.revert(b.PoolDiff),
  [SortIndex.NetDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.NetDiff) - DiffSuffixPipe.revert(b.NetDiff),
  [SortIndex.LastDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.LastDiff) - DiffSuffixPipe.revert(b.LastDiff),
  [SortIndex.BestDiff]: (a: NMDevice, b: NMDevice) => DiffSuffixPipe.revert(a.BestDiff.split("\r")[0]) - DiffSuffixPipe.revert(b.BestDiff.split("\r")[0]),
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
  @ViewChild('otaFileUploader') otaFileUploader!: FileUpload;
  @ViewChild('otaWebsiteUploader') otaWebsiteUploader!: FileUpload;

  public refresh$: BehaviorSubject<null> = new BehaviorSubject(null);

  public selectedAxeOs: any = null;
  public showEdit = false;
  public showUpdateWebsite = false;
  public showUpdateFirmware = false;

  public updateFirmwareDevices: UpdateDevice[] = [];
  public updateWebsiteDevices: UpdateDevice[] = [];

  public swarmData: NMDevice[] = [];
  public selectedItems: NMDevice[] = [];
  public swarmSummary: SwarmSummary | undefined = undefined;

  public logs: string[] = [];

  private subscription: Subscription = new Subscription();

  public currentDate: string = '';
  public currentTime: string = '';
  public sortIndex: SortIndex | undefined = 0;
  public sortOrder: SortOrder = SortOrder.Asc;

  public allSelected = false;

  // Filter properties
  public filterHighPower = false;
  public filterLowPower = false;

  private intervalId: any;

  // Swarm discovery state
  private deviceMap      = new Map<string, NMDevice>();
  private probeFailCount = new Map<string, number>();
  private blacklist      = new Set<string>();
  private lastContactMs  = new Map<string, number>();
  private knownAliveIps  = new Set<string>();

  @Input() uri = '';

  constructor(
    private http: HttpClient,
    private toastr: ToastrService,
    private systemService: SystemService,
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

    // Trigger a fresh backend scan when entering the swarm page.
    // Start polling only AFTER the POST response so the first /alive fetch sees
    // a clean backend state instead of leftover IPs from the previous session.
    this.http.post(`${this.uri}/api/swarm/scan`, {}).pipe(catchError(() => of(null))).subscribe(() => {
      // Clear all cached device state so the list rebuilds from the fresh scan
      this.deviceMap.clear();
      this.probeFailCount.clear();
      this.blacklist.clear();
      this.lastContactMs.clear();
      this.swarmData = [];
      this.swarmSummary = undefined;

      // Subscription 1: Refresh /alive every 10 s to update the IP candidate list.
      // New IPs get probed; IPs already in deviceMap get an immediate info refresh.
      // The blacklist is per-scan-cycle: reset it here so newly-alive IPs are re-probed.
      const aliveSubscription = interval(10000).pipe(startWith(0)).subscribe(() => {
        this.blacklist.clear();
        this.http.get<{ self: string; ips: string[] }>(`${this.uri}/alive`).pipe(
          catchError(() => of({ self: '', ips: [] as string[] }))
        ).subscribe(resp => {
          const ips = (resp.ips || []).filter((ip: string) => !!ip);
          this.knownAliveIps = new Set(ips);

          ips.forEach((ip: string) => {
            if (this.deviceMap.has(ip)) {
              this.fetchDeviceInfo(ip);
            } else {
              this.probeDevice(ip);
            }
          });

          // Evict devices not seen in alive list for > 5 minutes
          const now = Date.now();
          this.deviceMap.forEach((_, ip) => {
            if ((now - (this.lastContactMs.get(ip) || 0)) > 5 * 60 * 1000) {
              this.deviceMap.delete(ip);
              this.lastContactMs.delete(ip);
              this.probeFailCount.delete(ip);
            }
          });

          this.rebuildSwarmData();
        });
      });

      // Subscription 2: Refresh device info every 3 s for already-confirmed devices.
      const refreshSubscription = interval(3000).subscribe(() => {
        this.deviceMap.forEach((_, ip) => this.fetchDeviceInfo(ip));
      });

      // Subscription 3: Wake this device's screensaver every 5 s while the swarm page is open.
      const heartbeatSubscription = interval(5000).pipe(startWith(0)).subscribe(() => {
        this.systemService.wakeup().subscribe();
      });

      this.subscription.add(aliveSubscription);
      this.subscription.add(refreshSubscription);
      this.subscription.add(heartbeatSubscription);
    });
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
    if (axe.BoardType.includes('Axe')) {
      this.http.post(`http://${axe.ip}/api/system/restart`, {}, {
        headers: {
          'Content-Type': 'text/plain'
        },
      }).subscribe(res => {

      });
      this.toastr.success('Axe device restarted', 'Success');

    } else {
      this.toastr.warning('Warning!', 'Device is not Axe series');
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

  public toggleAll() {
    const filteredData = this.getFilteredData();
    filteredData.forEach(item => item.selected = this.allSelected);
    this.updateSelection();
  }

  updateSelection() {
    const filteredData = this.getFilteredData();
    this.allSelected = filteredData.length > 0 && filteredData.every(item => item.selected);
    this.selectedItems = this.swarmData.filter(item => item.selected);
  }

  public otaWWWUpdate(event: FileUploadHandlerEvent) {
    const file = event.files[0];
    if (file.name != 'spiffs.bin') {
      this.toastr.error('Incorrect file, looking for spiffs.bin', 'Error');
      this.otaWebsiteUploader.clear();
      return;
    }

    if (this.selectedItems.length === 0) {
      this.toastr.error('Please select at least one device', 'Error');
      return;
    }

    this.updateWebsiteDevices = this.selectedItems
      .map(item => {return {ip:item.ip, progress: "0%", result: true}})
      .sort((a, b) => ipToNumber(a.ip) - ipToNumber(b.ip));
    this.updateWebsiteDevices.forEach(item => {
      // Wake the screensaver on each target device first; only after the wakeup
      // response comes back do we start streaming the SPIFFS binary.
      this.systemService.wakeup(`http://${item.ip}`).pipe(
        switchMap(() => this.systemService.performWWWOTAUpdate(file, `http://${item.ip}`))
      ).subscribe({
        next: (event) => {
          if (event.type === HttpEventType.UploadProgress) {
            // this.toastr.info(`Website update progress: ${Math.round((event.loaded / (event.total as number)) * 100)}%`, 'Info');
            item.progress = `${Math.round((event.loaded / (event.total as number)) * 100)}%`;
          } else if (event.type === HttpEventType.Response) {
            if (event.ok) {
              setTimeout(() => {
                this.toastr.success('Website updated', 'Successed');
                item.progress = "Ok";
              }, 1000);
            } else {
              this.toastr.error(event.statusText, 'Error');
              item.progress = "Error";
              item.result = false;
            }
          }
        },
        error: (err) => {
          const reason = this.extractOtaError(err);
          this.toastr.error(`${item.ip}: ${reason}`, 'SPIFFS Update Failed');
          this.otaWebsiteUploader.clear();
          item.progress = "Error";
          item.result = false;
        }
      });
    })

  }

  public otaUpdate(event: FileUploadHandlerEvent) {
    const file = event.files[0];
    if (file.name != 'firmware.bin') {
      this.toastr.error('Incorrect file, looking for firmware.bin.', 'Error');
      this.otaFileUploader.clear();
      return;
    }
    if (this.selectedItems.length === 0) {
      this.toastr.error('Please select at least one device', 'Error');
      return;
    }

    this.updateFirmwareDevices = this.selectedItems
      .map(item => {return {ip:item.ip, progress: "0%", result: true}})
      .sort((a, b) => ipToNumber(a.ip) - ipToNumber(b.ip));

    this.updateFirmwareDevices.forEach(item => {
      // Wake the screensaver on each target device first; only after the wakeup
      // response comes back do we start streaming the firmware binary.
      this.systemService.wakeup(`http://${item.ip}`).pipe(
        switchMap(() => this.systemService.performOTAUpdate(file, `http://${item.ip}`))
      ).subscribe({
        next: (event) => {
          if (event.type === HttpEventType.UploadProgress) {
            // this.toastr.info(`Firmware update progress: ${Math.round((event.loaded / (event.total as number)) * 100)}%`, 'Info');
            item.progress = `${Math.round((event.loaded / (event.total as number)) * 100)}%`;
          } else if (event.type === HttpEventType.Response) {
            if (event.ok) {
              setTimeout(() => {
                this.toastr.success('Firmware updated', 'Successed');
                item.progress = "Ok";
              }, 1000);
            } else {
              this.toastr.error(event.statusText, 'Error');
              item.progress = "Error";
              item.result = false;
            }
          }
        },
        error: (err) => {
          const reason = this.extractOtaError(err);
          this.toastr.error(`${item.ip}: ${reason}`, 'Firmware Update Failed');
          this.otaFileUploader.clear();
          item.progress = "Error";
          item.result = false;
        }
      });
    });
  }

  // Filter methods
  private extractOtaError(err: any): string {
    if (err.error && typeof err.error === 'string' && err.error.trim().length > 0) {
      return err.error.trim();
    }
    if (err.status === 0) {
      return 'Connection lost \u2014 device may have rebooted';
    }
    if (err.status >= 500) {
      return `Server error (HTTP ${err.status})`;
    }
    return err.message || 'Unknown error';
  }

  // Filter methods
  public toggleHighPowerFilter() {
    this.filterHighPower = !this.filterHighPower;
    if (this.filterHighPower) {
      this.filterLowPower = false;
    }
  }

  public toggleLowPowerFilter() {
    this.filterLowPower = !this.filterLowPower;
    if (this.filterLowPower) {
      this.filterHighPower = false;
    }
  }

  public clearFilters() {
    this.filterHighPower = false;
    this.filterLowPower = false;
  }

  public getTempClass(temp: number): string {
    if (temp === 0) {
      return 'temp-na';
    } else if (temp < 60) {
      return 'temp-good';
    } else if (temp >= 60 && temp <= 75) {
      return 'temp-warning';
    } else {
      return 'temp-danger';
    }
  }

  public formatMultiLineData(data: string): string {
    if (!data) return '';
    // 替换 \r 或 \n 为 HTML 换行标签
    return data.replace(/[\r\n]/g, '\n');
  }

  public getFilteredData(): NMDevice[] {
    let filtered = this.swarmData;
    
    if (this.filterHighPower) {
      filtered = filtered.filter(device => this.isHighPower(device));
    } else if (this.filterLowPower) {
      filtered = filtered.filter(device => this.isLowPower(device));
    }
    
    return this.sort(filtered);
  }

  public isHighPower(device: NMDevice): boolean {
    const hashRate = HashSuffixPipe.revert(device.HashRate);
    return hashRate >= 1000000000; // >= 1GH/s
  }

  public isLowPower(device: NMDevice): boolean {
    const hashRate = HashSuffixPipe.revert(device.HashRate);
    return hashRate < 1000000000; // < 1GH/s
  }

  public getHighPowerCount(): number {
    return this.swarmData.filter(device => this.isHighPower(device)).length;
  }

  public getLowPowerCount(): number {
    return this.swarmData.filter(device => this.isLowPower(device)).length;
  }

  // Methods for filtered statistics
  public getFilteredHashRate(): number {
    const filteredData = this.getFilteredData();
    let totalHashRate = 0;
    filteredData.forEach(device => {
      totalHashRate += HashSuffixPipe.revert(device.HashRate);
    });
    return totalHashRate; // 返回原始H/s值
  }

  // 格式化算力显示
  public getFormattedHashRate(): { value: number, unit: string } {
    const hashRateInHS = this.getFilteredHashRate();
    
    if (hashRateInHS >= 1000000000000) { // >= 1TH/s
      return { value: hashRateInHS / 1000000000000, unit: 'TH/s' };
    } else if (hashRateInHS >= 1000000000) { // >= 1GH/s
      return { value: hashRateInHS / 1000000000, unit: 'GH/s' };
    } else if (hashRateInHS >= 1000000) { // >= 1MH/s
      return { value: hashRateInHS / 1000000, unit: 'MH/s' };
    } else if (hashRateInHS >= 1000) { // >= 1KH/s
      return { value: hashRateInHS / 1000, unit: 'KH/s' };
    } else {
      return { value: hashRateInHS, unit: 'H/s' };
    }
  }

  public getFilteredBestDiffSession(): string {
    const filteredData = this.getFilteredData();
    if (filteredData.length === 0) return '0';
    
    const sessionDiffs = filteredData.map(device => {
      // 处理 \r 或 \n 分隔符
      const parts = device.BestDiff.split(/[\r\n]/);
      return parts.length > 0 ? parts[0] : device.BestDiff;
    });
    return this.getBestDiffFromArray(sessionDiffs);
  }

  public getFilteredBestDiffEver(): string {
    const filteredData = this.getFilteredData();
    if (filteredData.length === 0) return '0';
    
    const everDiffs = filteredData.map(device => {
      // 处理 \r 或 \n 分隔符
      const parts = device.BestDiff.split(/[\r\n]/);
      return parts.length > 1 ? parts[1] : '0';
    });
    return this.getBestDiffFromArray(everDiffs);
  }

  private getBestDiffFromArray(diffs: string[]): string {
    let max = 0;
    diffs.forEach(item => {
      const diff = DiffSuffixPipe.revert(item);
      if (diff > max) {
        max = diff;
      }
    });
    return DiffSuffixPipe.transform(max);
  }

  public getTotalPower(): string {
    let total = 0;
    this.getFilteredData().forEach(device => {
      if (device.Power && device.Power !== '---') {
        const num = parseFloat(device.Power.replace(/[^0-9.]/g, ''));
        if (!isNaN(num)) {
          total += num;
        }
      }
    });
    return total.toFixed(1);
  }

  protected readonly SortIndex = SortIndex;

  // ── Swarm discovery helpers ────────────────────────────────────────────────

  /** Probe an unknown IP; register as NM device on success, blacklist after 3 failures. */
  private probeDevice(ip: string): void {
    this.http.get<any>(`http://${ip}/probe`).pipe(
      timeout(3000),
      catchError(() => {
        const n = (this.probeFailCount.get(ip) || 0) + 1;
        this.probeFailCount.set(ip, n);
        if (n >= 3) this.blacklist.add(ip);
        return EMPTY;
      })
    ).subscribe((data: any) => {
      if (data && data.model !== undefined) {
        // Confirmed NM device – reset counter and fetch full info
        this.probeFailCount.set(ip, 0);
        this.fetchDeviceInfo(ip);
      } else {
        const n = (this.probeFailCount.get(ip) || 0) + 1;
        this.probeFailCount.set(ip, n);
        if (n >= 3) this.blacklist.add(ip);
      }
    });
  }

  /** Fetch /api/system/info from a confirmed NM device and update the device map. */
  private fetchDeviceInfo(ip: string): void {
    this.http.get<ISystemInfo>(`http://${ip}/api/system/info`).pipe(
      timeout(5000),
      catchError(() => EMPTY)
    ).subscribe((info: ISystemInfo) => {
      const prev   = this.deviceMap.get(ip);
      const device = this.mapInfoToDevice(ip, info);
      if (prev) device.selected = prev.selected;
      this.deviceMap.set(ip, device);
      this.lastContactMs.set(ip, Date.now());
      this.rebuildSwarmData();
    });
  }

  /** Map /api/system/info response fields to the NMDevice interface. */
  private mapInfoToDevice(ip: string, info: ISystemInfo): NMDevice {
    const miner   = info.miner;
    const id      = info.identity;
    const pwr     = info.power;
    const temps   = info.temps;
    const stratum = info.stratum;

    const accepted = miner?.sAccepted ?? 0;
    const rejected = miner?.sRejected ?? 0;
    const total    = accepted + rejected;
    const rate     = total > 0 ? (accepted / total * 100).toFixed(1) : '0.0';

    return {
      ip,
      Hostname:   id?.hostName   ?? '',
      BoardType:  id?.hwModel    ?? '',
      PoolInUse:  stripStratumPrefix(stratum?.url ?? ''),
      HashRate:   HashSuffixPipe.transform((miner?.hashRate ?? 0) * 1e9),
      Share:      `${rejected}/${accepted}/${rate}%`,
      PoolDiff:   miner?.poolDiff      ?? '0',
      NetDiff:    miner?.networkDiff   ?? '0',
      LastDiff:   miner?.lastDiff      ?? '0',
      BestDiff:   `${miner?.bestDiffSession ?? '0'}\r${miner?.bestDiffEver ?? '0'}`,
      Valid:      miner?.blkhits        ?? 0,
      Power:      pwr   ? `${pwr.power.toFixed(1)}W` : '---',
      Temp:       temps?.asic   ?? 0,
      VcoreTemp:  temps?.vcore  ?? 0,
      RSSI:       id?.rssi      ?? 0,
      FreeHeap:   (miner?.freeHeap ?? 0) / 1024,
      Version:    id?.fwVersion ?? '',
      Uptime:     formatUptime(miner?.uptimeSeconds ?? 0),
      UptimeEver: formatUptime(miner?.uptimeEver    ?? 0),
      Lastseen:   Date.now(),   // ms timestamp, updated by rebuildSwarmData
      selected:   false,
    };
  }

  /** Format absolute-ms timestamp as human-readable last-seen string. */
  formatLastseen(ms: number): string {
    const diffSec = Math.floor((Date.now() - ms) / 1000);
    if (diffSec < 30)    return 'Just now';
    if (diffSec < 3600)  return `${Math.floor(diffSec / 60)}m ago`;
    if (diffSec < 86400) return `${Math.floor(diffSec / 3600)}h ago`;
    return `${Math.floor(diffSec / 86400)}d ago`;
  }

  /** Rebuild swarmData from deviceMap and update selection / Lastseen. */
  private rebuildSwarmData(): void {
    const alreadySelectedIPs = this.selectedItems.map(i => i.ip);
    const now     = Date.now();
    const devices = Array.from(this.deviceMap.values());
    devices.forEach(d => {
      d.selected = alreadySelectedIPs.includes(d.ip);
      d.Lastseen = this.lastContactMs.get(d.ip) || Date.now();
    });
    this.swarmData = this.sort(devices);
    if (this.swarmData.length > 0) {
      this.swarmSummary = calculateSwarmSummary(this.swarmData);
    }
  }
}

/** Strip stratum protocol prefix — keep only host:port */
function stripStratumPrefix(url: string): string {
  return url.replace(/^stratum\+\w+:\/\//, '');
}

function formatUptime(seconds: number): string {
  if (seconds <= 0) return '0000d 00h 00m';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  return `${String(d).padStart(4, '0')}d ${String(h).padStart(2, '0')}h ${String(m).padStart(2, '0')}m`;
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
