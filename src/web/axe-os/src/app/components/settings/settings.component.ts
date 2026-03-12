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
  // Available USDT pairs from Binance, as selectable options {label, value}.
  // label = base symbol (e.g. "BTC"), value = base symbol.
  public pairOptions: {label: string, value: string}[] = [];

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

    // Load available pairs from Binance to populate Market dropdowns.
    this.systemService.getAvailablePairs().subscribe(pairs => {
      this.pairOptions = pairs.map(p => {
        const base = p.endsWith('USDT') ? p.slice(0, -4) : p;
        return {label: base, value: base};
      });
    });

    this.info$.pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        // Map new field names to legacy names for backward compatibility
        info.ASICModel = info.asic || info.ASICModel;
        info.flipscreen = info.screenFlip ?? info.flipscreen;
        info.ledindicator = info.ledIndicator ?? info.ledindicator;
        
        // Parse new stratum nested structure
        if (info.stratum) {
          info.stratumURL1 = info.stratum.primary?.url || info.primaryUrl || info.stratumURL1 || '';
          info.stratumURL2 = info.stratum.fallback?.url || info.fallBackUrl || info.stratumURL2 || '';
          info.stratumUser1 = info.stratum.primary?.user || info.primaryUser || info.stratumUser1 || '';
          info.stratumUser2 = info.stratum.fallback?.user || info.fallBackUser || info.stratumUser2 || '';
          info.stratumPassword1 = info.stratum.primary?.pwd || info.primaryPassword || info.stratumPassword1 || '';
          info.stratumPassword2 = info.stratum.fallback?.pwd || info.fallBackPassword || info.stratumPassword2 || '';
        } else {
          // Fallback to old flat structure
          info.stratumURL1 = info.primaryUrl || info.stratumURL1 || '';
          info.stratumURL2 = info.fallBackUrl || info.stratumURL2 || '';
          info.stratumUser1 = info.primaryUser || info.stratumUser1 || '';
          info.stratumUser2 = info.fallBackUser || info.stratumUser2 || '';
          info.stratumPassword1 = info.primaryPassword || info.stratumPassword1 || '';
          info.stratumPassword2 = info.fallBackPassword || info.stratumPassword2 || '';
        }
        info.ssid = info.wifiSSID || info.ssid;
        info.coreVoltage = info.vcoreReq || info.coreVoltage;
        info.frequency = info.freqReq || info.frequency;
        info.brightness = info.Brightness ?? info.brightness;
        info.autofanspeed = info.fanAutoSpeed ?? info.autofanspeed;
        info.autoscreen = info.screenAutoRoll ?? info.autoscreen;
        if (info.fans && info.fans.length > 0) {
          const defaultFan = info.fans.find((f: any) => f.id === 0) || info.fans[0];
          info.fanspeed = defaultFan.speed;
        }
        
        this.ASICModel = info.ASICModel;
        
        // Parse saved watchlist (comma-separated string → string[])
        const watchlistArr: string[] = info.coinWatchlist
          ? (info.coinWatchlist as string).split(',').map((s: string) => s.trim()).filter(Boolean)
          : [];
        
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
          // Market settings
          coinDisplay: [info.coinPriceDisplay || info.coin || 'BTC'],
          coinWatchlist: [watchlistArr],
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

    // Convert watchlist array to comma-separated string for backend
    if (Array.isArray(form.coinWatchlist)) {
      form.coinWatchlist = form.coinWatchlist.join(',');
    }

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

  public saveMarket() {
    const raw = this.form.getRawValue();
    const payload: any = {
      coinDisplay: raw.coinDisplay,
      coinWatchlist: Array.isArray(raw.coinWatchlist)
        ? raw.coinWatchlist.join(',')
        : (raw.coinWatchlist || ''),
    };
    this.systemService.updateSystem(undefined, payload)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => { this.toastr.success('Success!', 'Saved.'); },
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
