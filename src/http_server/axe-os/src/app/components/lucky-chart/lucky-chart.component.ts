import { Component, OnInit, OnDestroy, ViewChild, ElementRef, AfterViewInit } from '@angular/core';
import { SystemService, StatusHistoryResponse } from '../../services/system.service';
import { Subscription, interval, timer } from 'rxjs';
import { takeWhile, retryWhen, delay, take, mergeMap, catchError } from 'rxjs/operators';
import { Chart, ChartConfiguration, ChartData, ChartOptions, registerables, LogarithmicScale } from 'chart.js';
import 'chartjs-adapter-moment';

// 注册Chart.js组件
Chart.register(...registerables);

// 格式化数字为KMGT格式
function formatNumber(num: number): string {
  const units = ['', 'K', 'M', 'G', 'T', 'P', 'E'];
  let unitIndex = 0;
  let value = num;
  
  while (value >= 1000 && unitIndex < units.length - 1) {
    value /= 1000;
    unitIndex++;
  }
  
  // 根据数值大小决定小数位数
  let decimals = 0;
  if (value < 10) decimals = 2;
  else if (value < 100) decimals = 1;
  
  return value.toFixed(decimals) + units[unitIndex];
}

// 格式化时间戳为24小时格式
function formatTimestamp(timestamp: number): string {
  const date = new Date(timestamp);
  return date.toLocaleTimeString('en-GB', { 
    hour12: false,
    hour: '2-digit', 
    minute: '2-digit',
    second: '2-digit'
  });
}

@Component({
  selector: 'app-lucky-chart',
  templateUrl: './lucky-chart.component.html',
  styleUrls: ['./lucky-chart.component.scss']
})
export class LuckyChartComponent implements OnInit, AfterViewInit, OnDestroy {
  
  @ViewChild('chartCanvas', { static: false }) chartCanvas!: ElementRef<HTMLCanvasElement>;
  
  luckyData: any[] = []; // 用于图表显示的数据（可能包含填充的gaps）
  originalLuckyData: any[] = []; // 原始数据（未填充gaps）
  chart: Chart | null = null;
  isLoading = false;
  lastUpdateTime = '';
  dataSize = 0;
  
  // 滑动窗口配置
  private readonly MAX_VISIBLE_BARS = 30; // 最多显示30根柱子
  private readonly ENABLE_SCROLL_THRESHOLD = 20; // 超过20根柱子时启用滚动
  private windowStart = 0; // 窗口起始索引
  
  // 拖拽相关属性
  private isDragging = false;
  private dragStartX = 0;
  private dragStartWindowStart = 0;
  
  private subscription: Subscription = new Subscription();
  private realTimeSubscription: Subscription = new Subscription();
  private isComponentActive = true;
  
  constructor(private systemService: SystemService) {}

  ngOnInit(): void {
    console.log('🎯 Lucky Chart component ngOnInit called');
    this.loadLuckyHistory();
  }

  ngAfterViewInit(): void {
    console.log('🎯 Lucky Chart ngAfterViewInit called');
    // Chart will be initialized after data is loaded
    
    // 添加拖拽事件监听器
    this.setupDragListeners();
  }

  ngOnDestroy(): void {
    console.log('🎯 Lucky Chart component destroyed');
    this.isComponentActive = false;
    this.subscription.unsubscribe();
    this.realTimeSubscription.unsubscribe();
    
    // 清理拖拽事件监听器
    if (this.chartCanvas && this.chartCanvas.nativeElement) {
      const canvas = this.chartCanvas.nativeElement;
      canvas.removeEventListener('mousedown', this.onMouseDown);
      canvas.removeEventListener('mousemove', this.onMouseMove);
      canvas.removeEventListener('mouseup', this.onMouseUp);
      canvas.removeEventListener('mouseleave', this.onMouseUp);
    }
    
    if (this.chart) {
      this.chart.destroy();
    }
  }

  private loadLuckyHistory(): void {
    if (!this.isComponentActive) return;

    this.isLoading = true;
    console.log('📊 Loading lucky history data...');

    this.subscription.add(
      this.systemService.getLuckyHistory().pipe(
        retryWhen(errors => 
          errors.pipe(
            delay(1000),
            take(3),
            mergeMap((error: any, index: number) => {
              console.warn(`🔄 Lucky history request failed (attempt ${index + 1}/3):`, error);
              if (index === 2) {
                throw error;
              }
              return timer(2000);
            })
          )
        ),
        catchError((error: any) => {
          console.error('❌ Failed to load lucky history after retries:', error);
          this.isLoading = false;
          throw error;
        })
      ).subscribe({
        next: (response: StatusHistoryResponse) => {
          console.log('✅ Lucky history loaded successfully:', response);
          this.processLuckyData(response);
          this.isLoading = false;
          this.lastUpdateTime = formatTimestamp(Date.now());
          
          // Initialize chart after data is loaded
          if (!this.chart) {
            this.initializeChart();
          } else {
            this.updateChart();
          }
          
          // Start real-time updates every 5 seconds (like status chart)
          this.startRealTimeUpdates();
        },
        error: (error: any) => {
          console.error('❌ Error loading lucky history:', error);
          this.isLoading = false;
        }
      })
    );
  }

  private processLuckyData(response: StatusHistoryResponse): void {
    this.originalLuckyData = response.statistics.map((stat: any[]) => ({
      proximity: stat[0] || 0,
      share_diff: stat[1] || 0,
      net_diff: stat[2] || 1,
      epoch: stat[3] || 0  // 已经是毫秒时间戳
    }));
    
    console.log(`📈 Processed ${this.originalLuckyData.length} lucky data points`);
    
    // 记录总数据数量
    this.dataSize = this.originalLuckyData.length;
    
    // 设置初始窗口位置（显示最新数据）
    this.setInitialWindow();
    
    // 应用滑动窗口到显示数据
    this.applySlidingWindow();
    
    console.log(`📊 Window start: ${this.windowStart}, Total data: ${this.originalLuckyData.length}, Display data: ${this.luckyData.length}`);
  }

  private filterLast20Minutes(): void {
    if (this.luckyData.length === 0) return;
    
    const now = Date.now();
    const twentyMinutesAgo = now - (20 * 60 * 1000); // 20分钟前的时间戳
    
    // 同时过滤原始数据和显示数据
    this.originalLuckyData = this.originalLuckyData.filter(item => item.epoch >= twentyMinutesAgo);
    this.luckyData = this.luckyData.filter(item => item.epoch >= twentyMinutesAgo);
  }

  private setInitialWindow(): void {
    // 如果数据少于等于最大可见柱子数，显示全部
    if (this.originalLuckyData.length <= this.MAX_VISIBLE_BARS) {
      this.windowStart = 0;
    } else {
      // 显示最新的数据（从末尾开始）
      this.windowStart = Math.max(0, this.originalLuckyData.length - this.MAX_VISIBLE_BARS);
    }
  }

  private applySlidingWindow(): void {
    // 计算窗口结束位置
    const windowEnd = Math.min(this.windowStart + this.MAX_VISIBLE_BARS, this.originalLuckyData.length);
    
    // 提取窗口内的数据
    this.luckyData = this.originalLuckyData.slice(this.windowStart, windowEnd);
    
    console.log(`📊 Sliding window: ${this.windowStart} to ${windowEnd}, showing ${this.luckyData.length} items`);
  }

  private fillEpochGaps(): void {
    if (this.luckyData.length === 0) return;

    // Sort by epoch
    this.luckyData.sort((a, b) => a.epoch - b.epoch);
    
    const filledData: any[] = [];
    const minEpoch = this.luckyData[0].epoch;
    const maxEpoch = this.luckyData[this.luckyData.length - 1].epoch;
    
    // Create a map for quick lookup of existing data
    const dataMap = new Map<number, any>();
    this.luckyData.forEach(item => {
      dataMap.set(item.epoch, item);
    });
    
    // Fill gaps with null data points (for visual continuity)
    for (let epoch = minEpoch; epoch <= maxEpoch; epoch += 60) { // Assuming 1-minute intervals
      if (dataMap.has(epoch)) {
        filledData.push(dataMap.get(epoch)!);
      } else {
        filledData.push({
          proximity: 0,
          share_diff: 0,
          net_diff: this.getLastKnownNetDiff(epoch), // Use last known net diff for missing epochs
          epoch: epoch
        });
      }
    }
    
    this.luckyData = filledData;
    console.log(`📈 Filled gaps, total data points: ${this.luckyData.length}`);
  }

  private getLastKnownNetDiff(targetEpoch: number): number {
    // Find the last known net_diff before the target epoch
    for (let i = this.luckyData.length - 1; i >= 0; i--) {
      if (this.luckyData[i].epoch < targetEpoch && this.luckyData[i].net_diff > 0) {
        return this.luckyData[i].net_diff;
      }
    }
    return 1; // Default minimum value for log scale
  }

  private initializeChart(): void {
    if (!this.chartCanvas || !this.isComponentActive) return;

    const ctx = this.chartCanvas.nativeElement.getContext('2d');
    if (!ctx) return;

    console.log('📊 Initializing lucky chart...');

    // Calculate appropriate log scale range
    const maxNetDiff = Math.max(...this.luckyData.map(d => d.net_diff));
    const maxShareDiff = Math.max(...this.luckyData.map(d => d.share_diff));
    const logMax = Math.max(maxNetDiff, maxShareDiff);
    const logMin = Math.min(1, Math.min(...this.luckyData.filter(d => d.share_diff > 0).map(d => d.share_diff)));

    const chartData: ChartData = {
      labels: this.luckyData.map(d => formatTimestamp(d.epoch)),
      datasets: [
        {
          type: 'bar' as const,
          label: 'Share Difficulty',
          data: this.luckyData.map(d => d.share_diff > 0 ? d.share_diff : 0.1), // Use 0.1 as minimum for log scale display
          backgroundColor: 'rgba(54, 162, 235, 0.6)',
          borderColor: 'rgba(54, 162, 235, 1)',
          borderWidth: 1,
          yAxisID: 'y'
        },
        {
          type: 'line' as const,
          label: 'Network Difficulty',
          data: this.luckyData.map(d => d.net_diff > 0 ? d.net_diff : 1), // Use 1 as minimum for log scale display
          backgroundColor: 'transparent',
          borderColor: 'rgba(255, 99, 132, 1)',
          borderWidth: 2,
          fill: false,
          pointRadius: 0,
          pointHoverRadius: 4,
          yAxisID: 'y'
        }
      ]
    };

    const chartOptions: ChartOptions = {
      responsive: true,
      maintainAspectRatio: false,
      interaction: {
        mode: 'index',
        intersect: false,
      },
      plugins: {
        title: {
          display: true,
          text: 'Share and Network Difficulty (Logarithmic Scale)',
          font: {
            size: 16,
            weight: 'bold'
          }
        },
        legend: {
          display: true,
          position: 'top'
        },
        tooltip: {
          callbacks: {
            title: (context: any) => {
              const index = context[0].dataIndex;
              const epoch = this.luckyData[index].epoch;
              return formatTimestamp(epoch);
            },
            label: (context: any) => {
              const index = context.dataIndex;
              const label = context.dataset.label;
              
              if (label.includes('Share')) {
                // 显示真实的share_diff值和百分比
                const shareDiff = this.luckyData[index].share_diff;
                const netDiff = this.luckyData[index].net_diff;
                const percentage = ((shareDiff / netDiff) * 100).toFixed(12);
                return `${label}: ${formatNumber(shareDiff)} (${percentage}%)`;
              } else {
                const netDiff = this.luckyData[index].net_diff;
                return `${label}: ${formatNumber(netDiff)}`;
              }
            }
          }
        }
      },
      scales: {
        x: {
          display: true,
          title: {
            display: true,
            text: 'Time'
          },
          ticks: {
            maxTicksLimit: 10
          }
        },
        y: {
          type: 'logarithmic',
          display: true,
          title: {
            display: true,
            text: 'Difficulty'
          },
          min: logMin,
          max: logMax * 10, // Add some padding above max value
          ticks: {
            callback: function(value: any) {
              return formatNumber(Number(value));
            }
          }
        }
      },
      // 添加拖拽功能（当数据超过阈值时启用）
      onHover: (event: any, elements: any) => {
        if (this.originalLuckyData.length > this.ENABLE_SCROLL_THRESHOLD) {
          event.native.target.style.cursor = 'grab';
        }
      }
    };

    const config: ChartConfiguration = {
      type: 'bar',
      data: chartData,
      options: chartOptions
    };

    this.chart = new Chart(ctx, config);
    
    // 添加拖拽事件监听器（如果数据量超过阈值）
    if (this.originalLuckyData.length > this.ENABLE_SCROLL_THRESHOLD) {
      const canvas = this.chartCanvas.nativeElement;
      canvas.addEventListener('mousedown', this.onMouseDown);
      canvas.addEventListener('mousemove', this.onMouseMove);
      canvas.addEventListener('mouseup', this.onMouseUp);
      canvas.addEventListener('mouseleave', this.onMouseUp); // 鼠标离开也停止拖拽
      
      // 设置初始光标样式
      canvas.style.cursor = 'grab';
    }
    
    console.log('✅ Lucky chart initialized successfully');
  }

  private updateChart(): void {
    if (!this.chart || !this.isComponentActive) return;

    console.log('🔄 Updating lucky chart...');

    // Update labels and data
    this.chart.data.labels = this.luckyData.map(d => formatTimestamp(d.epoch));
    
    if (this.chart.data.datasets[0]) {
      this.chart.data.datasets[0].data = this.luckyData.map(d => d.share_diff > 0 ? d.share_diff : 0.1);
    }
    if (this.chart.data.datasets[1]) {
      this.chart.data.datasets[1].data = this.luckyData.map(d => d.net_diff > 0 ? d.net_diff : 1);
    }

    // Update scale ranges
    const maxNetDiff = Math.max(...this.luckyData.map(d => d.net_diff));
    const maxShareDiff = Math.max(...this.luckyData.map(d => d.share_diff));
    const logMax = Math.max(maxNetDiff, maxShareDiff);
    const logMin = Math.min(1, Math.min(...this.luckyData.filter(d => d.share_diff > 0).map(d => d.share_diff)));

    if (this.chart.options?.scales?.['y']) {
      this.chart.options.scales['y'].min = logMin;
      this.chart.options.scales['y'].max = logMax * 10;
    }

    this.chart.update();
    console.log('✅ Lucky chart updated successfully');
  }

  private startRealTimeUpdates(): void {
    if (!this.isComponentActive) return;

    console.log('🔄 Starting real-time updates for lucky chart (every 5 seconds)...');

    this.realTimeSubscription.add(
      interval(5000).pipe( // Update every 5 seconds (like status chart)
        takeWhile(() => this.isComponentActive)
      ).subscribe(() => {
        // Use real-time endpoint without showing loading spinner
        this.updateLuckyDataSilently();
      })
    );
  }

  private updateLuckyDataSilently(): void {
    if (!this.isComponentActive) return;

    this.systemService.getLuckyRealtime().pipe(
      catchError((error: any) => {
        console.warn('⚠️ Silent lucky data update failed:', error);
        return [];
      })
    ).subscribe((response: StatusHistoryResponse) => {
      if (response && response.statistics && response.statistics.length > 0) {
        console.log('🔄 Lucky data updated silently');
        this.appendRealTimeData(response);
        this.lastUpdateTime = formatTimestamp(Date.now());
        
        if (this.chart) {
          this.updateChart();
        }
      }
    });
  }

  private appendRealTimeData(response: StatusHistoryResponse): void {
    console.log('📊 Appending real-time lucky data...');
    
    // 转换新数据
    const newData = response.statistics.map((stat: any[]) => ({
      proximity: stat[0] || 0,
      share_diff: stat[1] || 0,
      net_diff: stat[2] || 1,
      epoch: stat[3] || 0  // 已经是毫秒时间戳
    }));

    if (newData.length === 0) return;

    // 获取原始数据中最新数据的时间戳
    const lastEpoch = this.originalLuckyData.length > 0 ? this.originalLuckyData[this.originalLuckyData.length - 1].epoch : 0;
    
    // 只添加比当前最新数据更新的数据点
    const newerData = newData.filter(item => item.epoch > lastEpoch);
    
    if (newerData.length > 0) {
      // 追加新数据到原始数据
      this.originalLuckyData.push(...newerData);
      console.log(`📈 Appended ${newerData.length} new lucky data points to original data`);
      
      // 确保原始数据按epoch排序
      this.originalLuckyData.sort((a, b) => a.epoch - b.epoch);
      
      // 更新数据大小
      this.dataSize = this.originalLuckyData.length;
      
      // 自动滚动到最新数据（如果当前显示的是最后的数据）
      const wasShowingLatest = (this.windowStart + this.MAX_VISIBLE_BARS) >= (this.originalLuckyData.length - newerData.length);
      if (wasShowingLatest) {
        this.setInitialWindow(); // 重新设置窗口到最新位置
      }
      
      // 重新应用滑动窗口
      this.applySlidingWindow();
      
      console.log(`📊 After real-time update, total data: ${this.originalLuckyData.length}, window: ${this.windowStart}-${this.windowStart + this.luckyData.length}`);
    } else {
      console.log('📊 No new data to append (duplicate or older timestamp)');
    }
  }

  private setupDragListeners(): void {
    // 鼠标拖拽事件将在图表创建后设置
    console.log('🖱️ Drag listeners setup completed');
  }

  private onMouseDown = (event: MouseEvent): void => {
    if (this.originalLuckyData.length <= this.ENABLE_SCROLL_THRESHOLD) return;
    
    this.isDragging = true;
    this.dragStartX = event.clientX;
    this.dragStartWindowStart = this.windowStart;
    
    if (this.chartCanvas && this.chartCanvas.nativeElement) {
      this.chartCanvas.nativeElement.style.cursor = 'grabbing';
    }
    
    event.preventDefault();
  };

  private onMouseMove = (event: MouseEvent): void => {
    if (!this.isDragging) return;
    
    const deltaX = event.clientX - this.dragStartX;
    const chartWidth = this.chartCanvas?.nativeElement?.width || 600;
    const pixelsPerBar = chartWidth / this.MAX_VISIBLE_BARS;
    const barsMoved = Math.round(deltaX / pixelsPerBar);
    
    const newWindowStart = Math.max(0, Math.min(
      this.dragStartWindowStart - barsMoved,
      this.originalLuckyData.length - this.MAX_VISIBLE_BARS
    ));
    
    if (newWindowStart !== this.windowStart) {
      this.windowStart = newWindowStart;
      this.applySlidingWindow();
      this.updateChart();
    }
  };

  private onMouseUp = (): void => {
    if (!this.isDragging) return;
    
    this.isDragging = false;
    
    if (this.chartCanvas && this.chartCanvas.nativeElement) {
      this.chartCanvas.nativeElement.style.cursor = 'grab';
    }
  };
}