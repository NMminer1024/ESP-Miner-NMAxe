import {HttpErrorResponse, HttpEventType} from '@angular/common/http';
import {Component, Input, OnDestroy, OnInit} from '@angular/core';
import {FormBuilder, FormGroup, Validators} from '@angular/forms';
import {ToastrService} from 'ngx-toastr';
import {Subscription} from 'rxjs';
import {startWith} from 'rxjs';
import {LoadingService} from 'src/app/services/loading.service';
import {SystemService} from 'src/app/services/system.service';
import {WebsocketService} from 'src/app/services/web-socket.service';
import {eASICModel} from 'src/models/enum/eASICModel';

@Component({
  selector: 'app-preferences',
  templateUrl: './preference.component.html',
  styleUrls: ['./preference.component.scss']
})
export class PreferenceComponent implements OnInit, OnDestroy {

  public form!: FormGroup;

  public devToolsOpen: boolean = false;
  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;
  public hwModel: string = '';
  public hasDualFan: boolean = false;
  public asicTempMin: number = 0;
  public asicTempMax: number = 80;
  public vcoreTempMin: number = 0;
  public vcoreTempMax: number = 100;

  public screensaverOptions = [
    { label: 'Never',    value: 0     },
    { label: '1 min',    value: 60    },
    { label: '5 min',    value: 300   },
    { label: '15 min',   value: 900   },
    { label: '1 hour',   value: 3600  },
    { label: '3 hours',  value: 10800 },
    { label: '24 hours', value: 86400 },
  ];

  // Screensaver GIF upload state
  public screensaverFile: File | null = null;
  public screensaverUploading: boolean = false;
  public screensaverProgress: number = 0;         // network upload %
  public screensaverDeviceProgress: number = 0;   // device SPIFFS write %
  private wsSub: Subscription | null = null;

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService,
    private wsService: WebsocketService
  ) {

    window.addEventListener('resize', this.checkDevTools);
    this.checkDevTools();

  }

  ngOnInit(): void {
    this.systemService.getSettingPreference(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        const flipscreen   = info.screenFlip    ?? info.flipscreen  ?? 0;
        const invertscreen = info.invertscreen  ?? 0;
        const ledindicator = info.ledIndicator  ?? info.ledindicator ?? 0;
        const brightness   = info.Brightness    ?? info.brightness  ?? 100;
        const autoscreen   = info.screenAutoRoll ?? info.autoscreen ?? 0;

        const screensaverEnable  = info.screensaverEnable  ?? 0;
        const screensaverTimeout = info.screensaverTimeout ?? 0;
        // compute dropdown value: 0 (never) when disabled or timeout==0, otherwise use the timeout
        const screensaverOption  = (screensaverEnable == 0 || screensaverTimeout == 0) ? 0 : screensaverTimeout;

        const asicFan  = info.fans?.find((f: any) => f.id === 0) ?? info.fans?.[0];
        const vcoreFan = info.fans?.find((f: any) => f.id === 1);
        this.hasDualFan   = !!vcoreFan;

        const autoasicfanspeed  = asicFan?.auto   ?? 1;
        const asictargettemp    = asicFan?.target ?? 30;
        const asicfanspeed      = asicFan?.speed  ?? 100;
        const autovcorefanspeed = vcoreFan?.auto   ?? 1;
        const vcoretargettemp   = vcoreFan?.target ?? 85;
        const vcorefanspeed     = vcoreFan?.speed  ?? 100;

        this.ASICModel = info.asic || info.ASICModel;
        this.hwModel   = info.hwModel ?? '';
        this.form = this.fb.group({
          flipscreen:         [flipscreen == 1],
          invertscreen:       [invertscreen == 1],
          ledindicator:       [ledindicator == 1],
          brightness:         [brightness, [Validators.required, Validators.min(1), Validators.max(100)]],
          autoscreen:         [autoscreen == 1],
          autoasicfanspeed:   [autoasicfanspeed == 1 || autoasicfanspeed === true, [Validators.required]],
          asictargettemp:     [parseFloat(String(asictargettemp)) || 30, [Validators.required, Validators.min(this.asicTempMin), Validators.max(this.asicTempMax)]],
          asicfanspeed:       [asicfanspeed, [Validators.required]],
          autovcorefanspeed:  [autovcorefanspeed == 1 || autovcorefanspeed === true, [Validators.required]],
          vcoretargettemp:    [parseFloat(String(vcoretargettemp)) || 85, [Validators.required, Validators.min(this.vcoreTempMin), Validators.max(this.vcoreTempMax)]],
          vcorefanspeed:      [vcorefanspeed, [Validators.required]],
          screensaverTimeout: [screensaverOption, [Validators.required]],
        });

        this.form.controls['autoasicfanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autoasicfanspeed'].value)
        ).subscribe(autoasic => {
          if (autoasic) {
            this.form.controls['asicfanspeed'].disable();
          } else {
            this.form.controls['asicfanspeed'].enable();
          }
        });

        this.form.controls['autovcorefanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autovcorefanspeed'].value)
        ).subscribe(autovcore => {
          if (autovcore) {
            this.form.controls['vcorefanspeed'].disable();
          } else {
            this.form.controls['vcorefanspeed'].enable();
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

  public get screensaverResolutionHint(): string {
    if (this.hwModel === 'NMAxe' || this.hwModel === 'NMAxeGamma') {
      return 'GIF: ≤300 KB, recommended 240×135 px (NMAxe / NMAxeGamma)';
    } else if (this.hwModel === 'NMQAxe++') {
      return 'GIF: ≤300 KB, recommended 320×240 px (NMQAxe++)';
    }
    return 'GIF: ≤300 KB, 240×135 (NMAxe/Gamma) · 320×240 (NMQAxe++)';
  }

  public getTemperatureSliderClass(): string {
    const temp = this.form?.get('asictargettemp')?.value || 30;
    if (temp <= 50) return 'temp-safe';
    else if (temp <= 60) return 'temp-normal';
    else if (temp <= 65) return 'temp-warm';
    else return 'temp-hot';
  }

  public getVcoreTemperatureSliderClass(): string {
    const temp = this.form?.get('vcoretargettemp')?.value || 85;
    if (temp <= 90) return 'temp-safe';
    else if (temp <= 100) return 'temp-normal';
    else if (temp <= 115) return 'temp-warm';
    else return 'temp-hot';
  }

  public updateSystem() {    const raw = this.form.getRawValue();

    const fans: any[] = [
      { id: 0, auto: raw.autoasicfanspeed ? 1 : 0, target: raw.asictargettemp, speed: raw.asicfanspeed }
    ];
    if (this.hasDualFan) {
      fans.push({ id: 1, auto: raw.autovcorefanspeed ? 1 : 0, target: raw.vcoretargettemp, speed: raw.vcorefanspeed });
    }

    const form: any = {
      Brightness:         raw.brightness,
      screenFlip:         raw.flipscreen   ? 1 : 0,
      invertscreen:       raw.invertscreen ? 1 : 0,
      ledIndicator:       raw.ledindicator ? 1 : 0,
      screenAutoRoll:     raw.autoscreen   ? 1 : 0,
      screensaverEnable:  raw.screensaverTimeout === 0 ? 0 : 1,
      screensaverTimeout: raw.screensaverTimeout,
      fans
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

  public onScreensaverFileSelected(event: Event): void {
    const input = event.target as HTMLInputElement;
    if (input.files && input.files.length > 0) {
      this.screensaverFile = input.files[0];
      this.uploadScreensaver();
    }
  }

  public uploadScreensaver(): void {
    if (!this.screensaverFile) return;
    this.screensaverUploading = true;
    this.screensaverProgress = 0;
    this.screensaverDeviceProgress = 0;

    // Subscribe to WebSocket to receive device-side SPIFFS write progress
    this.wsSub = this.wsService.connect().subscribe((msg: string) => {
      const m = msg.match(/screensaver upload:\s*(\d+)%/);
      if (m) {
        this.screensaverDeviceProgress = parseInt(m[1], 10);
      }
    });

    // Do NOT pipe through lockUIUntilComplete() — that operator shows a blocking
    // overlay for the entire lifetime of the observable.  A file upload emits
    // multiple UploadProgress events before completing, so the overlay would
    // cover the page the whole time.  We manage the busy state ourselves instead.
    this.systemService.uploadScreensaver(this.uri, this.screensaverFile)
      .subscribe({
        next: (event: any) => {
          if (event?.type === HttpEventType.UploadProgress && event.total) {
            this.screensaverProgress = Math.round(100 * event.loaded / event.total);
          } else if (event?.type === HttpEventType.Response) {
            this.screensaverUploading = false;
            this.screensaverFile = null;
            this.screensaverProgress = 0;
            this.screensaverDeviceProgress = 0;
            this.wsSub?.unsubscribe();
            this.wsSub = null;
            this.toastr.success('Screen saver uploaded!', 'Success');
          }
        },
        error: (err: HttpErrorResponse) => {
          this.screensaverUploading = false;
          this.screensaverProgress = 0;
          this.screensaverDeviceProgress = 0;
          this.wsSub?.unsubscribe();
          this.wsSub = null;
          this.toastr.error('Upload failed.', err.message);
        }
      });
  }

  ngOnDestroy(): void {
    this.wsSub?.unsubscribe();
  }

}
