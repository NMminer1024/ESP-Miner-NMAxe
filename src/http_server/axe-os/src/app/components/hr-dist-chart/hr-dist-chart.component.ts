import { Component, OnInit, ViewChild, AfterViewInit } from '@angular/core';
import { Observable, interval, startWith, switchMap, tap } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { UIChart } from "primeng/chart";

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
export class HrDistChartComponent implements OnInit, AfterViewInit {
  @ViewChild('chart') chart!: UIChart;

  public chartData: any;
  public chartOptions: any;
  public distData$: Observable<HashrateDistribution>;
  public scale: number = 0;
  public samplingTime: number = 0;
  public samplingCount: number = 0;
  public scaleUnit: string = 'GH/s';
  public formattedTime: string = '0s';
  private currentMaxHr: number = 0;
  private currentMaxBars: number = 0;

  constructor(private systemService: SystemService) {
    // 初始化图表数据
    this.chartData = {
      labels: [],
      datasets: [
        {
          type: 'bar',
          label: 'Hashrate Distribution (%)',
          data: [],
          backgroundColor: 'rgba(54, 162, 235, 0.6)',
          borderColor: 'rgba(54, 162, 235, 1)',
          borderWidth: 1
        }
      ]
    };

    // 初始化图表选项
    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');

    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false, // 禁用宽高比约束，让图表填满容器
      responsive: true,           // 启用响应式，适应容器变化
      aspectRatio: 0,            // 设置为0，完全忽略宽高比
      resizeDelay: 50,           // 调整大小延迟
      devicePixelRatio: 1,       // 控制像素比，防止高分辨率设备上的拉伸
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
              // 使用简单的计算，避免this引用
              const maxHr = 1000; // 默认值，实际会在运行时更新
              const maxBars = 20; // 默认值，实际会在运行时更新
              const scaleValue = maxHr / maxBars;
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
      // 使用与monitor组件相同的最小layout padding
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
              size: 11  // 小号字体保留标题
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
            color: surfaceBorder,
            drawBorder: false
          },
          border: {
            display: false
          }
        },
        y: {
          title: {
            display: false  // 移除Y轴标题
          },
          min: 0,
          max: 100,
          ticks: {
            color: textColorSecondary,
            font: {
              size: 10
            },
            callback: (value: number) => `${value}%`,
            stepSize: 20  // 减少Y轴标签密度
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          }
        }
      }
    };

    // 设置数据流
    this.distData$ = interval(10000).pipe( // 每10秒更新一次
      startWith(() => this.systemService.getHashrateDistribution()),
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

  ngOnInit(): void {
    // 组件初始化时启动数据流
    this.distData$.subscribe();
  }

  ngAfterViewInit(): void {
    // 等待视图初始化完成后设置Canvas样式
    setTimeout(() => {
      this.setupCanvasStyles();
    }, 100);
  }

  private setupCanvasStyles(): void {
    if (this.chart && this.chart.chart) {
      const canvas = this.chart.chart.canvas;
      if (canvas) {
        // 强制设置Canvas样式，确保填满容器
        canvas.style.width = '100%';
        canvas.style.height = '100%';
        canvas.style.maxWidth = '100%';
        canvas.style.maxHeight = '100%';
        canvas.style.display = 'block';
        
        console.log('HR Chart Canvas styles set for responsive mode');
        
        // 强制重新调整图表大小
        this.chart.chart.resize();
      }
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
    this.chartData.labels = labels;
    this.chartData.datasets[0].data = values;

    // 更新 tooltip 回调函数以使用当前数据
    if (this.chart && this.chart.chart && this.chart.chart.options.plugins?.tooltip) {
      const componentContext = this;
      const currentData = data;
      
      this.chart.chart.options.plugins.tooltip.callbacks = {
        title: function(context: any[]) {
          const index = context[0].dataIndex;
          const scaleValue = currentData.max_hr / currentData.max_bars;
          const rangeStart = index * scaleValue;
          const rangeEnd = (index + 1) * scaleValue;
          
          if (scaleValue >= 1000) {
            return `Range: ${(rangeStart/1000).toFixed(1)}-${(rangeEnd/1000).toFixed(1)} TH/s`;
          } else {
            return `Range: ${Math.round(rangeStart)}-${Math.round(rangeEnd)} GH/s`;
          }
        },
        label: function(context: any) {
          const percentage = context.parsed.y;
          return `Percentage: ${percentage.toFixed(1)}%`;
        }
      };
    }

    // 触发图表更新
    if (this.chart && this.chart.chart) {
      this.chart.chart.update();
    } else {
      this.chartData = { ...this.chartData };
    }
  }
}
