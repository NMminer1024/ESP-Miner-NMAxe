import {Component, OnInit} from '@angular/core';
import {interval, map, Observable, shareReplay, startWith, switchMap, tap} from 'rxjs';
import {HashSuffixPipe} from 'src/app/pipes/hash-suffix.pipe';
import {SystemService} from 'src/app/services/system.service';
import {eASICModel} from 'src/models/enum/eASICModel';
import {ISystemInfo} from 'src/models/ISystemInfo';
import {IGaugeLimits} from 'src/models/IGaugeLimits';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent implements OnInit {

  public info$: Observable<ISystemInfo>;

  public quickLink$: Observable<string | undefined>;
  public expectedHashRate$: Observable<number | undefined>;
  public chartData?: any;

  // Gauge limits
  public gaugeLimits?: IGaugeLimits;

  constructor(
    private systemService: SystemService
  ) {
    this.info$ = interval(5000).pipe(
      startWith(0),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      map(info => {
        // Map new field names to legacy names for backward compatibility
        info.temp = info.asicTemp;
        info.vrTemp = info.vcoreTemp;
        info.coreVoltage = info.vcoreReq;
        info.coreVoltageActual = info.vcoreActual;
        info.frequency = info.freqReq;
        info.hostname = info.hostName;
        info.ssid = info.wifiSSID;
        
        // Map nested stratum structure to legacy flat structure
        info.stratumURLUSED = info.stratum?.used?.url || info.usedUrl || '';
        info.stratumUserUSED = info.stratum?.used?.user || info.usedUser || '';
        info.stratumURL1 = info.stratum?.primary?.url || info.primaryUrl || '';
        info.stratumUser1 = info.stratum?.primary?.user || info.primaryUser || '';
        info.stratumPassword1 = info.stratum?.primary?.pwd || info.primaryPassword || '';
        info.stratumURL2 = info.stratum?.fallback?.url || info.fallBackUrl || '';
        info.stratumUser2 = info.stratum?.fallback?.user || info.fallBackUser || '';
        info.stratumPassword2 = info.stratum?.fallback?.pwd || info.fallBackPassword || '';
        
        info.version = info.fwVersion;
        info.boardVersion = info.hwModel;
        info.coin = info.coinPriceDisplay;
        info.brightness = info.Brightness;
        info.ASICModel = info.asic;
        info.bestDiff = info.bestDiffEver;
        info.bestSessionDiff = info.bestDiffSession;
        info.smallCoreCount = info.smallCoreCnt;
        info.flipscreen = info.screenFlip;
        info.autoscreen = info.screenAutoRoll;
        info.ledindicator = info.ledIndicator;
        info.autofanspeed = info.fanAutoSpeed;
        
        // Extract fan data from fans array (use fan id=0 as default)
        if (info.fans && info.fans.length > 0) {
          const defaultFan = info.fans.find((f: any) => f.id === 0) || info.fans[0];
          info.fanspeed = defaultFan.speed;
          info.fanrpm = defaultFan.rpm;
        }

        // Process numeric values
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(2));
        info.current = parseFloat((info.current / 1000).toFixed(3));
        info.coreVoltageActual = parseFloat((info.vcoreActual / 1000).toFixed(3));
        info.coreVoltage = parseFloat((info.vcoreReq / 1000).toFixed(3));
        info.temp = parseFloat(info.asicTemp.toFixed(1));
        info.vrTemp = parseFloat(info.vcoreTemp.toFixed(1));
        info.mcuTemp = info.mcuTemp !== undefined ? parseFloat(info.mcuTemp.toFixed(1)) : undefined;

        return info;
      }),
      shareReplay({refCount: true, bufferSize: 1})
    );

    this.expectedHashRate$ = this.info$.pipe(map(info => {
      return Math.floor((info.frequency || info.freqReq) * ((info.smallCoreCount || info.smallCoreCnt) * info.asicCount) / 1000)
    }))

    this.quickLink$ = this.info$.pipe(
      map(info => {
        // Parse new stratum nested structure or fallback to legacy flat structure
        const poolUrl = info.stratum?.used?.url || info.stratumURLUSED || info.usedUrl || '';
        const poolUser = info.stratum?.used?.user || info.stratumUserUSED || info.usedUser || '';
        const coin = info.coin || info.coinPriceDisplay || '';
        
        if (poolUrl.includes('public-pool.io')) {
          const address = poolUser.split('.')[0]
          return `https://web.public-pool.io/#/app/${address}`;
        } else if (poolUrl.includes('ocean.xyz')) {
          const address = poolUser.split('.')[0]
          return `https://ocean.xyz/stats/${address}`;
        } else if (poolUrl.includes('solo.d-central.tech')) {
          const address = poolUser.split('.')[0]
          return `https://solo.d-central.tech/#/app/${address}`;
        } else if (poolUrl.includes('solo.ckpool.org:3333')) {
          const address = poolUser.split('.')[0]
          return `https://solostats.ckpool.org/users/${address}`;
        } else if (poolUrl.includes('pool.nmminer.com')) {
          const address = poolUser.split('.')[0]
          return `https://pool.nmminer.com/user?workername=${address}`;
        } else if (poolUrl.includes('au.solobtc.nmminer.com')) {
          const address = poolUser.split('.')[0]
          return `https://au.solobtc.nmminer.com/#/app/${address}`;
        } else if (poolUrl.includes('solobtc.nmminer.com')) {
          const address = poolUser.split('.')[0]
          return `https://solobtc.nmminer.com/#/app/${address}`;
        } else if (poolUrl.includes('molepool.com')) {
          const address = poolUser.split('.')[0]
          return `https://${coin}.molepool.com/account/${address}`;
        } else if (poolUrl.includes('dgb-stratum.solominer.net')) {
          const address = poolUser.split('.')[0]
          return `https://digibyte.solominer.net/#/app/${address}`;
        } 
        else {
          return undefined;
        }
      })
    )
  }

  ngOnInit(): void {
    // Load gauge limits once when component initializes
    this.systemService.getGaugeLimits().subscribe({
      next: (limits) => {
        this.gaugeLimits = limits;
        console.log('Gauge limits loaded:', limits);
      },
      error: (err) => {
        console.error('Failed to load gauge limits:', err);
      }
    });
  }

  // Helper methods for gauge configuration
  getWarningThreshold(max: number): number {
    return max * 0.9;
  }

  isWarning(value: number | undefined, max: number): boolean {
    if (value === undefined || value === null) {
      return false;
    }
    return value >= this.getWarningThreshold(max);
  }
}
