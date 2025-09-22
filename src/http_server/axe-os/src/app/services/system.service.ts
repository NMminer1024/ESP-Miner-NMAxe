import {HttpClient, HttpEvent, HttpParams, HttpHeaders} from '@angular/common/http';
import {Injectable} from '@angular/core';
import {delay, Observable, of, timeout, map, catchError} from 'rxjs';
import {eASICModel} from 'src/models/enum/eASICModel';
import {ISystemInfo} from 'src/models/ISystemInfo';

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
          power: 11.670000076293945,
          voltage: 5208.75,
          current: 2237.5,
          temp: 60,
          vrTemp: 45,
          hashRate: 475,
          bestDiff: "0",
          bestSessionDiff: "0",
          freeHeap: 200504,
          coreVoltage: 1200,
          coreVoltageActual: 1200,
          hostname: "NMAxe",
          // timezone: "8.0",
          ssid: "default",
          wifiPass: "password",
          wifiStatus: "Connected!",
          sharesAccepted: 1,
          sharesRejected: 0,
          uptimeSeconds: 38,
          asicCount: 1,
          smallCoreCount: 672,
          ASICModel: eASICModel.BM1366,
          stratumURLUSED: "stratum+tcp://solo.ckpool.org:3333",
          stratumURL1: "stratum+tcp://solo.ckpool.org:3333",
          stratumURL2: "stratum+tcp://solo.ckpool.org:3333",
          stratumUser1: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          stratumPassword1: "x",
          stratumUser2: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          stratumPassword2: "x",
          stratumUserUSED: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          frequency: 485,
          coin: "btc",
          brightness: 100,
          version: "2.0",
          boardVersion: "204",
          flipscreen: 1,
          invertscreen: 0,
          ledindicator: 0,
          autofanspeed: 1,
          targetAsicTemp: "55",
          autoscreen: 0,
          fanspeed: 100,
          fanrpm: 0,
          boardtemp1: 30,
          boardtemp2: 40,
          overheat_mode: 0
        }
      ).pipe(delay(1000));
    }
  }

  public restart(uri: string = '') {
    return this.httpClient.post(`${uri}/api/system/restart`, {}, {responseType: 'text'});
  }

  public updateSystem(uri: string = '', update: any) {
    return this.httpClient.patch(`${uri}/api/system`, update);
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

  public performOTAUpdate(file: File | Blob, host = '') {
    return this.otaUpdate(file, `${host}/api/system/OTA`);
  }

  public performWWWOTAUpdate(file: File | Blob, host = '') {
    return this.otaUpdate(file, `${host}/api/system/OTAWWW`);
  }

  public getSwarmInfo(uri: string = ''): Observable<{ ip: string }[]> {
    return this.httpClient.get(`${uri}/api/swarm/info`) as Observable<{ ip: string }[]>;
  }

  public getHashrateDistribution(uri: string = ''): Observable<any> {
    return this.httpClient.get(`${uri}/api/system/hr/dist`);
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
    
    return this.httpClient.get(`${uri}/api/system/status/history`, { 
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
    
    return this.httpClient.get<StatusHistoryResponse>(`${uri}/api/system/status/realtime`, { headers })
      .pipe(timeout(20000)); // 实时数据20秒超时
  }

  public getLuckyHistory(uri: string = ''): Observable<StatusHistoryResponse> {
    const headers = new HttpHeaders({
      'Cache-Control': 'no-cache, no-store, must-revalidate',
      'Pragma': 'no-cache',
      'Expires': '0'
    });
    
    console.log('📡 Requesting lucky history data...');
    
    return this.httpClient.get(`${uri}/api/system/luck/history`, { 
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
    
    return this.httpClient.get<StatusHistoryResponse>(`${uri}/api/system/luck/realtime`, { headers })
      .pipe(timeout(20000)); // 实时数据20秒超时
  }
}
