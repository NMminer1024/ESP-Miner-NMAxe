import { Component, OnInit, OnDestroy, ViewChild, ElementRef } from '@angular/core';
import { SystemService, StatusHistoryResponse } from 'src/app/services/system.service';
import { interval, Subscription } from 'rxjs';

@Component({
  selector: 'app-monitor-chart',
  templateUrl: './monitor-chart.component.html',
  styleUrls: ['./monitor-chart.component.scss']
})
export class MonitorChartComponent implements OnInit, OnDestroy {
  @ViewChild('chart') chart!: ElementRef;

  public chartData: any = {};
  public chartOptions: any = {};
  public loading = true;
  public sampleInterval = 10; // 默认采样间隔
  public dataCount = 0;
  public totalDataPoints = 0;

  private realtimeSubscription?: Subscription;

  constructor(private systemService: SystemService) {
    this.initializeChart();
  }

  ngOnInit(): void {
    this.loadHistoryData();
    // 每30秒更新一次实时数据
    this.startRealtimeUpdate();
  }

  ngOnDestroy(): void {
    if (this.realtimeSubscription) {
      this.realtimeSubscription.unsubscribe();
    }
  }

  private initializeChart(): void {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');

    this.chartData = {
      labels: [],
      datasets: [
        {
          label: 'Hashrate (GH/s)',
          data: [],
          fill: false,
          borderColor: '#007bff',
          backgroundColor: '#007bff',
          tension: 0.1,
          pointRadius: 0,
          pointHoverRadius: 4,
          borderWidth: 2,
          yAxisID: 'y'
        },
        {
          label: 'ASIC Temp (°C)',
          data: [],
          fill: false,
          borderColor: '#ff6b6b',
          backgroundColor: '#ff6b6b',
          tension: 0.1,
          pointRadius: 0,
          pointHoverRadius: 4,
          borderWidth: 2,
          yAxisID: 'y1'
        },
        {
          label: 'VCore Temp (°C)',
          data: [],
          fill: false,
          borderColor: '#ffa726',
          backgroundColor: '#ffa726',
          tension: 0.1,
          pointRadius: 0,
          pointHoverRadius: 4,
          borderWidth: 2,
          yAxisID: 'y1'
        }
      ]
    };

    this.chartOptions = {
      responsive: true,
      maintainAspectRatio: false,
      interaction: {
        intersect: false,
        mode: 'index'
      },
      animation: {
        duration: 0 // 禁用动画以提高性能
      },
      plugins: {
        legend: {
          display: true,
          position: 'top'
        },
        tooltip: {
          enabled: true,
          mode: 'index',
          intersect: false,
          backgroundColor: 'rgba(42, 42, 42, 0.9)',
          titleColor: '#ffffff',
          bodyColor: '#ffffff',
          borderColor: '#404040',
          borderWidth: 1,
          cornerRadius: 8,
          displayColors: true,
          callbacks: {
            title: (context: any) => {
              if (context[0]?.label) {
                return `Time: ${context[0].label}`;
              }
              return '';
            },
            label: (context: any) => {
              const datasetLabel = context.dataset.label;
              let value = context.parsed.y;
              
              if (datasetLabel.includes('Hashrate')) {
                return `${datasetLabel}: ${value.toFixed(1)} GH/s`;
              } else {
                return `${datasetLabel}: ${value.toFixed(1)}°C`;
              }
            }
          }
        }
      },
      scales: {
        x: {
          type: 'time',
          time: {
            unit: 'hour',
            displayFormats: {
              hour: 'HH:mm'
            }
          },
          ticks: {
            color: textColorSecondary,
            maxTicksLimit: 12
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          }
        },
        y: {
          type: 'linear',
          display: true,
          position: 'left',
          title: {
            display: true,
            text: 'Hashrate (GH/s)',
            color: textColor
          },
          ticks: {
            color: textColorSecondary
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          }
        },
        y1: {
          type: 'linear',
          display: true,
          position: 'right',
          title: {
            display: true,
            text: 'Temperature (°C)',
            color: textColor
          },
          ticks: {
            color: textColorSecondary
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false,
            drawOnChartArea: false
          }
        }
      }
    };
  }

  private loadHistoryData(): void {
    this.loading = true;
    console.log(`Loading history data with sample interval: ${this.sampleInterval}`);
    
    this.systemService.getStatusHistory(this.sampleInterval).subscribe({
      next: (response: StatusHistoryResponse) => {
        console.log('History data loaded:', response);
        this.processHistoryData(response);
        this.loading = false;
      },
      error: (error) => {
        console.error('Error loading history data:', error);
        this.loading = false;
      }
    });
  }

  private processHistoryData(response: StatusHistoryResponse): void {
    if (!response.statistics || response.statistics.length === 0) {
      console.log('No statistics data available');
      return;
    }

    this.totalDataPoints = response.size || 0;
    this.dataCount = response.sampledSize || response.statistics.length;

    const timeLabels: number[] = [];
    const hashrateData: number[] = [];
    const asicTempData: number[] = [];
    const vcoreTempData: number[] = [];

    response.statistics.forEach((dataPoint: any[], index: number) => {
      if (dataPoint.length >= 12) {
        timeLabels.push(dataPoint[11]); // epoch timestamp (ms)
        hashrateData.push(typeof dataPoint[0] === 'string' ? parseFloat(dataPoint[0]) : dataPoint[0]); // GH/s
        asicTempData.push(typeof dataPoint[1] === 'string' ? parseFloat(dataPoint[1]) : dataPoint[1]); // °C
        vcoreTempData.push(typeof dataPoint[2] === 'string' ? parseFloat(dataPoint[2]) : dataPoint[2]); // °C
      }
    });

    this.updateChart(timeLabels, hashrateData, asicTempData, vcoreTempData);
  }

  private updateChart(timeLabels: number[], hashrateData: number[], asicTempData: number[], vcoreTempData: number[]): void {
    this.chartData = {
      labels: timeLabels.map(timestamp => new Date(timestamp).toLocaleTimeString()),
      datasets: [
        {
          ...this.chartData.datasets[0],
          data: hashrateData
        },
        {
          ...this.chartData.datasets[1],
          data: asicTempData
        },
        {
          ...this.chartData.datasets[2],
          data: vcoreTempData
        }
      ]
    };

    // 强制更新图表
    if (this.chart && this.chart.chart) {
      this.chart.chart.update('none'); // 无动画更新
    }
  }

  private startRealtimeUpdate(): void {
    this.realtimeSubscription = interval(30000).subscribe(() => {
      // 每30秒获取最新数据点并添加到图表
      this.systemService.getStatusRealtime().subscribe({
        next: (response: StatusHistoryResponse) => {
          if (response.statistics && response.statistics.length > 0) {
            this.addRealtimeData(response.statistics[0]);
          }
        },
        error: (error) => {
          console.error('Error updating realtime data:', error);
        }
      });
    });
  }

  private addRealtimeData(dataPoint: any[]): void {
    if (dataPoint.length >= 12) {
      const timestamp = dataPoint[11];
      const hashrate = typeof dataPoint[0] === 'string' ? parseFloat(dataPoint[0]) : dataPoint[0];
      const asicTemp = typeof dataPoint[1] === 'string' ? parseFloat(dataPoint[1]) : dataPoint[1];
      const vcoreTemp = typeof dataPoint[2] === 'string' ? parseFloat(dataPoint[2]) : dataPoint[2];

      // 添加新数据点
      this.chartData.labels.push(new Date(timestamp).toLocaleTimeString());
      this.chartData.datasets[0].data.push(hashrate);
      this.chartData.datasets[1].data.push(asicTemp);
      this.chartData.datasets[2].data.push(vcoreTemp);

      // 限制显示的数据点数量，保持图表性能
      const maxPoints = Math.floor(14400 / this.sampleInterval);
      if (this.chartData.labels.length > maxPoints) {
        this.chartData.labels.shift();
        this.chartData.datasets.forEach((dataset: any) => dataset.data.shift());
      }

      // 更新图表
      if (this.chart && this.chart.chart) {
        this.chart.chart.update('none');
      }
    }
  }

  public onSampleIntervalChange(newInterval: number): void {
    this.sampleInterval = newInterval;
    this.loadHistoryData();
  }

  public refreshData(): void {
    this.loadHistoryData();
  }
}
