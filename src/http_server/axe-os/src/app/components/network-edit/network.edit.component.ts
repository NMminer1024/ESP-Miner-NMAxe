import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { startWith } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-network-edit',
  templateUrl: './network.edit.component.html',
  styleUrls: ['./network.edit.component.scss']
})
export class NetworkEditComponent implements OnInit {

  public form!: FormGroup;
  public savedChanges: boolean = false;
  private originalFormValues: any = {};

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService
  ) {

  }
  ngOnInit(): void {
    this.systemService.getInfo(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.form = this.fb.group({
          hostname: [info.hostname, [Validators.required, Validators.maxLength(11)]],
          timezone: [(info as any).timezone || 0, [Validators.required, Validators.min(-12), Validators.max(14)]],
          ssid: [info.ssid, [Validators.required]],
          wifiPass: ['*****'],
        });

        // 保存原始值用于比较
        this.originalFormValues = {
          hostname: info.hostname,
          timezone: (info as any).timezone || 0,
          ssid: info.ssid,
          wifiPass: '*****'
        };

        // 监听表单变化，确保 dirty 状态正确更新
        this.form.valueChanges.subscribe(() => {
          // 当任何字段发生变化时，确保表单被标记为 dirty
          if (!this.form.dirty) {
            this.form.markAsDirty();
          }
        });

      });
  }

  // 检查表单是否真的有变化
  hasFormChanged(): boolean {
    if (!this.form || !this.originalFormValues) {
      return false;
    }
    
    const currentValues = this.form.getRawValue();
    
    // 检查除了 wifiPass 之外的字段
    const fieldsToCheck = ['hostname', 'timezone', 'ssid'];
    for (const field of fieldsToCheck) {
      if (currentValues[field] !== this.originalFormValues[field]) {
        return true;
      }
    }
    
    // 检查 WiFi 密码是否有变化（如果不是 '*****' 就说明用户修改了）
    if (currentValues.wifiPass !== '*****') {
      return true;
    }
    
    return false;
  }

  // 获取按钮提示信息
  getButtonTooltip(): string {
    if (!this.form) {
      return 'Loading...';
    }
    
    if (this.form.invalid) {
      return 'Please fix form validation errors before saving';
    }
    
    if (!this.form.dirty && !this.hasFormChanged()) {
      return 'No changes detected. Modify any field to enable saving';
    }
    
    return 'Click to save changes';
  }


  public updateSystem() {

    const form = this.form.getRawValue();

    // Allow an empty wifi password
    form.wifiPass = form.wifiPass == null ? '' : form.wifiPass;

    if (form.wifiPass === '*****') {
      delete form.wifiPass;
    }

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Saved.');
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
          this.savedChanges = false;
        }
      });
  }

  showWifiPassword: boolean = false;
  toggleWifiPasswordVisibility() {
    this.showWifiPassword = !this.showWifiPassword;
  }

  public restart() {
    this.systemService.restart()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Bitaxe restarted');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error', `Could not restart. ${err.message}`);
        }
      });
  }
}
