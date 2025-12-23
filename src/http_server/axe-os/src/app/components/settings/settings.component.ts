import {HttpErrorResponse} from '@angular/common/http';
import {Component} from '@angular/core';
import {FormBuilder, FormGroup, Validators} from '@angular/forms';
import {ToastrService} from 'ngx-toastr';
import {Observable, shareReplay, startWith} from 'rxjs';
import {LoadingService} from 'src/app/services/loading.service';
import {SystemService} from 'src/app/services/system.service';
import {eASICModel} from 'src/models/enum/eASICModel';

@Component({
  selector: 'app-settings',
  templateUrl: './settings.component.html',
  styleUrls: ['./settings.component.scss']
})
export class SettingsComponent {

  public form!: FormGroup;

  public devToolsOpen: boolean = false;
  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;

  public info$: Observable<any>;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService
  ) {

    window.addEventListener('resize', this.checkDevTools);
    this.checkDevTools();

    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}))

    this.info$.pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.ASICModel = info.ASICModel;
        this.form = this.fb.group({
          flipscreen: [info.flipscreen == 1],
          invertscreen: [info.invertscreen == 1],
          ledindicator: [info.ledindicator == 1],
          stratumURL1: [info.stratumURL1, [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/),
          ]],
          stratumURL2: [info.stratumURL2, [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/),
          ]],
          stratumUser1: [info.stratumUser1, [Validators.required]],
          stratumUser2: [info.stratumUser2, [Validators.required]],
          stratumPassword1:[info.stratumPassword1],
          stratumPassword2:[info.stratumPassword2],
          // timezone: [info.timezone, [Validators.required]],
          ssid: [info.ssid, [Validators.required]],
          wifiPass: ['password'],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          brightness: [info.brightness, [Validators.required]],
          autofanspeed: [info.autofanspeed == 1, [Validators.required]],
          autoscreen: [info.autoscreen == 1],
          fanspeed: [info.fanspeed, [Validators.required]],
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

  public updateSystem() {

    const form = this.form.getRawValue();

    form.frequency = parseInt(form.frequency);
    form.coreVoltage = parseInt(form.coreVoltage);

    // bools to ints
    form.flipscreen = form.flipscreen == true ? 1 : 0;
    form.invertscreen = form.invertscreen == true ? 1 : 0;
    form.autofanspeed = form.autofanspeed == true ? 1 : 0;
    form.ledindicator = form.ledindicator == true ? 1 : 0;
    form.autoscreen = form.autoscreen == true ? 1 : 0;

    if (form.wifiPass === 'password') {
      delete form.wifiPass;
    }

    this.systemService.updateSystem(undefined, form)
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

  public restart() {
    this.systemService.restart().subscribe(res => {

    });
    this.toastr.success('Success!', 'NMAxe restarted');
  }
}
