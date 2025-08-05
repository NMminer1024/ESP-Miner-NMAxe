import {HttpErrorResponse, HttpEventType} from '@angular/common/http';
import {Component, OnInit, ViewChild} from '@angular/core';
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
export class UpdateComponent implements OnInit {

  @ViewChild('otaFileUploader') otaFileUploader!: FileUpload;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;

  public latestRelease$: Observable<any>;
  public info$: Observable<any>;
  public latestRelease: any = null;
  public currentInfo: any = null;
  public hasUpdate: boolean = false;
  public versionStatus: 'behind' | 'current' | 'ahead' = 'current'; // 新增版本状态
  public recentReleases: any[] = []; // 最近的5个release版本
  public versionChain: any[] = []; // 版本链条
  public currentPositionInChain: number = -1; // 当前版本在链条中的位置

  constructor(
    private systemService: SystemService,
    private toastrService: ToastrService,
    private githubUpdateService: GithubUpdateService
  ) {
    this.latestRelease$ = this.githubUpdateService.getReleases().pipe(map(releases => {
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
        this.checkForUpdates();
      },
      error: (err) => {
        console.error('Failed to fetch latest release:', err);
      }
    });

    // 获取当前系统信息
    this.info$.subscribe({
      next: (info) => {
        this.currentInfo = info;
        this.checkForUpdates();
      },
      error: (err) => {
        console.error('Failed to fetch system info:', err);
      }
    });
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
        // 顺序：省略号 → 当前版本 → ... → 最新版本
        this.versionChain = [
          { type: 'ellipsis', version: '...', isCurrent: false },
          { type: 'current', version: cleanCurrentVersion, isCurrent: true },
          { type: 'gap', version: '→', isCurrent: false },
          // 反转数组，使最新版本在右侧
          ...this.recentReleases.slice(0, 5).reverse().map((release, index, array) => ({
            type: 'release',
            version: release.name.replace(/^(NMAxe-)?v?/, ''),
            isCurrent: false,
            isLatest: index === array.length - 1, // 最后一个（最新的）
            publishedAt: release.published_at
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
            publishedAt: release.published_at
          })),
          { type: 'gap', version: '→', isCurrent: false },
          { type: 'current', version: cleanCurrentVersion, isCurrent: true },
          { type: 'future', version: 'dev', isCurrent: false }
        ];
      }
    } else {
      // 当前版本在最近8个release中
      // 反转数组，使版本从低到高排列
      this.versionChain = this.recentReleases.slice().reverse().map((release, index, array) => ({
        type: 'release',
        version: release.name.replace(/^(NMAxe-)?v?/, ''),
        isCurrent: index === (array.length - 1 - currentIndex), // 调整索引
        isLatest: index === array.length - 1, // 最后一个是最新版本
        publishedAt: release.published_at,
        behindCount: currentIndex - (array.length - 1 - index)
      }));
    }
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
            this.firmwareUpdateProgress = Math.round((event.loaded / (event.total as number)) * 100);
          } else if (event.type === HttpEventType.Response) {
            if (event.ok) {
              this.toastrService.success('Firmware updated', 'Success!');
            } else {
              this.toastrService.error(event.statusText, 'Error');
            }
          }
        },
        error: (err) => {
          this.toastrService.error('Uploaded Error', 'Error');
          this.otaFileUploader.clear();
        },
        complete: () => {
          this.firmwareUpdateProgress = null;
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
            this.websiteUpdateProgress = Math.round((event.loaded / (event.total as number)) * 100);
          } else if (event.type === HttpEventType.Response) {
            if (event.ok) {
              setTimeout(() => {
                this.toastrService.success('Website updated', 'Success!');
                window.location.reload();
              }, 1000);
            } else {
              this.toastrService.error(event.statusText, 'Error');
            }
          }
        },
        error: (err) => {
          this.toastrService.error('Upload Error', 'Error');
          this.otaFileUploader.clear();
        },
        complete: () => {
          this.websiteUpdateProgress = null;
          this.otaFileUploader.clear();
        }
      });
  }

  /**
   * 下载指定版本的固件和网站文件
   * @param version 版本号
   */
  downloadVersionFiles(version: string) {
    // 清理版本号格式，确保匹配GitHub release格式
    const cleanVersion = version.replace(/^v?/, '');
    
    // 在最近的releases中查找匹配的版本
    const targetRelease = this.recentReleases.find(release => {
      const releaseVersion = release.name.replace(/^(NMAxe-)?v?/, '');
      return releaseVersion === cleanVersion;
    });

    if (!targetRelease) {
      this.toastrService.warning(`Release for version ${version} not found`, 'Download Failed');
      return;
    }

    // 查找firmware.bin和spiffs.bin文件
    const firmwareAsset = targetRelease.assets?.find((asset: any) => asset.name === 'firmware.bin');
    const spiffsAsset = targetRelease.assets?.find((asset: any) => asset.name === 'spiffs.bin');

    let downloadCount = 0;
    let totalDownloads = 0;

    if (firmwareAsset) totalDownloads++;
    if (spiffsAsset) totalDownloads++;

    if (totalDownloads === 0) {
      this.toastrService.error(`No downloadable files found for version ${version}`, 'Download Failed');
      return;
    }

    // 下载firmware.bin
    if (firmwareAsset) {
      this.downloadFile(firmwareAsset.browser_download_url, firmwareAsset.name);
      downloadCount++;
    }

    // 下载spiffs.bin
    if (spiffsAsset) {
      this.downloadFile(spiffsAsset.browser_download_url, spiffsAsset.name);
      downloadCount++;
    }

    // 显示下载提示
    this.toastrService.success(
      `Downloading ${downloadCount} file${downloadCount > 1 ? 's' : ''} for version ${version}`, 
      'Download Started'
    );
  }

  /**
   * 下载文件的辅助方法
   * @param url 下载链接
   * @param filename 文件名
   */
  private downloadFile(url: string, filename: string) {
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    link.target = '_blank';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }
}
