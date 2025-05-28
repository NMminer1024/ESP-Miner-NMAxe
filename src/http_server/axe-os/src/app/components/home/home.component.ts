import {Component, ViewChild} from '@angular/core';
import {interval, map, Observable, shareReplay, startWith, switchMap, tap} from 'rxjs';
import {HashSuffixPipe} from 'src/app/pipes/hash-suffix.pipe';
import {SystemService} from 'src/app/services/system.service';
import {eASICModel} from 'src/models/enum/eASICModel';
import {ISystemInfo} from 'src/models/ISystemInfo';
import {UIChart} from "primeng/chart";

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent {
  @ViewChild('chart') chart!: UIChart;

  public info$: Observable<ISystemInfo>;

  public quickLink$: Observable<string | undefined>;
  public expectedHashRate$: Observable<number | undefined>;


  public chartOptions: any;
  public timeLabel: number[] = [];
  public hashrateData: number[] = [];
  public asicTempData: number[] = [];
  public vcoreTempData: number[] = [];
  public chartData?: any;

  constructor(
    private systemService: SystemService
  ) {


    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');
    const primaryColor = documentStyle.getPropertyValue('--primary-color');

    this.chartData = {
      labels: [],
      datasets: [
        {
          type: 'line',
          label: 'Hashrate',
          data: [],
          fill: false,
          backgroundColor: rgb(248, 4, 4),
          borderColor: rgb(248, 4, 4),
          tension: .4,
          pointRadius: 1,
          borderWidth: 1,
          yAxisID: 'y_hashrate'
        },
        {
          type: 'line',
          label: 'ASIC Temp',
          data: [],
          fill: false,
          backgroundColor:rgb(64, 245, 9),
          borderColor: rgb(64, 245, 9),
          tension: .4,
          pointRadius: 1,
          borderWidth: 1,
          yAxisID: 'y_temp'
        },
        {
          type: 'line',
          label: 'VCore Temp',
          data: [],
          fill: false,
          backgroundColor: rgb(8, 23, 236),
          borderColor: rgb(8, 23, 236),
          tension: .4,
          pointRadius: 1,
          borderWidth: 1,
          yAxisID: 'y_temp'
        }
      ]
    };

    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          labels: {
            color: textColor
          }
        }
      },
      scales: {
        x: {
          type: 'time',
          time: {
            unit: 'hour',
          },
          ticks: {
            color: textColorSecondary
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false,
            display: true
          }
        },
        y_hashrate: {
          position: 'left',
          title: {
            display: true,
            text: 'Hashrate'
          },
          ticks: {
            color: rgb(211, 211, 211),
            callback: (value: number) => HashSuffixPipe.transform(value)
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          }
        },
        y_temp: {
          position: 'right',
          title: {
            display: true,
            text: 'Temperature (°C)'
          },
          min: 35,
          max: 85,
          ticks: {
            color: rgb(211, 211, 211)
          },
          grid: {
            drawOnChartArea: false
          }
        }
      }
    };





    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      tap(info => {

        this.hashrateData.push(info.hashRate * 1000000000);
        this.asicTempData.push(info.temp);
        this.vcoreTempData.push(info.vrTemp);
        this.timeLabel.push(new Date().getTime());

        if (this.hashrateData.length > 2000) {
          this.hashrateData.shift();
          this.asicTempData.shift();
          this.vcoreTempData.shift();
          this.timeLabel.shift();
        }

        this.chartData.labels = this.timeLabel;
        this.chartData.datasets[0].data = this.hashrateData;
        this.chartData.datasets[1].data = this.asicTempData;
        this.chartData.datasets[2].data = this.vcoreTempData;

      // 触发更新
        if (this.chart && this.chart.chart) {
          this.chart.chart.update();
        }else {
          this.chartData = {
            ...this.chartData
          };
        }

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
function rgb(r: number, g: number, b: number): string {
  return `rgb(${r}, ${g}, ${b})`;
}
