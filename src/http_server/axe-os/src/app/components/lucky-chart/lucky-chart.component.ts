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
  private readonly MAX_VISIBLE_BARS = 48; // 最多显示48根柱子
  private readonly ENABLE_SCROLL_THRESHOLD = 20; // 超过20根柱子时启用滚动
  private windowStart = 0; // 窗口起始索引
  
  // 拖拽相关属性
  private isDragging = false;
  private dragStartX = 0;
  private dragStartWindowStart = 0;
  private lastMouseX = 0; // 记录上次鼠标位置
  private dragSensitivity = 6; // 拖拽灵敏度（像素/柱子）
  private currentPosition = 0; // 当前浮点位置
  private lastDragUpdateTime = 0; // 上次拖拽更新时间
  private updateThrottle = 16; // 更新节流（毫秒），约60fps
  
  // 奖牌相关属性
  private medalData: { index: number, type: 'gold' | 'silver' | 'bronze' }[] = [];
  
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
    
    // 计算奖牌数据
    this.calculateMedals();
    
    console.log(`📊 Sliding window: ${this.windowStart} to ${windowEnd}, showing ${this.luckyData.length} items`);
  }

  /**
   * 计算当前显示数据中share_diff的前三名，生成奖牌数据
   */
  private calculateMedals(): void {
    // 清空之前的奖牌数据
    this.medalData = [];
    
    if (this.luckyData.length === 0) return;
    
    // 创建索引数组并按share_diff从高到低排序
    const sortedIndices = this.luckyData
      .map((item, index) => ({ index, share_diff: item.share_diff }))
      .filter(item => item.share_diff > 0) // 过滤掉无效数据
      .sort((a, b) => b.share_diff - a.share_diff); // 从高到低排序
    
    // 分配奖牌（前三名）
    if (sortedIndices.length >= 1) {
      this.medalData.push({ index: sortedIndices[0].index, type: 'gold' });
    }
    if (sortedIndices.length >= 2) {
      this.medalData.push({ index: sortedIndices[1].index, type: 'silver' });
    }
    if (sortedIndices.length >= 3) {
      this.medalData.push({ index: sortedIndices[2].index, type: 'bronze' });
    }
    
    console.log(`🏆 Medals calculated:`, this.medalData.map(m => 
      `${m.type} at index ${m.index} (share_diff: ${this.luckyData[m.index].share_diff})`
    ));
  }

  /**
   * 获取奖牌样式信息
   */
  private getMedalStyle(type: 'gold' | 'silver' | 'bronze'): { icon: string, color: string } {
    switch (type) {
      case 'gold':
        return { icon: '🥇', color: '#FFD700' }; // 金色
      case 'silver':
        return { icon: '🥈', color: '#C0C0C0' }; // 银色
      case 'bronze':
        return { icon: '🥉', color: '#CD7F32' }; // 铜色
      default:
        return { icon: '🏆', color: '#808080' };
    }
  }

  /**
   * 创建奖牌绘制插件
   */
  private createMedalPlugin() {
    const component = this; // 保持对组件的引用
    
    return {
      id: 'medalPlugin',
      afterDatasetsDraw: function(chart: any) {
        const ctx = chart.ctx;
        const chartArea = chart.chartArea;
        
        // 遍历奖牌数据
        component.medalData.forEach(medal => {
          // 获取数据点的meta信息
          const meta = chart.getDatasetMeta(0); // share_diff是第一个数据集（bar类型）
          if (!meta.data[medal.index]) return;
          
          const element = meta.data[medal.index];
          const medalStyle = component.getMedalStyle(medal.type);
          
          // 计算奖牌位置（柱子顶部上方）
          const x = element.x;
          const y = element.y - 25; // 柱子顶部上方25像素
          
          // 绘制奖牌图标
          ctx.save();
          ctx.font = '20px Arial';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          
          // 绘制背景圆圈（可选）
          ctx.beginPath();
          ctx.arc(x, y, 15, 0, 2 * Math.PI);
          ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
          ctx.fill();
          ctx.strokeStyle = medalStyle.color;
          ctx.lineWidth = 2;
          ctx.stroke();
          
          // 绘制奖牌图标
          ctx.fillStyle = medalStyle.color;
          ctx.fillText(medalStyle.icon, x, y);
          
          ctx.restore();
        });
        
        // 绘制庆祝撒花图标（当share_diff >= net_diff时）
        component.luckyData.forEach((item, index) => {
          if (item.share_diff >= item.net_diff && item.share_diff > 0) {
            const meta = chart.getDatasetMeta(0);
            if (!meta.data[index]) return;
            
            const element = meta.data[index];
            const x = element.x;
            const y = element.y - 50; // 柱子顶部上方50像素（比奖牌更高）
            
            ctx.save();
            ctx.font = '24px Arial';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            
            // 绘制撒花图标
            ctx.fillText('🎉', x, y);
            
            ctx.restore();
          }
        });
      }
    };
  }

  /**
   * 获取柱子颜色数组，根据奖牌状态设置不同颜色
   */
  private getBarColors(): string[] {
    const colors: string[] = [];
    const defaultColor = 'rgba(54, 162, 235, 0.6)'; // 默认蓝色
    
    // 为每个数据点设置颜色
    for (let i = 0; i < this.luckyData.length; i++) {
      let color = defaultColor;
      
      // 检查是否是奖牌得主
      const medal = this.medalData.find(m => m.index === i);
      if (medal) {
        switch (medal.type) {
          case 'gold':
            color = 'rgba(255, 215, 0, 0.8)'; // 金色
            break;
          case 'silver':
            color = 'rgba(192, 192, 192, 0.8)'; // 银色
            break;
          case 'bronze':
            color = 'rgba(205, 127, 50, 0.8)'; // 铜色
            break;
        }
      }
      
      colors.push(color);
    }
    
    return colors;
  }

  /**
   * 获取柱子边框颜色数组
   */
  private getBarBorderColors(): string[] {
    const colors: string[] = [];
    const defaultBorderColor = 'rgba(54, 162, 235, 1)'; // 默认蓝色边框
    
    // 为每个数据点设置边框颜色
    for (let i = 0; i < this.luckyData.length; i++) {
      let color = defaultBorderColor;
      
      // 检查是否是奖牌得主
      const medal = this.medalData.find(m => m.index === i);
      if (medal) {
        switch (medal.type) {
          case 'gold':
            color = 'rgba(255, 215, 0, 1)'; // 金色边框
            break;
          case 'silver':
            color = 'rgba(192, 192, 192, 1)'; // 银色边框
            break;
          case 'bronze':
            color = 'rgba(205, 127, 50, 1)'; // 铜色边框
            break;
        }
      }
      
      colors.push(color);
    }
    
    return colors;
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
          backgroundColor: this.getBarColors(), // 使用动态颜色数组
          borderColor: this.getBarBorderColors(), // 使用动态边框颜色数组
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
      backgroundColor: 'rgba(248, 248, 248, 0.05)', // 淡色背景，提高对比度
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
            maxTicksLimit: 10,
            color: 'rgba(255, 255, 255, 0.8)' // 提高X轴文字的对比度
          },
          grid: {
            color: 'rgba(255, 255, 255, 0.2)', // 明亮的网格线
            lineWidth: 1
          }
        },
        y: {
          type: 'logarithmic',
          display: true,
          title: {
            display: true,
            text: 'Difficulty',
            color: 'rgba(255, 255, 255, 0.8)' // 提高Y轴标题对比度
          },
          min: logMin,
          max: logMax * 10, // Add some padding above max value
          ticks: {
            color: 'rgba(255, 255, 255, 0.8)', // 提高Y轴刻度文字的对比度
            callback: function(value: any) {
              return formatNumber(Number(value));
            }
          },
          grid: {
            color: 'rgba(255, 255, 255, 0.3)', // 更明亮的Y轴网格线
            lineWidth: 1
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
      options: chartOptions,
      plugins: [this.createMedalPlugin()] // 添加奖牌插件
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

  private updateChart(disableAnimation: boolean = false): void {
    if (!this.chart || !this.isComponentActive) return;

    console.log('🔄 Updating lucky chart...');

    // Update labels and data
    this.chart.data.labels = this.luckyData.map(d => formatTimestamp(d.epoch));
    
    if (this.chart.data.datasets[0]) {
      this.chart.data.datasets[0].data = this.luckyData.map(d => d.share_diff > 0 ? d.share_diff : 0.1);
      // 更新柱子颜色
      this.chart.data.datasets[0].backgroundColor = this.getBarColors();
      this.chart.data.datasets[0].borderColor = this.getBarBorderColors();
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

    // 重新计算奖牌数据（当数据直接更新时，如实时更新）
    this.calculateMedals();

    // 根据是否禁用动画来更新图表
    if (disableAnimation) {
      this.chart.update('none'); // 'none'模式禁用所有动画
    } else {
      this.chart.update();
    }
    
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
    this.lastMouseX = event.clientX;
    this.dragStartWindowStart = this.windowStart;
    this.currentPosition = this.windowStart; // 初始化浮点位置
    
    if (this.chartCanvas && this.chartCanvas.nativeElement) {
      this.chartCanvas.nativeElement.style.cursor = 'grabbing';
    }
    
    // 阻止默认行为，避免选中文本
    event.preventDefault();
    event.stopPropagation();
  };

  private onMouseMove = (event: MouseEvent): void => {
    if (!this.isDragging) return;
    
    // 节流：避免过度频繁的更新
    const now = Date.now();
    
    // 计算鼠标移动距离
    const deltaX = event.clientX - this.lastMouseX;
    this.lastMouseX = event.clientX;
    
    // 根据拖拽灵敏度计算移动的柱子数（支持小数）
    const moveDistance = deltaX / this.dragSensitivity;
    
    // 更新浮点位置（反向移动）
    this.currentPosition = Math.max(0, Math.min(
      this.currentPosition - moveDistance,
      this.originalLuckyData.length - this.MAX_VISIBLE_BARS
    ));
    
    // 节流更新：只有超过一定时间间隔才更新图表
    if (now - this.lastDragUpdateTime >= this.updateThrottle) {
      const newWindowStart = Math.round(this.currentPosition);
      
      // 只有当整数位置改变时才更新图表
      if (newWindowStart !== this.windowStart) {
        this.windowStart = newWindowStart;
        this.applySlidingWindow();
        this.updateChart(true); // 拖拽时禁用动画，保持柱子高度
        
        console.log(`🖱️ Dragging to position: ${this.windowStart} (float: ${this.currentPosition.toFixed(2)})`);
      }
      
      this.lastDragUpdateTime = now;
    }
    
    event.preventDefault();
  };

  private onMouseUp = (): void => {
    if (!this.isDragging) return;
    
    this.isDragging = false;
    
    // 确保最终位置是正确的整数位置
    const finalPosition = Math.round(this.currentPosition);
    if (finalPosition !== this.windowStart) {
      this.windowStart = finalPosition;
      this.applySlidingWindow();
      this.updateChart(true); // 拖拽结束时也禁用动画，确保平滑过渡
    }
    
    if (this.chartCanvas && this.chartCanvas.nativeElement) {
      this.chartCanvas.nativeElement.style.cursor = 'grab';
    }
    
    console.log(`🖱️ Drag ended at position: ${this.windowStart}`);
  };
}