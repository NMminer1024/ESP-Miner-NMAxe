import {HttpErrorResponse} from '@angular/common/http';
import {Component, Input, OnInit} from '@angular/core';
import {FormBuilder, FormGroup, Validators} from '@angular/forms';
import {ToastrService} from 'ngx-toastr';
import {startWith} from 'rxjs';
import {LoadingService} from 'src/app/services/loading.service';
import {SystemService} from 'src/app/services/system.service';
import {eASICModel} from 'src/models/enum/eASICModel';

@Component({
  selector: 'app-preferences',
  templateUrl: './preference.component.html',
  styleUrls: ['./preference.component.scss']
})
export class PreferenceComponent implements OnInit {

  public form!: FormGroup;

  public devToolsOpen: boolean = false;
  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService
  ) {

    window.addEventListener('resize', this.checkDevTools);
    this.checkDevTools();

  }

  ngOnInit(): void {
    this.systemService.getSettingPreference(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        const flipscreen    = info.screenFlip    ?? info.flipscreen    ?? 0;
        const invertscreen  = info.invertscreen  ?? 0;
        const ledindicator  = info.ledIndicator  ?? info.ledindicator  ?? 0;
        const brightness    = info.Brightness    ?? info.brightness    ?? 100;
        const autofanspeed  = info.fanAutoSpeed  ?? info.autofanspeed  ?? 1;
        const autoscreen    = info.screenAutoRoll ?? info.autoscreen   ?? 0;
        const targetAsicTemp = info.asicTargetTemp ?? info.targetAsicTemp ?? '70';

        if (info.fans && info.fans.length > 0) {
          const defaultFan = info.fans.find((f: any) => f.id === 0) || info.fans[0];
          info.fanspeed = defaultFan.speed;
        }

        this.ASICModel = info.asic || info.ASICModel;
        this.form = this.fb.group({
          flipscreen:      [flipscreen == 1],
          invertscreen:    [invertscreen == 1],
          ledindicator:    [ledindicator == 1],
          brightness:      [brightness, [Validators.required]],
          autofanspeed:    [autofanspeed == 1, [Validators.required]],
          targetAsicTemp:  [parseFloat(targetAsicTemp as string) || 70, [Validators.required, Validators.min(20), Validators.max(70)]],
          autoscreen:      [autoscreen == 1],
          fanspeed:        [info.fanspeed, [Validators.required]],
        });

        this.form.controls['autofanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autofanspeed'].value)
        ).subscribe(autofanspeed => {
          if (autofanspeed) {
            this.form.controls['fanspeed'].disable();
          } else {
            this.form.controls['fanspeed'].enable();
          }
        });
      });
  }


  private checkDevTools = () => {
    if (
      window.outerWidth - window.innerWidth > 160 ||
      window.outerHeight - window.innerHeight > 160
    ) {
      this.devToolsOpen = true;
    } else {
      this.devToolsOpen = false;
    }
  };

  public getTemperatureSliderClass(): string {
    const temp = this.form?.get('targetAsicTemp')?.value || 70;
    
    if (temp <= 50) {
      return 'temp-safe'; // 绿色 - 安全温度
    } else if (temp <= 60) {
      return 'temp-normal'; // 黄绿色 - 正常温度
    } else if (temp <= 65) {
      return 'temp-warm'; // 黄色 - 温暖温度
    } else {
      return 'temp-hot'; // 红色 - 高温警告
    }
  }

  public updateSystem() {
    const raw = this.form.getRawValue();

    // Convert booleans to ints for backend
    const form: any = {
      ...raw,
      flipscreen:   raw.flipscreen   ? 1 : 0,
      invertscreen: raw.invertscreen ? 1 : 0,
      ledindicator: raw.ledindicator ? 1 : 0,
      autofanspeed: raw.autofanspeed ? 1 : 0,
      autoscreen:   raw.autoscreen   ? 1 : 0,
    };

    this.systemService.patchSettingPreference(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }

}
