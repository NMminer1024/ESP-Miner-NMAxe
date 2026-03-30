import {HttpErrorResponse, HttpEventType} from '@angular/common/http';
import {Component, OnInit, ViewChild, AfterViewInit, ElementRef, ChangeDetectorRef} from '@angular/core';
import {ToastrService} from 'ngx-toastr';
import {FileUpload, FileUploadHandlerEvent} from 'primeng/fileupload';
import {map, Observable, shareReplay} from 'rxjs';
import {GithubUpdateService} from 'src/app/services/github-update.service';
import {SystemService} from 'src/app/services/system.service';
import { marked } from 'marked';

@Component({
  selector: 'app-update',
  templateUrl: './update.component.html',
  styleUrls: ['./update.component.scss']
})
export class UpdateComponent implements OnInit, AfterViewInit {

  @ViewChild('otaFileUploader') otaFileUploader!: FileUpload;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;

  private progressPollInterval: any = null;

  public latestRelease$: Observable<any>;
  public info$: Observable<any>;
  public latestRelease: any = null;
  public currentInfo: any = null;
  public hasUpdate: boolean = false;
  public versionStatus: 'behind' | 'current' | 'ahead' = 'current'; // 新增版本状态
  public recentReleases: any[] = []; // 最近的8个release版本
  public allReleases: any[] = []; // 所有的release版本
  public versionChain: any[] = []; // 版本链条
  public currentPositionInChain: number = -1; // 当前版本在链条中的位置
  public selectedRelease: any = null; // 当前选中显示的release
  public isLatestSelected: boolean = true; // 是否选中的是最新版本
  public isDevelopmentVersionSelected: boolean = false; // 是否选中的是开发版本

  constructor(
    private systemService: SystemService,
    private toastrService: ToastrService,
    private githubUpdateService: GithubUpdateService,
    private elementRef: ElementRef,
    private cdr: ChangeDetectorRef
  ) {
    this.latestRelease$ = this.githubUpdateService.getReleases().pipe(map(releases => {
      // 存储所有的release版本
      this.allReleases = releases;
      // 获取最近的8个release版本
      this.recentReleases = releases.slice(0, 8);
      return releases[0];
    }));

    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}));
  }

  ngOnInit() {
    // 自动获取最新版本信息
    this.latestRelease$.subscribe({
      next: (release) => {
        this.latestRelease = release;
        this.selectedRelease = release; // 默认显示最新版本
        this.checkForUpdates();
      },
      error: (err) => {
        console.error('Failed to fetch latest release:', err);
      }
    });

    // 获取当前系统信息
    this.info$.subscribe({
      next: (info) => {
        // Map new field names to legacy names for backward compatibility
        info.version      = (info.identity as any)?.fwVersion ?? info.fwVersion ?? info.version;
        info.boardVersion = (info.identity as any)?.hwModel   ?? info.hwModel   ?? info.boardVersion;
        
        this.currentInfo = info;
        this.checkForUpdates();
      },
      error: (err) => {
        console.error('Failed to fetch system info:', err);
      }
    });
  }

  ngAfterViewInit() {
    // 使用setTimeout确保DOM渲染完成后再执行
    setTimeout(() => {
      this.updateVersionChainClasses();
      // 监听窗口大小变化，重新计算
      window.addEventListener('resize', () => {
        setTimeout(() => this.updateVersionChainClasses(), 100);
      });
    }, 100);
  }

  /**
   * 更新版本链条节点的CSS类 - 现在使用HTML箭头元素，无需CSS类
   */
  private updateVersionChainClasses() {
    try {
      const versionNodes = this.elementRef.nativeElement.querySelectorAll('.version-node');
      if (!versionNodes.length) return;

      // 清理所有之前可能存在的连接相关类
      versionNodes.forEach((node: HTMLElement) => {
        node.classList.remove('in-line');
      });

      // 现在使用HTML .version-connector 元素，不需要添加任何类
    } catch (error) {
      console.warn('Error updating version chain classes:', error);
    }
  }

  /**
   * 比较版本号大小
   * @param current 当前版本号
   * @param latest 最新版本号
   * @returns 'behind' | 'current' | 'ahead'
   */
  private compareVersions(current: string, latest: string): 'behind' | 'current' | 'ahead' {
    if (!current || !latest) return 'current';
    
    // 移除版本号前缀 (如 "v" 或 "NMAxe-v")
    const cleanCurrent = current.replace(/^(NMAxe-)?v?/, '');
    const cleanLatest = latest.replace(/^(NMAxe-)?v?/, '');
    
    // 将版本号分割成数字数组
    const currentParts = cleanCurrent.split('.').map(part => {
      // 提取数字部分，忽略后缀字母
      const match = part.match(/^(\d+)/);
      return match ? parseInt(match[1], 10) : 0;
    });
    
    const latestParts = cleanLatest.split('.').map(part => {
      const match = part.match(/^(\d+)/);
      return match ? parseInt(match[1], 10) : 0;
    });
    
    // 补齐长度
    const maxLength = Math.max(currentParts.length, latestParts.length);
    while (currentParts.length < maxLength) currentParts.push(0);
    while (latestParts.length < maxLength) latestParts.push(0);
    
    // 逐位比较
    for (let i = 0; i < maxLength; i++) {
      if (latestParts[i] > currentParts[i]) {
        return 'behind'; // 当前版本落后
      } else if (latestParts[i] < currentParts[i]) {
        return 'ahead'; // 当前版本超前
      }
    }
    
    return 'current'; // 版本相同
  }

  /**
   * 检查是否有可用更新
   */
  private checkForUpdates() {
    if (this.latestRelease && this.currentInfo && this.recentReleases.length > 0) {
      const latestVersion = this.latestRelease.name;
      const currentVersion = this.currentInfo.version;
      this.versionStatus = this.compareVersions(currentVersion, latestVersion);
      this.hasUpdate = this.versionStatus === 'behind';
      
      // 构建版本链条
      this.buildVersionChain(currentVersion);
    }
  }

  /**
   * 构建版本链条，显示当前版本在release历史中的位置
   * 版本从低到高排列（左到右），箭头表示升级方向
   */
  private buildVersionChain(currentVersion: string) {
    // 清理当前版本号格式
    const cleanCurrentVersion = currentVersion.replace(/^(NMAxe-)?v?/, '');
    
    // 找到当前版本在release列表中的位置
    let currentIndex = -1;
    for (let i = 0; i < this.recentReleases.length; i++) {
      const releaseVersion = this.recentReleases[i].name.replace(/^(NMAxe-)?v?/, '');
      if (this.versionsEqual(cleanCurrentVersion, releaseVersion)) {
        currentIndex = i;
        break;
      }
    }
    
    this.currentPositionInChain = currentIndex;
    
    if (currentIndex === -1) {
      // 当前版本不在最近8个release中
      if (this.versionStatus === 'behind') {
        // 版本落后，可能落后很多版本
        // 顺序：省略号 → 当前版本 → 中间版本省略号 → 最近几个版本 → 最新版本
        this.versionChain = [
          { type: 'ellipsis', version: '...', isCurrent: false },
          { type: 'gap', version: '→', isCurrent: false },
          { type: 'current', version: cleanCurrentVersion, isCurrent: true },
          { type: 'gap', version: '→', isCurrent: false },
          { type: 'ellipsis', version: '...', isCurrent: false },
          { type: 'gap', version: '→', isCurrent: false },
          // 显示最近4个版本
          ...this.recentReleases.slice(0, 4).reverse().map((release, index, array) => ({
            type: 'release',
            version: release.name.replace(/^(NMAxe-)?v?/, ''),
            isCurrent: false,
            isLatest: index === array.length - 1, // 最后一个（最新的）
            publishedAt: release.published_at,
            releaseData: release
          }))
        ];
      } else if (this.versionStatus === 'ahead') {
        // 开发版本，超前于最新release
        // 顺序：旧版本 → ... → 最新版本 → 当前版本 → 未来
        this.versionChain = [
          // 反转数组，使最新版本在合适位置
          ...this.recentReleases.slice(0, 5).reverse().map((release, index, array) => ({
            type: 'release',
            version: release.name.replace(/^(NMAxe-)?v?/, ''),
            isCurrent: false,
            isLatest: index === array.length - 1, // 最后一个（最新的）
            publishedAt: release.published_at,
            releaseData: release
          })),
          { type: 'gap', version: '→', isCurrent: false },
          { type: 'current', version: cleanCurrentVersion, isCurrent: true },
          { type: 'future', version: 'dev', isCurrent: false }
        ];
      }
    } else {
      // 当前版本在最近8个release中
      if (currentIndex > 5) {
        // 如果当前版本落后超过5个版本，也添加省略号但保留一些中间版本
        // 顺序：省略号 → 当前版本 → 中间版本省略号 → 最近几个版本 → 最新版本
        this.versionChain = [
          { type: 'ellipsis', version: '...', isCurrent: false },
          { type: 'gap', version: '→', isCurrent: false },
          { type: 'current', version: cleanCurrentVersion, isCurrent: true },
          { type: 'gap', version: '→', isCurrent: false },
          { type: 'ellipsis', version: '...', isCurrent: false },
          { type: 'gap', version: '→', isCurrent: false },
          // 显示最近4个版本
          ...this.recentReleases.slice(0, 4).reverse().map((release, index, array) => ({
            type: 'release',
            version: release.name.replace(/^(NMAxe-)?v?/, ''),
            isCurrent: false,
            isLatest: index === array.length - 1, // 最后一个（最新的）
            publishedAt: release.published_at,
            releaseData: release
          }))
        ];
      } else {
        // 反转数组，使版本从低到高排列
        this.versionChain = this.recentReleases.slice().reverse().map((release, index, array) => ({
          type: 'release',
          version: release.name.replace(/^(NMAxe-)?v?/, ''),
          isCurrent: index === (array.length - 1 - currentIndex), // 调整索引
          isLatest: index === array.length - 1, // 最后一个是最新版本
          publishedAt: release.published_at,
          behindCount: currentIndex - (array.length - 1 - index),
          releaseData: release
        }));
      }
    }
    
    // 版本链条构建完成后，延迟更新CSS类
    setTimeout(() => {
      this.updateVersionChainClasses();
      this.cdr.detectChanges();
    }, 50);
  }

  /**
   * 检查两个版本号是否相等
   */
  private versionsEqual(version1: string, version2: string): boolean {
    const clean1 = version1.replace(/^(NMAxe-)?v?/, '');
    const clean2 = version2.replace(/^(NMAxe-)?v?/, '');
    return clean1 === clean2;
  }

  /**
   * 获取版本状态文本和图标
   */
  public getVersionStatusInfo(): {title: string, description: string, icon: string} {
    if (this.versionStatus === 'behind') {
      const behindCount = this.currentPositionInChain >= 0 ? this.currentPositionInChain : 'several';
      return {
        title: 'Update Available',
        description: `You are ${behindCount === 'several' ? 'several versions' : behindCount + ' version' + (behindCount > 1 ? 's' : '')} behind`,
        icon: 'pi-exclamation-triangle'
      };
    } else if (this.versionStatus === 'current') {
      return {
        title: 'You\'re Up to Date',
        description: 'You have the latest stable release',
        icon: 'pi-check-circle'
      };
    } else {
      return {
        title: 'Development Version',
        description: 'You\'re running a pre-release or development build',
        icon: 'pi-code'
      };
    }
  }

  /**
   * 选择并显示特定版本的信息
   * @param version 版本号
   */
  public selectVersionInfo(version: string) {
    // 清理版本号格式
    const cleanVersion = version.replace(/^v?/, '');
    
    // 检查是否选择的是开发版本（当前版本且处于超前状态）
    if (this.versionStatus === 'ahead' && this.currentInfo && cleanVersion === this.currentInfo.version.replace(/^(NMAxe-)?v?/, '')) {
      this.isDevelopmentVersionSelected = true;
      this.selectedRelease = null; // 开发版本没有对应的release
      this.isLatestSelected = false;
      return;
    }
    
    // 重置开发版本标志
    this.isDevelopmentVersionSelected = false;
    
    // 首先在所有releases中查找对应的release
    const targetRelease = this.allReleases.find(release => {
      const releaseVersion = release.name.replace(/^(NMAxe-)?v?/, '');
      return releaseVersion === cleanVersion;
    });

    if (targetRelease) {
      this.selectedRelease = targetRelease;
      this.isLatestSelected = targetRelease === this.latestRelease;
    } else {
      // 如果没找到，保持显示最新版本
      this.selectedRelease = this.latestRelease;
      this.isLatestSelected = true;
    }
  }

  /**
   * 格式化Release Notes
   */
  public formatReleaseNotes(body: string): string {
    if (!body) return '';
    
    // 使用marked进行标准Markdown解析（同步版本）
    const parsedHtml = marked(body, { async: false }) as string;
    
    // 对解析后的HTML进行一些自定义样式调整
    return parsedHtml
      // 为列表项添加自定义类
      .replace(/<ul>/g, '<ul class="release-list">')
      .replace(/<li>/g, '<li class="release-item">')
      // 为代码块添加自定义类
      .replace(/<code>/g, '<code class="inline-code">')
      // 为链接添加自定义类和target
      .replace(/<a href="/g, '<a class="release-link" target="_blank" href="')
      // 为标题添加自定义类
      .replace(/<h3>/g, '<h3 class="release-title">')
      .replace(/<h2>/g, '<h2 class="release-title">')
      .replace(/<h1>/g, '<h1 class="release-title">');
  }

  /**
   * 下载指定版本的固件和网站文件
   * @param version 版本号
   */
  downloadVersionFiles(version: string) {
    // 清理版本号格式，确保匹配GitHub release格式
    const cleanVersion = version.replace(/^v?/, '');
    
    console.log('Downloading files for version:', cleanVersion);
    console.log('Available releases:', this.recentReleases.map(r => r.name));
    
    // 在最近的releases中查找匹配的版本
    const targetRelease = this.recentReleases.find(release => {
      const releaseVersion = release.name.replace(/^(NMAxe-)?v?/, '');
      console.log(`Comparing ${releaseVersion} with ${cleanVersion}`);
      return releaseVersion === cleanVersion;
    });

    if (!targetRelease) {
      // 如果在最近的releases中没找到，尝试使用当前最新版本（可能是当前版本）
      console.warn(`Version ${cleanVersion} not found in recent releases, trying latest release`);
      const fallbackRelease = this.latestRelease;
      if (fallbackRelease) {
        this.downloadReleaseFiles(fallbackRelease, version);
      } else {
        this.toastrService.warning(`Release for version ${version} not found`, 'Download Failed');
      }
      return;
    }

    this.downloadReleaseFiles(targetRelease, version);
  }

  /**
   * 下载指定release的文件
   * @param release Release对象
   * @param version 显示用的版本号
   */
  private downloadReleaseFiles(release: any, version: string) {
    console.log('Release data:', release);
    console.log('Release assets:', release.assets);
    console.log('Release tag name:', release.tag_name);
    
    // 查找以版本号命名的zip压缩包
    const versionZipName = `${release.tag_name}.zip`;
    const zipAsset = release.assets?.find((asset: any) => asset.name === versionZipName);
    
    console.log('Looking for ZIP file:', versionZipName);
    console.log('Found ZIP asset:', zipAsset);

    if (!zipAsset) {
      this.toastrService.error(`ZIP file ${versionZipName} not found for version ${version}`, 'Download Failed');
      return;
    }

    // 获取板子版本信息，构建重命名的文件名
    const boardVersion = this.currentInfo?.boardVersion || 'Unknown';
    const renamedFileName = `${boardVersion}-${release.tag_name}.zip`;

    // 显示下载开始提示
    this.toastrService.success(
      `Downloading ${renamedFileName} for version ${version}`, 
      'Download Started'
    );

    // 下载zip压缩包并重命名
    this.downloadFile(zipAsset.browser_download_url, renamedFileName);
  }

  /**
   * 下载文件的辅助方法
   * @param url 下载链接
   * @param filename 文件名
   */
  private downloadFile(url: string, filename: string) {
    // 使用更简单直接的下载方法，避免CSP问题
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    link.target = '_blank';
    // 添加随机参数避免缓存
    const downloadUrl = url + (url.includes('?') ? '&' : '?') + '_t=' + Date.now();
    link.href = downloadUrl;
    
    // 设置样式确保不可见
    link.style.display = 'none';
    document.body.appendChild(link);
    
    // 触发下载
    link.click();
    
    // 立即移除链接
    document.body.removeChild(link);
    console.log(`Download triggered for ${filename}: ${downloadUrl}`);
  }

  otaUpdate(event: FileUploadHandlerEvent) {
    const file = event.files[0];

    if (file.name != 'firmware.bin') {
      this.toastrService.error('Incorrect file, looking for firmware.bin.', 'Error');
      this.otaFileUploader.clear();
      return;
    }

    this.systemService.performOTAUpdate(file)
      .subscribe({
        next: (event) => {
          if (event.type === HttpEventType.UploadProgress) {
            if (this.firmwareUpdateProgress === null) {
              this.firmwareUpdateProgress = 0;
              this.startProgressPolling('firmware');
            }
          } else if (event.type === HttpEventType.Response) {
            this.stopProgressPolling();
            if (event.ok) {
              this.firmwareUpdateProgress = 100;
              this.toastrService.success('Firmware updated', 'Success!');
              setTimeout(() => {
                this.firmwareUpdateProgress = null;
                this.otaFileUploader.clear();
              }, 2000);
            } else {
              this.toastrService.error(event.statusText, 'Error');
              this.firmwareUpdateProgress = null;
              this.otaFileUploader.clear();
            }
          }
        },
        error: (err) => {
          this.stopProgressPolling();
          const reason = this.extractOtaError(err);
          this.toastrService.error(reason, 'Firmware Update Failed');
          this.firmwareUpdateProgress = null;
          this.otaFileUploader.clear();
        },
        complete: () => {
          this.stopProgressPolling();
          this.otaFileUploader.clear();
        }
      });
  }

  otaWWWUpdate(event: FileUploadHandlerEvent) {
    const file = event.files[0];
    if (file.name != 'spiffs.bin') {
      this.toastrService.error('Incorrect file, looking for spiffs.bin.', 'Error');
      this.otaFileUploader.clear();
      return;
    }

    this.systemService.performWWWOTAUpdate(file)
      .subscribe({
        next: (event) => {
          if (event.type === HttpEventType.UploadProgress) {
            if (this.websiteUpdateProgress === null) {
              this.websiteUpdateProgress = 0;
              this.startProgressPolling('spiffs');
            }
          } else if (event.type === HttpEventType.Response) {
            this.stopProgressPolling();
            if (event.ok) {
              this.websiteUpdateProgress = 100;
              setTimeout(() => {
                this.websiteUpdateProgress = null;
                this.toastrService.success('Website updated', 'Success!');
                window.location.reload();
              }, 1000);
            } else {
              this.toastrService.error(event.statusText, 'Error');
              this.websiteUpdateProgress = null;
              this.otaFileUploader.clear();
            }
          }
        },
        error: (err) => {
          this.stopProgressPolling();
          const reason = this.extractOtaError(err);
          this.toastrService.error(reason, 'SPIFFS Update Failed');
          this.websiteUpdateProgress = null;
          this.otaFileUploader.clear();
        },
        complete: () => {
          this.stopProgressPolling();
          this.otaFileUploader.clear();
        }
      });
  }

  private extractOtaError(err: any): string {
    // err.error is the raw response body (responseType: 'text'), which is the
    // plain-text message sent by the ESP32 backend (e.g. "OTA write failed: MD5 check failed")
    if (err.error && typeof err.error === 'string' && err.error.trim().length > 0) {
      return err.error.trim();
    }
    // status 0 means the connection was dropped — device likely rebooted unexpectedly
    if (err.status === 0) {
      return 'Connection lost — device may have rebooted unexpectedly';
    }
    if (err.status >= 500) {
      return `Server error (HTTP ${err.status})`;
    }
    return err.message || 'Unknown error';
  }

  private startProgressPolling(target: 'firmware' | 'spiffs') {
    this.stopProgressPolling();
    this.progressPollInterval = setInterval(() => {
      this.systemService.getOtaProgress().subscribe({
        next: (p) => {
          if (target === 'firmware') {
            this.firmwareUpdateProgress = p.progress;
          } else {
            this.websiteUpdateProgress = p.progress;
          }
        }
      });
    }, 500);
  }

  private stopProgressPolling() {
    if (this.progressPollInterval) {
      clearInterval(this.progressPollInterval);
      this.progressPollInterval = null;
    }
  }
}
