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
  public latency$: Observable<number>;

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
        // Hoist nested sub-objects into local aliases
        const pw  = (info as any).power    as any;
        const tmp = (info as any).temps as any;
        const asc = (info as any).asic     as any;
        const mnr = (info as any).miner    as any;
        const idn = (info as any).identity as any;

        // --- power ---
        const rawPower   = pw?.power ?? 0;
        const rawVbus    = pw?.vbus  ?? 0;
        const rawIbus    = pw?.ibus  ?? 0;
        info.power_w = parseFloat(rawPower.toFixed(1));
        info.voltage = parseFloat((rawVbus / 1000).toFixed(2));
        info.current = parseFloat((rawIbus / 1000).toFixed(3));

        // --- temp ---
        info.asicTemp  = tmp?.asic  ?? info.asicTemp;
        info.vcoreTemp = tmp?.vcore ?? info.vcoreTemp;
        info.temp   = parseFloat(((info.asicTemp  ?? 0) as number).toFixed(1));
        info.vrTemp = parseFloat(((info.vcoreTemp ?? 0) as number).toFixed(1));

        // --- asic ---
        info.vcoreReq    = asc?.vcoreReq    ?? info.vcoreReq;
        info.vcoreActual = asc?.vcoreReal   ?? info.vcoreActual;
        info.freqReq     = asc?.freqReq     ?? info.freqReq;
        info.smallCoreCnt = asc?.smallCoreCnt ?? info.smallCoreCnt;
        info.asicCount   = asc?.count       ?? info.asicCount;
        info.coreVoltage       = parseFloat(((info.vcoreReq    ?? 0) as number / 1000).toFixed(3));
        info.coreVoltageActual = parseFloat(((info.vcoreActual ?? 0) as number / 1000).toFixed(3));
        info.frequency   = info.freqReq;
        info.ASICModel   = asc?.model ?? info.ASICModel;
        info.smallCoreCount = info.smallCoreCnt;

        // --- miner ---
        info.hashRate        = mnr?.hashRate        ?? info.hashRate;
        info.bestDiffEver    = mnr?.bestDiffEver    ?? info.bestDiffEver;
        info.bestDiffSession = mnr?.bestDiffSession ?? info.bestDiffSession;
        info.freeHeap        = mnr?.freeHeap        ?? info.freeHeap;
        info.sharesAccepted  = mnr?.sAccepted       ?? info.sharesAccepted;
        info.sharesRejected  = mnr?.sRejected       ?? info.sharesRejected;
        info.uptimeSeconds   = mnr?.uptimeSeconds   ?? info.uptimeSeconds;
        info.bestDiff        = info.bestDiffEver;
        info.bestSessionDiff = info.bestDiffSession;

        // --- identity ---
        info.fwVersion  = idn?.fwVersion ?? info.fwVersion;
        info.hwModel    = idn?.hwModel   ?? info.hwModel;
        info.hostName   = idn?.hostName  ?? info.hostName;
        info.wifiSSID   = idn?.ssid      ?? info.wifiSSID;
        info.wifiRSSI   = idn?.rssi      ?? info.wifiRSSI;
        info.hostname   = info.hostName;
        info.ssid       = info.wifiSSID;
        info.version    = info.fwVersion;
        info.boardVersion = info.hwModel;

        // --- stratum (nested → legacy flat) ---
        info.stratumURLUSED  = info.stratum?.url  || info.usedUrl  || '';
        info.stratumUserUSED = info.stratum?.user || info.usedUser || '';
        info.stratumURL1     = info.stratum?.primary?.url  || info.primaryUrl  || '';
        info.stratumUser1    = info.stratum?.primary?.user || info.primaryUser || '';
        info.stratumPassword1 = info.stratum?.primary?.pwd || info.primaryPassword || '';
        info.stratumURL2     = info.stratum?.fallback?.url  || info.fallBackUrl  || '';
        info.stratumUser2    = info.stratum?.fallback?.user || info.fallBackUser || '';
        info.stratumPassword2 = info.stratum?.fallback?.pwd || info.fallBackPassword || '';

        // --- misc ---
        info.coin       = info.coinPriceDisplay;
        info.brightness = info.Brightness;
        info.flipscreen = info.screenFlip;
        info.autoscreen = info.screenAutoRoll;
        info.ledindicator = info.ledIndicator;
        info.autofanspeed = info.fanAutoSpeed;

        // Extract fan data from fans array (use fan id=0 as default)
        if (info.fans && info.fans.length > 0) {
          const defaultFan = info.fans.find((f: any) => f.id === 0) || info.fans[0];
          info.fanspeed = defaultFan.speed;
          info.fanrpm   = defaultFan.rpm;
          const vcoreFan = info.fans.find((f: any) => f.id === 1);
          info.vcoreFanRpm = vcoreFan ? vcoreFan.rpm : undefined;
        }

        return info;
      }),
      shareReplay({refCount: true, bufferSize: 1})
    );

    this.expectedHashRate$ = this.info$.pipe(map(info => {
      return Math.floor((info.frequency || info.freqReq || 0) * ((info.smallCoreCount || info.smallCoreCnt || 0) * (info.asicCount || 0)) / 1000)
    }))

    this.quickLink$ = this.info$.pipe(
      map(info => {
        // Parse new stratum nested structure or fallback to legacy flat structure
        const poolUrl = info.stratum?.url || info.stratumURLUSED || info.usedUrl || '';
        const poolUser = info.stratum?.user || info.stratumUserUSED || info.usedUser || '';
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

    this.latency$ = interval(5000).pipe(
      startWith(0),
      switchMap(() => this.systemService.getStatusRealtime()),
      map(response => {
        if (response?.statistics?.length && response.statistics[0]?.length >= 13) {
          return Number(response.statistics[0][12]) || 0;
        }
        return 0;
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    );
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
