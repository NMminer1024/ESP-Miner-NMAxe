import { Component, OnInit, ViewChild, AfterViewInit, ElementRef, OnDestroy } from '@angular/core';
import { Observable, interval, startWith, switchMap, tap, Subscription } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { Chart, ChartConfiguration, ChartData, ChartOptions, registerables } from 'chart.js';

// 注册Chart.js组件
Chart.register(...registerables);

interface HashrateDistribution {
  max_bars: number;
  max_hr: number;
  times: number;
  dura: number;
  dist: { [key: string]: number };
}

@Component({
  selector: 'app-hr-dist-chart',
  templateUrl: './hr-dist-chart.component.html',
  styleUrls: ['./hr-dist-chart.component.scss']
})
export class HrDistChartComponent implements OnInit, AfterViewInit, OnDestroy {
  @ViewChild('chartCanvas', { static: false }) chartCanvas!: ElementRef<HTMLCanvasElement>;

  // Chart.js实例
  private chart: Chart | null = null;
  private dataSubscription: Subscription | null = null;

  public distData$!: Observable<HashrateDistribution>; // 延迟初始化
  public scale: number = 0;
  public samplingTime: number = 0;
  public samplingCount: number = 0;
  public scaleUnit: string = 'GH/s';
  public formattedTime: string = '0s';
  private currentMaxHr: number = 0;
  private currentMaxBars: number = 0;

  constructor(private systemService: SystemService) {
    // Constructor中不创建数据流，将在ngOnInit中创建
  }

  ngOnInit(): void {
    // 创建数据流但不立即订阅
    this.distData$ = interval(10000).pipe(
      switchMap(() => this.systemService.getHashrateDistribution()),
      tap((data: HashrateDistribution) => {
        this.updateChart(data);
        this.updateScaleAndUnit(data.max_hr, data.max_bars);
        this.currentMaxHr = data.max_hr;
        this.currentMaxBars = data.max_bars;
        this.samplingTime = data.dura;
        this.samplingCount = data.times;
        this.formattedTime = this.formatTime(data.dura);
      })
    );
  }

  ngAfterViewInit(): void {
    // 等待视图初始化完成后初始化图表并立即加载数据
    setTimeout(() => {
      this.initChart();
      this.loadInitialData(); // 立即加载第一次数据
      this.startPeriodicUpdates(); // 然后启动定期更新
    }, 100);
  }

  ngOnDestroy(): void {
    // 清理资源
    if (this.dataSubscription) {
      this.dataSubscription.unsubscribe();
    }
    if (this.chart) {
      this.chart.destroy();
    }
  }

  private initChart(): void {
    if (!this.chartCanvas) return;

    const ctx = this.chartCanvas.nativeElement.getContext('2d');
    if (!ctx) return;

    // 获取主题颜色
    const documentStyle = getComputedStyle(document.documentElement);
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary') || '#cccccc';
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border') || 'rgba(255, 255, 255, 0.1)';

    const chartConfig: ChartConfiguration = {
      type: 'bar',
      data: {
        labels: [],
        datasets: [{
          label: 'Hashrate Distribution (%)',
          data: [],
          backgroundColor: 'rgba(54, 162, 235, 0.6)',
          borderColor: 'rgba(54, 162, 235, 1)',
          borderWidth: 1
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        resizeDelay: 50,
        devicePixelRatio: window.devicePixelRatio || 1,
        interaction: {
          intersect: false,
          mode: 'index'
        },
        plugins: {
          legend: {
            display: false
          },
          title: {
            display: false
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
                const index = context[0].dataIndex;
                const scaleValue = this.currentMaxBars > 0 ? this.currentMaxHr / this.currentMaxBars : 0;
                const rangeStart = index * scaleValue;
                const rangeEnd = (index + 1) * scaleValue;
                
                if (scaleValue >= 1000) {
                  return `Range: ${(rangeStart/1000).toFixed(1)}-${(rangeEnd/1000).toFixed(1)} TH/s`;
                } else {
                  return `Range: ${Math.round(rangeStart)}-${Math.round(rangeEnd)} GH/s`;
                }
              },
              label: (context: any) => {
                return `Time Distribution: ${context.parsed.y}%`;
              }
            }
          }
        },
        layout: {
          padding: {
            top: 2,
            right: 2,
            bottom: 2,
            left: 2
          }
        },
        scales: {
          x: {
            title: {
              display: true,
              text: 'Hashrate',
              color: textColorSecondary,
              font: {
                size: 11
              }
            },
            ticks: {
              color: textColorSecondary,
              font: {
                size: 10
              },
              maxRotation: 45,
              minRotation: 0
            },
            grid: {
              color: surfaceBorder
            },
            border: {
              display: false
            }
          },
          y: {
            title: {
              display: false
            },
            min: 0,
            max: 100,
            ticks: {
              color: textColorSecondary,
              font: {
                size: 10
              },
              callback: (value: any) => `${value}%`,
              stepSize: 20
            },
            grid: {
              color: surfaceBorder
            }
          }
        }
      }
    };

    this.chart = new Chart(ctx, chartConfig);
    this.setupCanvasStyles();
  }

  private loadInitialData(): void {
    console.log('🚀 Loading initial hashrate distribution data');
    // 立即获取一次数据
    this.systemService.getHashrateDistribution().subscribe({
      next: (data: HashrateDistribution) => {
        this.updateChart(data);
        this.updateScaleAndUnit(data.max_hr, data.max_bars);
        this.currentMaxHr = data.max_hr;
        this.currentMaxBars = data.max_bars;
        this.samplingTime = data.dura;
        this.samplingCount = data.times;
        this.formattedTime = this.formatTime(data.dura);
        console.log('✅ Initial hashrate distribution data loaded');
      },
      error: (error) => {
        console.error('❌ Failed to load initial hashrate distribution data:', error);
      }
    });
  }

  private startPeriodicUpdates(): void {
    console.log('⏰ Starting periodic hashrate distribution updates');
    // 启动定期更新（从第二次开始）
    this.dataSubscription = this.distData$.subscribe({
      next: (data) => {
        console.log('🔄 Periodic hashrate distribution data updated');
      },
      error: (error) => {
        console.error('❌ Periodic update failed:', error);
      }
    });
  }

  private setupCanvasStyles(): void {
    if (this.chart && this.chartCanvas) {
      const canvas = this.chartCanvas.nativeElement;
      // 强制设置Canvas样式，确保填满容器
      canvas.style.width = '100%';
      canvas.style.height = '100%';
      canvas.style.maxWidth = '100%';
      canvas.style.maxHeight = '100%';
      canvas.style.display = 'block';
      
      console.log('HR Chart Canvas styles set for responsive mode');
      
      // 强制重新调整图表大小
      this.chart.resize();
    }
  }

  private getScaleValue(): number {
    return this.currentMaxBars > 0 ? this.currentMaxHr / this.currentMaxBars : 0;
  }

  private updateScaleAndUnit(maxHr: number, maxBars: number): void {
    const scaleValue = maxHr / maxBars;
    
    if (scaleValue >= 1000) {
      this.scale = Math.round(scaleValue / 1000);
      this.scaleUnit = 'TH/s';
    } else {
      this.scale = Math.round(scaleValue);
      this.scaleUnit = 'GH/s';
    }
  }

  private formatTime(seconds: number): string {
    if (seconds < 100) {
      return `${seconds}s`;
    }
    
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const remainingSeconds = seconds % 60;
    
    if (hours > 0) {
      return `${hours}h ${minutes}m ${remainingSeconds}s`;
    } else {
      return `${minutes}m ${remainingSeconds}s`;
    }
  }

  private updateChart(data: HashrateDistribution): void {
    if (!this.chart) return;

    const labels: string[] = [];
    const values: number[] = [];

    // 根据 max_bars 生成标签和数据
    for (let i = 0; i < data.max_bars; i++) {
      const keyStr = i.toString();
      const percentage = data.dist[keyStr] || 0;
      
      // 计算实际的算力范围标签
      const scaleValue = data.max_hr / data.max_bars;
      const rangeStart = i * scaleValue;
      const rangeEnd = (i + 1) * scaleValue;
      
      // 智能单位转换
      if (scaleValue >= 1000) {
        // 使用 TH/s
        labels.push(`${(rangeStart/1000).toFixed(1)}-${(rangeEnd/1000).toFixed(1)}`);
      } else {
        // 使用 GH/s
        labels.push(`${Math.round(rangeStart)}-${Math.round(rangeEnd)}`);
      }
      
      values.push(percentage);
    }

    // 更新图表数据
    this.chart.data.labels = labels;
    this.chart.data.datasets[0].data = values;

    // 更新图表
    this.chart.update('none'); // 使用'none'模式避免动画
  }
}
