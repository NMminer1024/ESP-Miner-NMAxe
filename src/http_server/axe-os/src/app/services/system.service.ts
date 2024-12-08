import { HttpClient, HttpEvent } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { delay, Observable, of } from 'rxjs';
import { eASICModel } from 'src/models/enum/eASICModel';
import { ISystemInfo } from 'src/models/ISystemInfo';

import { environment } from '../../environments/environment';

@Injectable({
  providedIn: 'root'
})
export class SystemService {

  constructor(
    private httpClient: HttpClient
  ) { }

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
          hostname: "Bitaxe",
          ssid: "default",
          wifiPass: "password",
          wifiStatus: "Connected!",
          sharesAccepted: 1,
          sharesRejected: 0,
          uptimeSeconds: 38,
          asicCount: 1,
          smallCoreCount: 672,
          ASICModel: eASICModel.BM1366,
          stratumURL: "public-pool.io",
          stratumPort: 21496,
          stratumUser: "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1",
          frequency: 485,
          brightness: 100,
          version: "2.0",
          boardVersion: "204",
          flipscreen: 1,
          invertscreen: 0,
          invertfanpolarity: 1,
          ledindicator: 0,
          autofanspeed: 1,
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
    return this.httpClient.post(`${uri}/api/system/restart`, {});
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
          responseType:"text"
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
  
  public performOTAUpdate(file: File | Blob) {
    return this.otaUpdate(file, `/api/system/OTA`);
  }
  public performWWWOTAUpdate(file: File | Blob) {
    return this.otaUpdate(file, `/api/system/OTAWWW`);
  }
  public getSwarmInfo(uri: string = ''): Observable<{ ip: string }[]> {
    return this.httpClient.get(`${uri}/api/swarm/info`) as Observable<{ ip: string }[]>;
  }
  public updateSwarm(uri: string = '', swarmConfig: any) {
    return this.httpClient.patch(`${uri}/api/swarm`, swarmConfig);
  }
}
