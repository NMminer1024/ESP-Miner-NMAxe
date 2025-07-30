import {HttpErrorResponse, HttpEventType} from '@angular/common/http';
import {Component, OnInit, ViewChild} from '@angular/core';
import {ToastrService} from 'ngx-toastr';
import {FileUpload, FileUploadHandlerEvent} from 'primeng/fileupload';
import {map, Observable, shareReplay} from 'rxjs';
import {GithubUpdateService} from 'src/app/services/github-update.service';
import {SystemService} from 'src/app/services/system.service';

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
      },
      error: (err) => {
        console.error('Failed to fetch latest release:', err);
      }
    });
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
