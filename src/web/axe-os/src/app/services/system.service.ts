import {HttpClient, HttpEvent, HttpParams, HttpHeaders} from '@angular/common/http';
import {Injectable} from '@angular/core';
import {delay, Observable, of, timeout, map, catchError, switchMap} from 'rxjs';
import {eASICModel} from 'src/models/enum/eASICModel';
import {ISystemInfo} from 'src/models/ISystemInfo';
import {IGaugeLimits} from 'src/models/IGaugeLimits';

import {environment} from '../../environments/environment';

export interface StatusHistoryResponse {
  timestamp: number;
  labels: string[];
  statistics: any[][];
  size: number;
  sampledSize?: number;
  sampleInterval?: number;
}

@Injectable({
  providedIn: 'root'
})
export class SystemService {

  constructor(
    private httpClient: HttpClient
  ) {
  }

  public getInfo(uri: string = ''): Observable<ISystemInfo> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/system/info`) as Observable<ISystemInfo>;
    } else {
      return of(
        {
          // ── Nested structured fields (new API) ──────────────────────────
          power: {
            power: 11.67,
            vbus:  5208,
            ibus:  2237,
          },
          temps: {
            vcore: 45,
            mcu:   35,
            asic:  60,
          },
          asic: {
            count:        1,
            model:        eASICModel.BM1366,
            vcoreReq:     1200,
            vcoreReal:    1200,
            freqReq:      485,
            smallCoreCnt: 672,
          },
          miner: {
            hashRate:        475,
            bestDiffEver:    "0",
            bestDiffSession: "0",
            freeHeap:        200504,
            sAccepted:       1,
            sRejected:       0,
            uptimeSeconds:   38,
          },
          identity: {
            fwVersion: "2.0",
            hwModel:   "204",
            hostName:  "NMAxe",
            ssid:      "default",
            rssi:      -55,
          },
          // ── Fan ────────────────────────────────────────────────────────
          fans: [
            { id: 0, speed: 100, rpm: 4500 }
          ],
          // ── Stratum ────────────────────────────────────────────────────
          stratum: {
            url:  "stratum+tcp://solo.ckpool.org:3333",
            user: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
            pwd:  "x",
            primary: {
              url:  "stratum+tcp://solo.ckpool.org:3333",
              user: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
              pwd:  "x"
            },
            fallback: {
              url:  "stratum+tcp://solo.ckpool.org:3333",
              user: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
              pwd:  "x"
            }
          },
          // ── Misc ───────────────────────────────────────────────────────
          coinPriceDisplay: "btc",
          mainprice: "BTC",
          timeZone: "8.0",
          timeFormat: 24,
          dateFormat: "YYYY/MM/DD",
          screenFlip: 1,
          screenAutoRoll: 0,
          Brightness: 100,
          asicTargetTemp: "55",
          fanAutoSpeed: 1,
          ledIndicator: 0,
          // ── Legacy flat names (backward compat) ────────────────────────
          usedUrl: "stratum+tcp://solo.ckpool.org:3333",
          usedUser: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          primaryUrl: "stratum+tcp://solo.ckpool.org:3333",
          primaryUser: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          primaryPassword: "x",
          fallBackUrl: "stratum+tcp://solo.ckpool.org:3333",
          fallBackUser: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          fallBackPassword: "x",
          stratumURLUSED: "stratum+tcp://solo.ckpool.org:3333",
          stratumURL1: "stratum+tcp://solo.ckpool.org:3333",
          stratumURL2: "stratum+tcp://solo.ckpool.org:3333",
          stratumUser1: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          stratumPassword1: "x",
          stratumUser2: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          stratumPassword2: "x",
          stratumUserUSED: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          coin: "btc",
          brightness: 100,
          version: "2.0",
          boardVersion: "204",
          ASICModel: eASICModel.BM1366,
          bestDiff: "0",
          bestSessionDiff: "0",
          smallCoreCount: 672,
          flipscreen: 1,
          invertscreen: 0,
          ledindicator: 0,
          autofanspeed: 1,
          targetAsicTemp: "55",
          autoscreen: 0,
          fanspeed: 100,
          fanrpm: 4500,
          boardtemp2: 40,
        }
      ) as Observable<ISystemInfo>;
    }
  }

  public restart(uri: string = '') {
    return this.httpClient.post(`${uri}/api/system/restart`, {}, {responseType: 'text'});
  }

  public resetMinerStats(uri: string = '') {
    return this.httpClient.post(`${uri}/api/system/clearhits`, {});
  }


  private otaUpdate(file: File | Blob, url: string) {
    return new Observable<HttpEvent<Object>>((subscriber) => {
      const formData = new FormData();
      formData.append('update', file);

      return this.httpClient.post(url, formData, {
        reportProgress: true,
        observe: 'events',
        responseType: "text"
      }).subscribe({
        next: (e) => {
          subscriber.next(e);
        },
        error: (err) => {
          subscriber.error(err)
        },
        complete: () => {
          subscriber.next()
          subscriber.complete();
        }
      });
    });
  }

  // Cache: key = host string, value = whether the device runs new-API firmware.
  // Probes /api/setting/time which only exists on new firmware;
  // falls back gracefully so old firmware (which only knows /api/system/OTA*) still works.
  private _newApiCache = new Map<string, boolean>();

  private checkNewApiSupport(host: string): Observable<boolean> {
    const cached = this._newApiCache.get(host);
    if (cached !== undefined) return of(cached);
    return this.httpClient.get<any>(`${host}/api/setting/time`).pipe(
      timeout(5000),
      map(res => {
        // Angular's HttpClient does NOT throw when JSON.parse fails — it silently assigns
        // the raw text to the response body.  When old firmware lacks /api/setting/time it
        // returns a 302→/ redirect that resolves to Angular's index.html (HTTP 200, HTML body).
        // Same-origin requests (Device A probing itself) are NOT blocked by CORS, so the
        // map() callback would fire and wrongly flag the device as new firmware.
        // Fix: require the response to be a proper JSON object containing the `timeZone`
        // field that only the real /api/setting/time endpoint returns.
        const isNewApi = res !== null && typeof res === 'object' && 'timeZone' in res;
        this._newApiCache.set(host, isNewApi);
        return isNewApi;
      }),
      catchError(() => { this._newApiCache.set(host, false); return of(false); })
    );
  }

  public performOTAUpdate(file: File | Blob, host = '') {
    return this.checkNewApiSupport(host).pipe(
      switchMap(isNew => this.otaUpdate(file, isNew ? `${host}/api/update/firmware` : `${host}/api/system/OTA`))
    );
  }

  public performWWWOTAUpdate(file: File | Blob, host = '') {
    return this.checkNewApiSupport(host).pipe(
      switchMap(isNew => this.otaUpdate(file, isNew ? `${host}/api/update/spiffs` : `${host}/api/system/OTAWWW`))
    );
  }

  public getSwarmInfo(uri: string = ''): Observable<{ ip: string }[]> {
    return this.httpClient.get(`${uri}/api/swarm/info`) as Observable<{ ip: string }[]>;
  }

  public getHashrateDistribution(uri: string = ''): Observable<any> {
    return this.httpClient.get(`${uri}/api/dashboard/hr/dist`);
  }

  public updateSwarm(uri: string = '', swarmConfig: any) {
    return this.httpClient.patch(`${uri}/api/swarm`, swarmConfig);
  }

  public getStatusHistory(sampleInterval: number = 10, uri: string = ''): Observable<StatusHistoryResponse> {
    const params = new HttpParams()
      .set('interval', sampleInterval.toString())
      .set('_t', Date.now().toString()); // 避免缓存
    
    // 根据采样间隔设置不同的超时时间 - 增加超时时间
    let timeoutMs = 45000; // 默认45秒
    if (sampleInterval <= 1) {
      timeoutMs = 240000; // 高分辨率模式：4分钟
    } else if (sampleInterval <= 5) {
      timeoutMs = 120000; // 中等分辨率：2分钟
    }
    
    const headers = new HttpHeaders({
      'Cache-Control': 'no-cache, no-store, must-revalidate',
      'Pragma': 'no-cache',
      'Expires': '0'
    });
    
    console.log(`📡 HTTP timeout set to ${timeoutMs/1000}s for sample interval ${sampleInterval}`);
    
    return this.httpClient.get(`${uri}/api/dashboard/chart/history`, { 
      params, 
      headers,
      responseType: 'text' // 先获取为文本
    }).pipe(
      timeout(timeoutMs),
      map((response: string) => {
        try {
          console.log(`📄 Response received, length: ${response.length} bytes`);
          
          // 检查响应是否看起来像JSON
          if (!response.trim().startsWith('{') && !response.trim().startsWith('[')) {
            throw new Error(`Invalid JSON format. Response starts with: "${response.substring(0, 100)}"`);
          }
          
          const parsed = JSON.parse(response) as StatusHistoryResponse;
          console.log(`✅ JSON parsed successfully, statistics count: ${parsed.statistics?.length || 0}`);
          return parsed;
        } catch (error) {
          console.error('❌ JSON parsing failed:', error);
          console.error('Response preview (first 500 chars):', response.substring(0, 500));
          console.error('Response preview (last 500 chars):', response.substring(Math.max(0, response.length - 500)));
          
          // 详细分析 JSON 错误
          if (error instanceof SyntaxError) {
            const errorMessage = error.message;
            const positionMatch = errorMessage.match(/at position (\d+)/);
            if (positionMatch) {
              const position = parseInt(positionMatch[1]);
              const start = Math.max(0, position - 50);
              const end = Math.min(response.length, position + 50);
              const context = response.substring(start, end);
              console.error(`JSON error context around position ${position}:`, context);
              console.error(`Character at error position: "${response.charAt(position)}" (charCode: ${response.charCodeAt(position)})`);
              
              // 检查常见问题
              if (context.includes('NaN') || context.includes('Infinity')) {
                console.error('Found NaN or Infinity in JSON - this indicates invalid numeric data');
              }
              if (context.includes('undefined')) {
                console.error('Found undefined in JSON - this indicates missing data');
              }
            }
          }
          
          throw new Error(`JSON parsing failed: ${error instanceof Error ? error.message : 'Unknown error'}`);
        }
      }),
      catchError(error => {
        if (error.name === 'TimeoutError') {
          console.error(`⏰ Request timed out after ${timeoutMs/1000}s for sample interval ${sampleInterval}`);
          throw new Error(`Request timed out after ${timeoutMs/1000} seconds. Try using a lower detail level.`);
        } else if (error.status === 0) {
          console.error('🌐 Network connection failed');
          throw new Error('Network connection failed. Please check your connection.');
        } else {
          console.error('❌ HTTP request failed:', error);
          throw error;
        }
      })
    );
  }

  public getStatusRealtime(uri: string = ''): Observable<StatusHistoryResponse> {
    const headers = new HttpHeaders({
      'Cache-Control': 'no-cache'
    });
    
    return this.httpClient.get<StatusHistoryResponse>(`${uri}/api/dashboard/chart/realtime`, { headers })
      .pipe(timeout(20000)); // 实时数据20秒超时
  }

  public getLuckyHistory(uri: string = ''): Observable<StatusHistoryResponse> {
    const headers = new HttpHeaders({
      'Cache-Control': 'no-cache, no-store, must-revalidate',
      'Pragma': 'no-cache',
      'Expires': '0'
    });
    
    console.log('📡 Requesting lucky history data...');
    
    return this.httpClient.get(`${uri}/api/dashboard/luck/history`, { 
      headers,
      responseType: 'text' // 先获取为文本
    }).pipe(
      timeout(60000), // 60秒超时
      map((response: string) => {
        try {
          console.log(`📄 Lucky history response received, length: ${response.length} bytes`);
          
          // 检查响应是否看起来像JSON
          if (!response.trim().startsWith('{') && !response.trim().startsWith('[')) {
            throw new Error(`Invalid JSON format. Response starts with: "${response.substring(0, 100)}"`);
          }
          
          const parsed = JSON.parse(response) as StatusHistoryResponse;
          console.log(`✅ Lucky history JSON parsed successfully, statistics count: ${parsed.statistics?.length || 0}`);
          return parsed;
        } catch (error) {
          console.error('❌ Lucky history JSON parsing failed:', error);
          console.error('Response preview (first 500 chars):', response.substring(0, 500));
          throw new Error(`Lucky history JSON parsing failed: ${error instanceof Error ? error.message : 'Unknown error'}`);
        }
      }),
      catchError(error => {
        if (error.name === 'TimeoutError') {
          console.error('⏰ Lucky history request timed out after 60s');
          throw new Error('Lucky history request timed out after 60 seconds.');
        } else if (error.status === 0) {
          console.error('🌐 Lucky history network connection failed');
          throw new Error('Network connection failed. Please check your connection.');
        } else {
          console.error('❌ Lucky history HTTP request failed:', error);
          throw error;
        }
      })
    );
  }

  public getLuckyRealtime(uri: string = ''): Observable<StatusHistoryResponse> {
    const headers = new HttpHeaders({
      'Cache-Control': 'no-cache'
    });
    
    return this.httpClient.get<StatusHistoryResponse>(`${uri}/api/dashboard/luck/realtime`, { headers })
      .pipe(timeout(20000)); // 实时数据20秒超时
  }

  public getGaugeLimits(uri: string = ''): Observable<IGaugeLimits> {
    if (environment.production) {
      return this.httpClient.get<IGaugeLimits>(`${uri}/api/dashboard/gauge/limits`);
    } else {
      // Mock data for development
      return of({
        power: {
          vbus: { min: 0.0, max: 18.0 },
          ibus: { min: 0.0, max: 4.0 },
          power: { min: 0.0, max: 30.0 }
        },
        heat: {
          mcu: { min: 0.0, max: 75.0 },
          asic: { min: 0.0, max: 80.0 },
          vcore: { min: 0.0, max: 100.0 },
          fan: { min: 0.0, max: 9000.0 }
        },
        performance: {
          asic_freq_req: { min: 400.0, max: 900.0 },
          vcore_req: { min: 1.0, max: 1.5 },
          vcore_measure: { min: 1.0, max: 1.5 }
        }
      });
    }
  }

  // ── Settings per-card APIs ────────────────────────────────────────────────

  public getSettingNetwork(uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/setting/network`);
    } else {
      return of({ hostName: 'NMAxe', ssid: 'default', status: 'connected', ip: '192.168.1.100' }).pipe(delay(300));
    }
  }

  public patchSettingNetwork(uri: string = '', data: any): Observable<any> {
    return this.httpClient.patch(`${uri}/api/setting/network`, data);
  }

  public getSettingTime(uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/setting/time`);
    } else {
      return of({ timeZone: '8.0', timeFormat: 24, dateFormat: 'YYYY/MM/DD' }).pipe(delay(300));
    }
  }

  public patchSettingTime(uri: string = '', data: any): Observable<any> {
    return this.httpClient.patch(`${uri}/api/setting/time`, data);
  }

  public getSettingMining(uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/setting/mining`);
    } else {
      return of({
        vcoreReq: 1200,
        freqReq: 485,
        asic: eASICModel.BM1366,
        stratum: {
          used:     { url: 'stratum+tcp://solo.ckpool.org:3333', user: '18dK8EfyepKuS74fs27iuDJWoGUT4rPto1', pwd: 'x' },
          primary:  { url: 'stratum+tcp://solo.ckpool.org:3333', user: '18dK8EfyepKuS74fs27iuDJWoGUT4rPto1', pwd: 'x' },
          fallback: { url: 'stratum+tcp://solo.ckpool.org:3333', user: '18dK8EfyepKuS74fs27iuDJWoGUT4rPto1', pwd: 'x' },
        },
        overclock: { options: [{ name: '485 MHz', value: 485 }, { name: '500 MHz', value: 500 }, { name: '525 MHz', value: 525 }] },
        vcore:     { options: [{ name: '1200 mV', value: 1200 }, { name: '1210 mV', value: 1210 }, { name: '1220 mV', value: 1220 }] },
      }).pipe(delay(300));
    }
  }

  public patchSettingMining(uri: string = '', data: any): Observable<any> {
    return this.httpClient.patch(`${uri}/api/setting/mining`, data);
  }

  public getSettingMarket(uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/setting/market`);
    } else {
      return of({ mainprice: 'BTC', coinWatchlist: 'BTC,ETH,SOL',
                  pairs: ['BTCUSDT','ETHUSDT','BNBUSDT','SOLUSDT','XRPUSDT','ADAUSDT','DOGEUSDT',
                          'AVAXUSDT','DOTUSDT','MATICUSDT','KASUSDT','LTCUSDT','BCHUSDT','XECUSDT',
                          'TRXUSDT','TONUSDT','ATOMUSDT','LINKUSDT','UNIUSDT','SHIBUSDT',
                          'NEARUSDT','ARBUSDT','OPUSDT','INJUSDT','SUIUSDT','APTUSDT',
                          'PEPEUSDT','WIFUSDT','BONKUSDT','FLOKIUSDT',
                          'AAVEUSDT','MKRUSDT','CRVUSDT','DYDXUSDT','GMXUSDT',
                          'FILUSDT','ARUSDT','GRTUSDT','RENDERUSDT','TAOUSDT',
                          'RUNEUSDT','STXUSDT','APEUSDT','SANDUSDT','MANAUSDT',
                          'XMRUSDT','ZECUSDT','RVNUSDT','DASHUSDT','DCRUSDT',
                          'DRBUSDT','XLMUSDT','ICPUSDT','VETUSDT','HBARUSDT'] }).pipe(delay(300));
    }
  }

  public patchSettingMarket(uri: string = '', data: any): Observable<any> {
    return this.httpClient.patch(`${uri}/api/setting/market`, data);
  }

  public getSettingPreference(uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.get(`${uri}/api/setting/preference`);
    } else {
      return of({
        screenFlip: 0, ledIndicator: 1, screenAutoRoll: 0, Brightness: 100,
        fans: [
          { id: 0, speed: 60,  rpm: 3600, auto: 1, target: 55, tempMin: 20,  tempMax: 75  },
          { id: 1, speed: 80,  rpm: 4200, auto: 1, target: 85, tempMin: 80,  tempMax: 130 },
        ],
      }).pipe(delay(300));
    }
  }

  public patchSettingPreference(uri: string = '', data: any): Observable<any> {
    return this.httpClient.patch(`${uri}/api/setting/preference`, data);
  }
}
