import {HttpErrorResponse} from '@angular/common/http';
import {Component} from '@angular/core';
import {AbstractControl, FormBuilder, FormGroup, ValidationErrors, Validators} from '@angular/forms';
import {ToastrService} from 'ngx-toastr';
import {LoadingService} from 'src/app/services/loading.service';
import {SystemService} from 'src/app/services/system.service';

@Component({
  selector: 'app-settings',
  templateUrl: './settings.component.html',
  styleUrls: ['./settings.component.scss']
})
export class SettingsComponent {

  public form!: FormGroup;
  // Available USDT pairs from Binance, as selectable options {label, value}.
  public pairOptions: {label: string, value: string}[] = [];

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {
    // Load market settings (includes available pairs) to build the market card form.
    this.systemService.getSettingMarket()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(market => {
        if (Array.isArray(market.pairs)) {
          this.pairOptions = (market.pairs as string[]).map(p => {
            const base = p.endsWith('USDT') ? p.slice(0, -4) : p;
            return {label: base, value: base};
          });
        }
        const watchlistArr: string[] = market.coinWatchlist
          ? (market.coinWatchlist as string).split(',').map((s: string) => s.trim()).filter(Boolean)
          : [];

        this.form = this.fb.group({
          mainprice: [market.mainprice || 'BTC'],
          coinWatchlist: [watchlistArr, [(c: AbstractControl): ValidationErrors | null =>
            Array.isArray(c.value) && c.value.length > 50
              ? { maxWatchlist: true }
              : null
          ]],
        });
      });
  }

  public saveMarket() {
    const raw = this.form.getRawValue();
    const watchlist: string[] = Array.isArray(raw.coinWatchlist) ? raw.coinWatchlist : [];
    if (watchlist.length > 50) {
      this.toastr.warning('Please select no more than 50 coins.', 'Watchlist too long');
      return;
    }
    const payload: any = {
      mainprice:     raw.mainprice,
      coinWatchlist: watchlist.join(','),
    };
    this.systemService.patchSettingMarket(undefined, payload)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => { this.toastr.success('Success!', 'Saved.'); },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }

  public restart() {
    this.systemService.restart().subscribe();
    this.toastr.success('Success!', 'NMAxe restarted');
  }
}
