import {HttpClient, HttpEventType} from '@angular/common/http';
import {Component, OnInit, OnDestroy, Input, ViewChild} from '@angular/core';
import {ToastrService} from 'ngx-toastr';
import {Subscription, interval} from 'rxjs';
import {BehaviorSubject, startWith, switchMap} from 'rxjs';
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
  Temp: number;
  RSSI: number;
  FreeHeap: number;
  Version: string;
  Uptime: string;
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

    this.subscription = interval(3000).pipe(
      startWith(0),
      switchMap(() => {
        this.logs.push(`Request sent ${this.uri}`)
        return this.http.get<{ devices: NMDevice[] }>(`${this.uri}/api/swarm`);
      })
    ).subscribe(
      data => {
        const alreadySelectedIPs = this.selectedItems.map(item => item.ip);

        data.devices.forEach(item => {
          item.selected = alreadySelectedIPs.includes(item.ip);
        });

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

    this.updateWebsiteDevices = this.selectedItems.map(item => {return {ip:item.ip, progress: "0%", result: true}});
    this.updateWebsiteDevices.forEach(item => {
      this.systemService.performWWWOTAUpdate(file, `http://${item.ip}`).subscribe({
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
          this.toastr.error('Upload Error', 'Error');
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

    this.updateFirmwareDevices  = this.selectedItems.map(item => {return {ip:item.ip, progress: "0%", result: true}});

    this.updateFirmwareDevices.forEach(item => {
      this.systemService.performOTAUpdate(file, `http://${item.ip}`).subscribe({
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
          this.toastr.error('Uploaded Error', 'Error');
          this.otaFileUploader.clear();
          item.progress = "Error";
          item.result = false;
        }
      });
    });
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
