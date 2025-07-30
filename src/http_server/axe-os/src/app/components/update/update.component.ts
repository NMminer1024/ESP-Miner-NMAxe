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

  constructor(
    private systemService: SystemService,
    private toastrService: ToastrService,
    private githubUpdateService: GithubUpdateService
  ) {
    this.latestRelease$ = this.githubUpdateService.getReleases().pipe(map(releases => {
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
   * @returns true if latest > current
   */
  private compareVersions(current: string, latest: string): boolean {
    if (!current || !latest) return false;
    
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
        return true;
      } else if (latestParts[i] < currentParts[i]) {
        return false;
      }
    }
    
    return false; // 版本相同
  }

  /**
   * 检查是否有可用更新
   */
  private checkForUpdates() {
    if (this.latestRelease && this.currentInfo) {
      const latestVersion = this.latestRelease.name;
      const currentVersion = this.currentInfo.version;
      this.hasUpdate = this.compareVersions(currentVersion, latestVersion);
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
}
