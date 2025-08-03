import { Component, OnInit, OnDestroy, ViewChild, ElementRef } from '@angular/core';
import { SystemService, StatusHistoryResponse } from 'src/app/services/system.service';
import { interval, Subscription } from 'rxjs';

@Component({
  selector: 'app-monitor-chart',
  templateUrl: './monitor-chart.component.html',
  styleUrls: ['./monitor-chart.component.scss']
})
export class MonitorChartComponent implements OnInit, OnDestroy {
  @ViewChild('chart') chart!: any; // PrimeNG Chart component reference

  public chartData: any = {};
  public chartOptions: any = {};
  public loading = true;
  public sampleInterval = 10; // Default sampling interval
  public dataCount = 0;
  public totalDataPoints = 0;
  public realtimeInterval = 30; // Current realtime update interval in seconds
  public realtimeCountdown = 30; // Countdown timer for next update (will be initialized properly)

  private realtimeSubscription?: Subscription;
  private countdownSubscription?: Subscription;
  private realtimeIntervalMap = new Map<number, number>([
    [1, 5],   // High detail: update every 5 seconds for continuous display
    [5, 15],  // Normal detail: update every 15 seconds
    [10, 30], // Fast mode: update every 30 seconds (default)
    [20, 60]  // Low detail: update every 60 seconds
  ]);

  constructor(private systemService: SystemService) {
    // Initialize realtime interval and countdown based on default sample interval
    this.realtimeInterval = this.realtimeIntervalMap.get(this.sampleInterval) || 30;
    this.realtimeCountdown = this.realtimeInterval;
    this.initializeChart();
  }

  ngOnInit(): void {
    this.loadHistoryData();
    // Start adaptive realtime updates based on sample interval
    this.startAdaptiveRealtimeUpdate();
  }

  ngOnDestroy(): void {
    if (this.realtimeSubscription) {
      this.realtimeSubscription.unsubscribe();
    }
    if (this.countdownSubscription) {
      this.countdownSubscription.unsubscribe();
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

    // Force chart update
    if (this.chart && this.chart.chart) {
      this.chart.chart.update('none'); // No animation update
    }
  }

  private startAdaptiveRealtimeUpdate(): void {
    // Get adaptive interval based on current sample rate
    this.realtimeInterval = this.realtimeIntervalMap.get(this.sampleInterval) || 30;
    this.realtimeCountdown = this.realtimeInterval; // Initialize countdown
    
    console.log(`🔄 Starting adaptive realtime updates: ${this.realtimeInterval}s interval for sample rate 1/${this.sampleInterval}`);
    
    if (this.realtimeSubscription) {
      this.realtimeSubscription.unsubscribe();
    }
    if (this.countdownSubscription) {
      this.countdownSubscription.unsubscribe();
    }
    
    // Start countdown timer (updates every second)
    this.startCountdownTimer();
    
    this.realtimeSubscription = interval(this.realtimeInterval * 1000).subscribe(() => {
      // Get latest data point and add to chart
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
      
      // Reset countdown after update
      this.realtimeCountdown = this.realtimeInterval;
    });
  }

  private startCountdownTimer(): void {
    this.countdownSubscription = interval(1000).subscribe(() => {
      if (this.realtimeCountdown > 0) {
        this.realtimeCountdown--;
      } else {
        this.realtimeCountdown = this.realtimeInterval; // Reset if it goes below 0
      }
    });
  }

  private startRealtimeUpdate(): void {
    // Legacy method - replaced by startAdaptiveRealtimeUpdate
    this.startAdaptiveRealtimeUpdate();
  }

  private addRealtimeData(dataPoint: any[]): void {
    if (dataPoint.length >= 12) {
      const timestamp = dataPoint[11];
      const hashrate = typeof dataPoint[0] === 'string' ? parseFloat(dataPoint[0]) : dataPoint[0];
      const asicTemp = typeof dataPoint[1] === 'string' ? parseFloat(dataPoint[1]) : dataPoint[1];
      const vcoreTemp = typeof dataPoint[2] === 'string' ? parseFloat(dataPoint[2]) : dataPoint[2];

      // Add new data point
      this.chartData.labels.push(new Date(timestamp).toLocaleTimeString());
      this.chartData.datasets[0].data.push(hashrate);
      this.chartData.datasets[1].data.push(asicTemp);
      this.chartData.datasets[2].data.push(vcoreTemp);

      // Limit displayed data points to maintain chart performance
      // Adjust max points based on sample interval for consistent time window
      const maxPoints = Math.floor(14400 / this.sampleInterval);
      if (this.chartData.labels.length > maxPoints) {
        this.chartData.labels.shift();
        this.chartData.datasets.forEach((dataset: any) => dataset.data.shift());
      }

      // Update chart
      if (this.chart && this.chart.chart) {
        this.chart.chart.update('none');
      }
    }
  }

  public onSampleIntervalChange(newInterval: any): void {
    // Ensure newInterval is a number (ngModel might bind as string)
    this.sampleInterval = Number(newInterval);
    
    console.log('Sample interval changed to:', this.sampleInterval, 'type:', typeof this.sampleInterval);
    
    // Update realtime interval and countdown based on new sample interval
    this.realtimeInterval = this.realtimeIntervalMap.get(this.sampleInterval) || 30;
    this.realtimeCountdown = this.realtimeInterval; // Reset countdown to new interval
    
    this.loadHistoryData();
    
    // Restart realtime updates with new adaptive interval
    this.startAdaptiveRealtimeUpdate();
    
    console.log(`📊 Sample interval changed to 1/${this.sampleInterval}, realtime updates now every ${this.realtimeInterval}s`);
  }

  public refreshData(): void {
    this.loadHistoryData();
  }
}
