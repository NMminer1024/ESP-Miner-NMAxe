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

// 格式化share值，专门用于显示在柱子顶端，固定显示4位数字
function formatShareValue(num: number): string {
  const units = ['', 'K', 'M', 'G', 'T', 'P', 'E'];
  let unitIndex = 0;
  let value = num;
  
  while (value >= 1000 && unitIndex < units.length - 1) {
    value /= 1000;
    unitIndex++;
  }
  
  // 根据数值大小决定小数位数，确保总共显示4位数字
  let decimals = 0;
  if (value >= 100) {
    decimals = 1; // 123.4K
  } else if (value >= 10) {
    decimals = 2; // 12.34K
  } else {
    decimals = 3; // 1.234K
  }
  
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
  
  luckyData: any[] = []; // 用于图表显示的数据
  private lastDataHash = ''; // 记录上次数据的哈希值，用于判断是否需要更新
  chart: Chart | null = null;
  isLoading = false;
  lastUpdateTime = '';
  dataSize = 0;
  
  // 奖牌相关属性
  private medalData: { 
    index: number, 
    type: 'gold' | 'silver' | 'bronze',
    globalIndex?: number,
    share_diff?: number
  }[] = [];
  
  private subscription: Subscription = new Subscription();
  private isComponentActive = true;
  
  // 闪烁动画相关属性
  private animationFrame: number | null = null;
  private startTime: number = Date.now();
  
  constructor(private systemService: SystemService) {}

  ngOnInit(): void {
    console.log('🎯 Lucky Chart component ngOnInit called');
    this.loadLuckyHistory();
    
    // 设置每5秒请求一次历史数据
    this.subscription.add(
      interval(5000).pipe(
        takeWhile(() => this.isComponentActive)
      ).subscribe(() => {
        if (!this.isLoading) {
          this.loadLuckyHistory(false); // 传入false表示不显示loading状态
        }
      })
    );
  }

  ngAfterViewInit(): void {
    console.log('🎯 Lucky Chart ngAfterViewInit called');
    // Chart will be initialized after data is loaded
  }

  ngOnDestroy(): void {
    console.log('🎯 Lucky Chart component destroyed');
    this.isComponentActive = false;
    this.subscription.unsubscribe();
    
    // 停止闪烁动画
    this.stopBlinkAnimation();
    
    if (this.chart) {
      this.chart.destroy();
    }
  }

  private loadLuckyHistory(showLoading: boolean = true): void {
    if (!this.isComponentActive) return;

    if (showLoading) {
      this.isLoading = true;
    }
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
          if (showLoading) {
            this.isLoading = false;
          }
          throw error;
        })
      ).subscribe({
        next: (response: StatusHistoryResponse) => {
          console.log('✅ Lucky history loaded successfully:', response);
          this.processLuckyData(response);
          
          if (showLoading) {
            this.isLoading = false;
          }
          this.lastUpdateTime = formatTimestamp(Date.now());
          
          // 计算当前数据的简单哈希值来判断数据是否变化
          const currentDataHash = this.calculateDataHash();
          const dataChanged = currentDataHash !== this.lastDataHash;
          this.lastDataHash = currentDataHash;
          
          if (!this.chart) {
            this.initializeChart();
          } else if (dataChanged) {
            this.updateChart(true); // 禁用动画以避免闪烁
          } else {
            console.log('📊 No data changes detected, skipping chart update');
          }
        },
        error: (error: any) => {
          console.error('❌ Error loading lucky history:', error);
          if (showLoading) {
            this.isLoading = false;
          }
        }
      })
    );
  }

  private processLuckyData(response: StatusHistoryResponse): void {
    // 安全检查响应数据
    if (!response || !response.statistics || !Array.isArray(response.statistics)) {
      console.error('❌ Invalid lucky data response:', response);
      this.luckyData = [];
      return;
    }

    console.log(`📈 Processing ${response.statistics.length} lucky data points...`);

    // 安全地处理数据，添加更多验证
    this.luckyData = response.statistics
      .map((stat: any[]) => {
        // 确保stat是数组且有足够元素
        if (!Array.isArray(stat) || stat.length < 4) {
          console.warn('⚠️ Invalid stat entry:', stat);
          return null;
        }
        
        const proximity = Number(stat[0]) || 0;
        const share_diff = Number(stat[1]) || 0;
        const net_diff = Number(stat[2]) || 1;
        const epoch = Number(stat[3]) || 0;
        
        // 验证数据有效性
        if (epoch <= 0 || net_diff <= 0) {
          console.warn('⚠️ Invalid data values:', { proximity, share_diff, net_diff, epoch });
          return null;
        }
        
        return { proximity, share_diff, net_diff, epoch };
      })
      .filter(item => item !== null) // 过滤掉无效数据
      .sort((a, b) => a!.epoch - b!.epoch); // 按时间排序
    
    console.log(`📈 Processed ${this.luckyData.length} valid lucky data points`);
    
    // 记录总数据数量
    this.dataSize = this.luckyData.length;
    
    // 如果没有有效数据，清空显示
    if (this.luckyData.length === 0) {
      console.warn('⚠️ No valid data to display');
      return;
    }
    
    // 计算奖牌数据
    this.calculateMedals();
    
    console.log(`📊 Display data: ${this.luckyData.length} points`);
  }

  /**
   * 计算数据的简单哈希值用于检测数据变化
   */
  private calculateDataHash(): string {
    if (this.luckyData.length === 0) return '';
    
    // 使用数据长度、第一个和最后一个元素的关键信息生成简单哈希
    const firstItem = this.luckyData[0];
    const lastItem = this.luckyData[this.luckyData.length - 1];
    
    const hashString = `${this.luckyData.length}_${firstItem.epoch}_${firstItem.share_diff}_${lastItem.epoch}_${lastItem.share_diff}`;
    
    // 简单的字符串哈希函数
    let hash = 0;
    for (let i = 0; i < hashString.length; i++) {
      const char = hashString.charCodeAt(i);
      hash = ((hash << 5) - hash) + char;
      hash = hash & hash; // 转换为32位整数
    }
    
    return hash.toString();
  }

  /**
   * 计算所有数据中share_diff的前三名，生成奖牌数据
   */
  private calculateMedals(): void {
    // 清空之前的奖牌数据
    this.medalData = [];
    
    if (this.luckyData.length === 0) return;
    
    // 从所有数据中找出前三名
    const sortedIndices = this.luckyData
      .map((item: any, index: number) => ({ index, share_diff: item.share_diff }))
      .filter((item: any) => item.share_diff > 0) // 过滤掉无效数据
      .sort((a: any, b: any) => b.share_diff - a.share_diff); // 从高到低排序
    
    const medalTypes: ('gold' | 'silver' | 'bronze')[] = ['gold', 'silver', 'bronze'];
    
    // 生成前三名的奖牌数据
    for (let i = 0; i < Math.min(3, sortedIndices.length); i++) {
      const index = sortedIndices[i].index;
      this.medalData.push({ 
        index: index, 
        type: medalTypes[i],
        share_diff: sortedIndices[i].share_diff
      });
    }
    
    console.log(`🏆 Generated ${this.medalData.length} medals:`, 
      this.medalData.map((m: any) => 
        `${m.type} at index ${m.index} (share_diff: ${m.share_diff})`
      ).join(', ')
    );
  }

  /**
   * 获取奖牌样式信息
   */
  private getMedalStyle(type: 'gold' | 'silver' | 'bronze'): { icon: string, number: string, color: string, bgColor: string } {
    switch (type) {
      case 'gold':
        return { icon: '🥇', number: '1', color: '#FFFFFF', bgColor: '#FFD700' }; // 金牌图标+数字
      case 'silver':
        return { icon: '🥈', number: '2', color: '#FFFFFF', bgColor: '#C0C0C0' }; // 银牌图标+数字
      case 'bronze':
        return { icon: '🥉', number: '3', color: '#FFFFFF', bgColor: '#CD7F32' }; // 铜牌图标+数字
      default:
        return { icon: '?', number: '?', color: '#FFFFFF', bgColor: '#808080' };
    }
  }

  /**
   * 获取响应式的奖牌尺寸（根据屏幕宽度调整）
   */
  private getMedalSizes(): { iconSize: number, numberSize: number, offset: number } {
    const screenWidth = window.innerWidth;
    if (screenWidth <= 480) {
      // 小屏幕：更小的奖牌
      return { iconSize: 20, numberSize: 10, offset: 4 };
    } else if (screenWidth <= 768) {
      // 中等屏幕：中等尺寸奖牌
      return { iconSize: 26, numberSize: 13, offset: 5 };
    } else {
      // 大屏幕：正常尺寸
      return { iconSize: 32, numberSize: 16, offset: 6 };
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
        
        // 获取响应式奖牌尺寸
        const medalSizes = component.getMedalSizes();
        
        // 遍历奖牌数据
        component.medalData.forEach(medal => {
          // 获取数据点的meta信息
          const meta = chart.getDatasetMeta(0); // share_diff是第一个数据集（bar类型）
          if (!meta.data[medal.index]) return;
          
          // 检查这个数据点是否share_diff >= net_diff，如果是则不显示奖牌
          const item = component.luckyData[medal.index];
          if (item && item.share_diff >= item.net_diff) return;
          
          const element = meta.data[medal.index];
          const medalStyle = component.getMedalStyle(medal.type);
          
          // 计算奖牌位置（柱子顶部上方，整体向上移动更多距离避免重叠）
          const x = element.x;
          const offsetY = medalSizes.iconSize <= 20 ? 25 : (medalSizes.iconSize <= 26 ? 35 : 45); // 增加向上偏移
          const y = element.y - offsetY;
          
          // 绘制奖牌图标
          ctx.save();
          
          // 绘制奖牌图标（emoji）- 使用响应式大小
          ctx.font = `${medalSizes.iconSize}px Arial`;
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText(medalStyle.icon, x, y);
          
          // 在奖牌图标上叠加数字 - 使用响应式大小
          ctx.font = `bold ${medalSizes.numberSize}px Arial`;
          ctx.fillStyle = '#000000'; // 黑色数字以确保可见性
          ctx.strokeStyle = '#FFFFFF'; // 白色描边增加对比度
          ctx.lineWidth = medalSizes.iconSize <= 20 ? 1 : 2; // 小屏幕使用更细的描边
          
          // 先绘制描边，再绘制填充（数字往上移动1像素）
          ctx.strokeText(medalStyle.number, x, y + medalSizes.offset - 1);
          ctx.fillText(medalStyle.number, x, y + medalSizes.offset - 1);
          
          // 在奖牌下方、柱子顶端上面显示share_diff值
          const shareValue = medal.share_diff || 0;
          const shareText = formatShareValue(shareValue); // 使用新的格式化函数，固定显示4位数字
          const shareTextSize = medalSizes.iconSize <= 20 ? 8 : (medalSizes.iconSize <= 26 ? 10 : 12); // 调整字体大小，比原来更小
          
          ctx.font = `bold ${shareTextSize}px Arial`;
          ctx.fillStyle = '#FFFFFF'; // 改为白色
          ctx.strokeStyle = '#000000'; // 黑色描边确保在任何背景下都清晰
          ctx.lineWidth = 2;
          
          // 在奖牌下方显示share值，位置在柱子顶端上面一点点
          // element.y 是柱子顶端，我们在上面5-10像素显示share值
          const shareY = element.y - (medalSizes.iconSize <= 20 ? 5 : (medalSizes.iconSize <= 26 ? 8 : 10));
          ctx.strokeText(shareText, x, shareY);
          ctx.fillText(shareText, x, shareY);
          
          ctx.restore();
        });
        
        // 为share_diff >= net_diff的数据点显示难度值在柱子顶部（不显示奖牌和撒花）
        component.luckyData.forEach((item, index) => {
          if (item.share_diff >= item.net_diff && item.share_diff > 0) {
            const meta = chart.getDatasetMeta(0);
            if (!meta.data[index]) return;
            
            const element = meta.data[index];
            const x = element.x;
            
            const shareValue = item.share_diff;
            const shareText = formatShareValue(shareValue);
            const shareTextSize = 12; // 固定字体大小
            
            ctx.save();
            ctx.font = `bold ${shareTextSize}px Arial`;
            ctx.fillStyle = '#FFFFFF'; // 白色文字
            ctx.strokeStyle = '#000000'; // 黑色描边
            ctx.lineWidth = 2;
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            
            // 显示在柱子顶端上方10像素
            const shareY = element.y - 10;
            ctx.strokeText(shareText, x, shareY);
            ctx.fillText(shareText, x, shareY);
            
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
    const defaultColor = 'rgba(54, 162, 235, 0.8)'; // 提高默认颜色透明度
    
    // 计算闪烁效果的颜色因子（1秒周期的正弦波）
    const currentTime = Date.now();
    const elapsed = (currentTime - this.startTime) / 1000; // 转换为秒
    const colorFactor = 0.5 + 0.5 * Math.sin(elapsed * 2 * Math.PI); // 0-1之间变化
    
    // 为每个数据点设置颜色
    for (let i = 0; i < this.luckyData.length; i++) {
      let color = defaultColor;
      const item = this.luckyData[i];
      
      // 检查是否share_diff >= net_diff（Lucky状态）
      if (item && item.share_diff >= item.net_diff && item.share_diff > 0) {
        // Lucky状态：使用更丰富的彩虹色渐变闪烁
        // 色相在0度(红色)到300度(紫色)之间循环变化，创造完整的彩虹效果
        const hue = colorFactor * 300; // 从红色(0)到紫色(300)
        const saturation = 90 + colorFactor * 10; // 饱和度90%-100%，保持鲜艳
        const lightness = 55 + Math.sin(elapsed * 4 * Math.PI) * 5; // 亮度50%-60%，有额外的快速闪烁
        const alpha = 0.85 + colorFactor * 0.15; // 透明度0.85-1.0
        
        color = `hsla(${hue}, ${saturation}%, ${lightness}%, ${alpha})`;
      } else {
        // 检查是否是奖牌得主（只有非Lucky状态才显示奖牌）
        const medal = this.medalData.find(m => m.index === i);
        if (medal) {
          switch (medal.type) {
            case 'gold':
              color = 'rgba(255, 215, 0, 0.9)'; // 增强金色透明度
              break;
            case 'silver':
              color = 'rgba(192, 192, 192, 0.9)'; // 增强银色透明度
              break;
            case 'bronze':
              color = 'rgba(205, 127, 50, 0.9)'; // 增强铜色透明度
              break;
          }
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
    
    // 计算闪烁效果的颜色因子（与填充颜色同步）
    const currentTime = Date.now();
    const elapsed = (currentTime - this.startTime) / 1000;
    const colorFactor = 0.5 + 0.5 * Math.sin(elapsed * 2 * Math.PI);
    
    // 为每个数据点设置边框颜色
    for (let i = 0; i < this.luckyData.length; i++) {
      let color = defaultBorderColor;
      const item = this.luckyData[i];
      
      // 检查是否share_diff >= net_diff（Lucky状态）
      if (item && item.share_diff >= item.net_diff && item.share_diff > 0) {
        // Lucky状态：使用更丰富的彩虹色渐变边框（与填充色同步但稍暗）
        const hue = colorFactor * 300; // 从红色(0)到紫色(300)
        const saturation = 95 + colorFactor * 5; // 饱和度95%-100%，比填充色更鲜艳
        const lightness = 40 + Math.sin(elapsed * 4 * Math.PI) * 5; // 亮度35%-45%，比填充色暗一些形成对比
        
        color = `hsl(${hue}, ${saturation}%, ${lightness}%)`;
      } else {
        // 检查是否是奖牌得主（只有非Lucky状态才显示奖牌）
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
      }
      
      colors.push(color);
    }
    
    return colors;
  }

  /**
   * 创建现代化柱子样式插件
   */
  private createModernBarPlugin() {
    const component = this;
    
    return {
      id: 'modernBarPlugin',
      beforeDraw: function(chart: any) {
        // 绘制虚线网格
        const ctx = chart.ctx;
        const yAxis = chart.scales.y;
        const xAxis = chart.scales.x;
        const chartArea = chart.chartArea;
        
        if (!yAxis || !chartArea) return;
        
        ctx.save();
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.15)';
        ctx.lineWidth = 1;
        ctx.setLineDash([5, 5]); // 虚线样式：5像素实线，5像素空白
        
        // 绘制Y轴网格线
        yAxis.ticks.forEach((tick: any, index: number) => {
          const y = yAxis.getPixelForTick(index);
          ctx.beginPath();
          ctx.moveTo(chartArea.left, y);
          ctx.lineTo(chartArea.right, y);
          ctx.stroke();
        });
        
        ctx.restore();
      },
      beforeDatasetDraw: function(chart: any, args: any) {
        const { ctx, chartArea } = chart;
        const meta = chart.getDatasetMeta(0); // share_diff柱子数据集
        
        // 检查数据集是否可见，如果隐藏则不绘制
        if (!meta || !meta.data || meta.hidden) return;
        
        ctx.save();
        
        // 为每个柱子添加圆角和阴影效果
        meta.data.forEach((element: any, index: number) => {
          if (!element || element.skip) return;
          
          const { x, y, base, width } = element;
          const barHeight = Math.abs(base - y);
          
          if (barHeight <= 0) return;
          
          // 获取柱子颜色
          const medal = component.medalData.find(m => m.index === index);
          let fillColor = 'rgba(54, 162, 235, 0.8)';
          
          if (medal) {
            switch (medal.type) {
              case 'gold':
                fillColor = 'rgba(255, 215, 0, 0.9)';
                break;
              case 'silver':
                fillColor = 'rgba(192, 192, 192, 0.9)';
                break;
              case 'bronze':
                fillColor = 'rgba(205, 127, 50, 0.9)';
                break;
            }
          }
          
          // 创建渐变效果
          const gradient = ctx.createLinearGradient(0, y, 0, base);
          gradient.addColorStop(0, fillColor);
          gradient.addColorStop(1, fillColor.replace(/[\d\.]+\)$/g, '0.4)')); // 底部更透明
          
          // 绘制圆角矩形
          const cornerRadius = 4; // 圆角半径
          const barX = x - width / 2;
          const barY = Math.min(y, base);
          
          ctx.beginPath();
          ctx.roundRect(barX, barY, width, barHeight, [cornerRadius, cornerRadius, 0, 0]); // 只有顶部圆角
          ctx.fillStyle = gradient;
          ctx.fill();
          
          // 添加细微的边框
          ctx.strokeStyle = fillColor.replace(/[\d\.]+\)$/g, '1)');
          ctx.lineWidth = 1;
          ctx.stroke();
        });
        
        ctx.restore();
      }
    };
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

    // 安全检查数据
    if (!this.luckyData || this.luckyData.length === 0) {
      console.warn('No lucky data available for chart initialization');
      return;
    }

    console.log('📊 Initializing lucky chart...');

    // Calculate appropriate log scale range with safe fallbacks
    const maxNetDiff = this.luckyData.length > 0 ? Math.max(...this.luckyData.map(d => d.net_diff || 1)) : 1;
    const maxShareDiff = this.luckyData.length > 0 ? Math.max(...this.luckyData.map(d => d.share_diff || 512)) : 512;
    const logMax = Math.max(maxNetDiff, maxShareDiff);
    
    // 恢复固定最小值为1，简化逻辑
    const logMin = 1;

    const chartData: ChartData = {
      labels: this.luckyData.map(d => formatTimestamp(d.epoch)),
      datasets: [
        {
          type: 'bar' as const,
          label: 'Share Difficulty',
          data: this.luckyData.map(d => d.share_diff > 0 ? d.share_diff : 0.1), // Use 0.1 as minimum for log scale display
          backgroundColor: this.getBarColors(), // 使用实际颜色而非透明
          borderColor: this.getBarBorderColors(), // 使用实际边框色
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
      // 优化动画配置以减少闪烁
      animation: {
        duration: 300, // 减少动画时间
        easing: 'easeOutQuart'
      },
      // 移除硬编码的背景色，使用CSS变量
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
          display: false // 禁用图例，简化界面操作
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
            display: false // 禁用默认网格，使用自定义插件绘制
          }
        }
      },
      // 简化的鼠标悬停处理
      onHover: (event: any, elements: any) => {
        // 不再需要拖拽功能
        event.native.target.style.cursor = 'default';
      }
    };

    const config: ChartConfiguration = {
      type: 'bar',
      data: chartData,
      options: chartOptions,
      plugins: [this.createModernBarPlugin(), this.createMedalPlugin()] // 添加现代化柱子插件和奖牌插件
    };

    this.chart = new Chart(ctx, config);
    
    console.log('✅ Lucky chart initialized successfully');
    
    // 启动闪烁动画
    this.startBlinkAnimation();
  }

  private updateChart(disableAnimation: boolean = false): void {
    if (!this.chart || !this.isComponentActive) return;

    console.log('🔄 Updating lucky chart...');

    // 先更新数据和样式
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

    // 更新Y轴范围
    if (this.luckyData.length > 0) {
      const maxNetDiff = Math.max(...this.luckyData.map(d => d.net_diff));
      const maxShareDiff = Math.max(...this.luckyData.map(d => d.share_diff));
      const logMax = Math.max(maxNetDiff, maxShareDiff, 1000); // 至少1000
      const logMin = 1; // 固定最小值为1

      if (this.chart.options?.scales?.['y']) {
        this.chart.options.scales['y'].min = logMin;
        this.chart.options.scales['y'].max = logMax * 2; // 减少范围避免过大
      }
    }

    // 重新计算奖牌数据
    this.calculateMedals();

    // 使用更平滑的更新方式
    if (disableAnimation) {
      this.chart.update('none'); // 完全禁用动画
    } else {
      this.chart.update('active'); // 使用较轻的动画模式
    }
    
    console.log('✅ Lucky chart updated successfully');
  }

  /**
   * 启动闪烁动画
   */
  private startBlinkAnimation(): void {
    if (!this.isComponentActive) return;
    
    const animate = () => {
      if (!this.isComponentActive || !this.chart) return;
      
      // 检查是否有需要闪烁的Lucky柱子
      const hasLuckyBars = this.luckyData.some(item => 
        item.share_diff >= item.net_diff && item.share_diff > 0
      );
      
      if (hasLuckyBars) {
        // 更新柱子颜色（会触发重绘）
        if (this.chart.data.datasets[0]) {
          this.chart.data.datasets[0].backgroundColor = this.getBarColors();
          this.chart.data.datasets[0].borderColor = this.getBarBorderColors();
          this.chart.update('none'); // 无动画更新以获得流畅的闪烁效果
        }
      }
      
      // 继续下一帧动画
      this.animationFrame = requestAnimationFrame(animate);
    };
    
    this.animationFrame = requestAnimationFrame(animate);
  }

  /**
   * 停止闪烁动画
   */
  private stopBlinkAnimation(): void {
    if (this.animationFrame) {
      cancelAnimationFrame(this.animationFrame);
      this.animationFrame = null;
    }
  }

}