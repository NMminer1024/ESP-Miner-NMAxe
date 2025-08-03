import {Component} from '@angular/core';
import {interval, map, Observable, shareReplay, startWith, switchMap, tap} from 'rxjs';
import {HashSuffixPipe} from 'src/app/pipes/hash-suffix.pipe';
import {SystemService} from 'src/app/services/system.service';
import {eASICModel} from 'src/models/enum/eASICModel';
import {ISystemInfo} from 'src/models/ISystemInfo';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent {

  public info$: Observable<ISystemInfo>;

  public quickLink$: Observable<string | undefined>;
  public expectedHashRate$: Observable<number | undefined>;
  public chartData?: any;

  constructor(
    private systemService: SystemService
  ) {
    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(2));
        info.current = parseFloat((info.current / 1000).toFixed(3));
        info.coreVoltageActual = parseFloat((info.coreVoltageActual / 1000).toFixed(3));
        info.coreVoltage = parseFloat((info.coreVoltage / 1000).toFixed(3));
        info.temp = parseFloat(info.temp.toFixed(1));
        info.vrTemp = parseFloat(info.vrTemp.toFixed(1));
        info.mcuTemp = info.mcuTemp !== undefined ? parseFloat(info.mcuTemp.toFixed(1)) : undefined;

        return info;
      }),
      shareReplay({refCount: true, bufferSize: 1})
    );

    this.expectedHashRate$ = this.info$.pipe(map(info => {
      return Math.floor(info.frequency * ((info.smallCoreCount * info.asicCount) / 1000))
    }))

    this.quickLink$ = this.info$.pipe(
      map(info => {
        if (info.stratumURLUSED.includes('public-pool.io')) {
          const address = info.stratumUserUSED.split('.')[0]
          return `https://web.public-pool.io/#/app/${address}`;
        } else if (info.stratumURLUSED.includes('ocean.xyz')) {
          const address = info.stratumUserUSED.split('.')[0]
          return `https://ocean.xyz/stats/${address}`;
        } else if (info.stratumURLUSED.includes('solo.d-central.tech')) {
          const address = info.stratumUserUSED.split('.')[0]
          return `https://solo.d-central.tech/#/app/${address}`;
        } else if (info.stratumURLUSED.includes('solo.ckpool.org:3333')) {
          const address = info.stratumUserUSED.split('.')[0]
          return `https://solostats.ckpool.org/users/${address}`;
        } else if (info.stratumURLUSED.includes('pool.nmminer.com')) {
          const address = info.stratumUserUSED.split('.')[0]
          return `https://pool.nmminer.com/user?workername=${address}`;
        } else if (info.stratumURLUSED.includes('molepool.com')) {
          const address = info.stratumUserUSED.split('.')[0]
          const coin    = info.coin;
          return `https://${coin}.molepool.com/account/${address}`;
        } else {
          return undefined;
        }
      })
    )
  }
}
