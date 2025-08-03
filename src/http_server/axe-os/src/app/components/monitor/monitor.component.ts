import { Component, OnInit, OnDestroy, ViewChild, ElementRef, AfterViewInit } from '@angular/core';
import { SystemService } from '../../services/system.service';
import { Subscription, interval, timer, throwError } from 'rxjs';
import { takeWhile, retryWhen, delay, take, mergeMap, catchError } from 'rxjs/operators';
import { Chart, ChartConfiguration, ChartData, ChartOptions, registerables, TimeScale } from 'chart.js';
import 'chartjs-adapter-moment';

// 注册Chart.js组件
Chart.register(...registerables);

interface HistoryNode {
  hashrate: string;
  asic_temp: string;
  vcore_temp: string;
  pbus: string;
  vbus: string;
  ibus: string;
  vcore: number;
  fanspeed: number;
  fanrpm: number;
  wifi_rssi: number;
  free_heap: number;
  free_psram: number;
  epoch: number;
}

interface FieldOption {
  value: string;
  label: string;
  unit: string;
  type: 'string' | 'number';
  selected: boolean;
  color: string;
}

interface TimeRange {
  value: string;
  label: string;
  minutes: number;
}

@Component({
  selector: 'app-monitor',
  templateUrl: './monitor.component.html',
  styleUrls: ['./monitor.component.scss']
})
export class MonitorComponent implements OnInit, AfterViewInit, OnDestroy {
  
  @ViewChild('chartCanvas', { static: false }) chartCanvas!: ElementRef<HTMLCanvasElement>;
  
  historyData: HistoryNode[] = [];
  selectedFields: string[] = ['hashrate', 'asic_temp', 'vcore_temp'];
  selectedTimeRange: string = 'all'; // Default to show all history
  sampleInterval = 5; // Default sample interval - Normal resolution
  realtimeInterval = 15; // Current realtime update interval in seconds
  realtimeCountdown = 15; // Countdown timer for next update (will be initialized properly)
  
  chart: Chart | null = null;
  isLoading = false;
  lastUpdateTime = '';
  dataSize = 0;
  boardVersion = 'ESP-Miner'; // Default board version
  loadingMessage = 'Loading...';
  hasLoadingError = false;
  retryCount = 0;
  maxRetries = 3;
  
  private subscription: Subscription = new Subscription();
  private realTimeSubscription: Subscription = new Subscription();
  private countdownSubscription: Subscription = new Subscription();
  private isComponentActive = true;
  
  // Adaptive realtime interval mapping based on sample resolution
  private realtimeIntervalMap = new Map<number, number>([
    [1, 5],   // High detail: update every 5 seconds for continuous display
    [5, 15],  // Normal detail: update every 15 seconds
    [10, 30], // Fast mode: update every 30 seconds
    [20, 60]  // Low detail: update every 60 seconds
  ]);
  
  // 状态持久化键名
  private readonly STORAGE_KEYS = {
    SELECTED_FIELDS: 'monitor_selected_fields',
    TIME_RANGE: 'monitor_time_range',
    SAMPLE_INTERVAL: 'monitor_sample_interval',
    FIELD_OPTIONS: 'monitor_field_options'
  };
  
  // Field configuration options
  fieldOptions: FieldOption[] = [
    { value: 'hashrate', label: 'Hash Rate', unit: 'GH/s', type: 'string', selected: true, color: '#4CAF50' },
    { value: 'asic_temp', label: 'ASIC Temp', unit: '°C', type: 'string', selected: true, color: '#FF9800' },
    { value: 'vcore_temp', label: 'VCore Temp', unit: '°C', type: 'string', selected: true, color: '#FF5722' },
    { value: 'pbus', label: 'Power', unit: 'W', type: 'string', selected: false, color: '#2196F3' },
    { value: 'vbus', label: 'Voltage', unit: 'V', type: 'string', selected: false, color: '#9C27B0' },
    { value: 'ibus', label: 'Current', unit: 'A', type: 'string', selected: false, color: '#795548' },
    { value: 'vcore', label: 'VCore', unit: 'mV', type: 'number', selected: false, color: '#607D8B' },
    { value: 'fanspeed', label: 'Fan Speed', unit: '%', type: 'number', selected: false, color: '#3F51B5' },
    { value: 'fanrpm', label: 'Fan RPM', unit: 'RPM', type: 'number', selected: false, color: '#00BCD4' },
    { value: 'wifi_rssi', label: 'WiFi RSSI', unit: 'dBm', type: 'number', selected: false, color: '#CDDC39' },
    { value: 'free_heap', label: 'Free Heap', unit: 'KB', type: 'number', selected: false, color: '#FFC107' },
    { value: 'free_psram', label: 'Free PSRAM', unit: 'KB', type: 'number', selected: false, color: '#E91E63' }
  ];
  
  // Time range options
  timeRanges: TimeRange[] = [
    { value: 'all', label: 'All History', minutes: -1 },
    { value: '10', label: 'Last 10m', minutes: 10 },
    { value: '30', label: 'Last 30m', minutes: 30 },
    { value: '60', label: 'Last 1h', minutes: 60 },
    { value: '180', label: 'Last 3h', minutes: 180 },
    { value: '360', label: 'Last 6h', minutes: 360 },
    { value: '720', label: 'Last 12h', minutes: 720 },
    { value: '1440', label: 'Last 24h', minutes: 1440 }
  ];

  constructor(private systemService: SystemService) { 
    // Initialize realtime interval and countdown based on default sample interval
    this.realtimeInterval = this.realtimeIntervalMap.get(this.sampleInterval) || 15;
    this.realtimeCountdown = this.realtimeInterval;
  }

  ngOnInit(): void {
    console.log('🚀 Monitor component ngOnInit called');
    this.loadSavedState(); // 加载保存的状态
    this.initializeSelectedFields();
    this.loadSystemInfo();
    // Component will auto-start real-time updates after chart initialization
  }

  ngAfterViewInit(): void {
    console.log('🎯 Monitor component ngAfterViewInit called');
    // Ensure DOM is fully rendered before initializing chart
    setTimeout(() => {
      console.log('AfterViewInit - ViewChild status:', !!this.chartCanvas);
      if (this.chartCanvas) {
        console.log('Canvas element found:', this.chartCanvas.nativeElement);
        this.initChart();
        this.loadHistoryData();
      } else {
        console.error('Canvas ViewChild not available in AfterViewInit');
        // Retry
        setTimeout(() => {
          if (this.chartCanvas) {
            console.log('Canvas found on retry');
            this.initChart();
            this.loadHistoryData();
          } else {
            console.error('Canvas still not found after retry');
          }
        }, 500);
      }
    }, 100);
  }

  ngOnDestroy(): void {
    this.isComponentActive = false;
    this.subscription.unsubscribe();
    this.realTimeSubscription.unsubscribe();
    this.countdownSubscription.unsubscribe();
    this.stopRealTimeUpdates();
    if (this.chart) {
      this.chart.destroy();
    }
  }

  // 状态持久化管理方法
  private loadSavedState(): void {
    try {
      // 恢复选中的字段
      const savedFields = localStorage.getItem(this.STORAGE_KEYS.SELECTED_FIELDS);
      if (savedFields) {
        this.selectedFields = JSON.parse(savedFields);
      }

      // 恢复时间范围
      const savedTimeRange = localStorage.getItem(this.STORAGE_KEYS.TIME_RANGE);
      if (savedTimeRange) {
        this.selectedTimeRange = savedTimeRange;
      }

      // 恢复采样间隔
      const savedSampleInterval = localStorage.getItem(this.STORAGE_KEYS.SAMPLE_INTERVAL);
      if (savedSampleInterval) {
        this.sampleInterval = parseInt(savedSampleInterval, 10);
      }

      // 恢复字段选项状态
      const savedFieldOptions = localStorage.getItem(this.STORAGE_KEYS.FIELD_OPTIONS);
      if (savedFieldOptions) {
        const savedOptions = JSON.parse(savedFieldOptions);
        // 合并保存的状态与默认选项
        this.fieldOptions.forEach(option => {
          const saved = savedOptions.find((s: any) => s.value === option.value);
          if (saved) {
            option.selected = saved.selected;
          }
        });
      }

      console.log('状态已恢复:', {
        selectedFields: this.selectedFields,
        selectedTimeRange: this.selectedTimeRange,
        sampleInterval: this.sampleInterval
      });

      // 恢复状态后更新显示
      this.initializeSelectedFields();
      this.updateChart();

    } catch (error) {
      console.warn('恢复保存状态时出错:', error);
    }
  }

  private saveState(): void {
    try {
      // 保存选中的字段
      localStorage.setItem(this.STORAGE_KEYS.SELECTED_FIELDS, JSON.stringify(this.selectedFields));
      
      // 保存时间范围
      localStorage.setItem(this.STORAGE_KEYS.TIME_RANGE, this.selectedTimeRange);
      
      // 保存采样间隔
      localStorage.setItem(this.STORAGE_KEYS.SAMPLE_INTERVAL, this.sampleInterval.toString());
      
      // 保存字段选项状态
      const fieldOptionsState = this.fieldOptions.map(option => ({
        value: option.value,
        selected: option.selected
      }));
      localStorage.setItem(this.STORAGE_KEYS.FIELD_OPTIONS, JSON.stringify(fieldOptionsState));

      console.log('状态已保存');
    } catch (error) {
      console.warn('保存状态时出错:', error);
    }
  }

  private initializeSelectedFields(): void {
    this.selectedFields = this.fieldOptions
      .filter(field => field.selected)
      .map(field => field.value);
    
    console.log('Selected fields initialized:', this.selectedFields);
  }

  private loadSystemInfo(): void {
    // Load system info to get board version
    this.systemService.getInfo().subscribe({
      next: (data: any) => {
        if (data && data.boardVersion) {
          this.boardVersion = data.boardVersion;
          console.log('Board version loaded:', this.boardVersion);
        }
      },
      error: (error: any) => {
        console.warn('Failed to load system info:', error);
      }
    });
  }

  private initChart(): void {
    console.log('initChart called');
    console.log('chartCanvas ViewChild:', !!this.chartCanvas);
    
    if (!this.chartCanvas) {
      console.error('chartCanvas ViewChild is not available');
      return;
    }
    
    const canvas = this.chartCanvas.nativeElement;
    console.log('Canvas element:', canvas);
    
    if (!canvas) {
      console.error('Canvas DOM element not found');
      return;
    }
    
    const ctx = canvas.getContext('2d');
    if (!ctx) {
      console.error('Canvas context not available');
      return;
    }

    console.log('Initializing chart...');
    
    // 获取容器尺寸来设置Canvas
    const container = canvas.parentElement;
    const containerWidth = container?.clientWidth || 800;
    const containerHeight = 400;
    
    console.log('Container dimensions:', {
      width: containerWidth,
      height: containerHeight,
      container: container
    });
    
    // 强制设置Canvas尺寸 - 使用多种方法
    canvas.width = containerWidth;
    canvas.height = containerHeight;
    canvas.style.width = containerWidth + 'px';
    canvas.style.height = containerHeight + 'px';
    canvas.setAttribute('width', containerWidth.toString());
    canvas.setAttribute('height', containerHeight.toString());
    
    console.log('Canvas size set to:', {
      width: canvas.width,
      height: canvas.height,
      styleWidth: canvas.style.width,
      styleHeight: canvas.style.height,
      attributes: {
        width: canvas.getAttribute('width'),
        height: canvas.getAttribute('height')
      }
    });

    const config: ChartConfiguration = {
      type: 'line',
      data: {
        labels: [],
        datasets: []
      },
      options: {
        responsive: false, // 禁用响应式模式
        maintainAspectRatio: false,
        resizeDelay: 0,
        devicePixelRatio: 1, // 固定像素比例
        scales: {
          x: {
            type: 'time', // 使用时间类型而不是linear
            time: {
              unit: 'minute',
              displayFormats: {
                minute: 'HH:mm',
                hour: 'HH:mm'
              },
              tooltipFormat: 'YYYY-MM-DD HH:mm:ss'
            },
            title: {
              display: true,
              text: 'Time',
              color: '#ffffff'
            },
            ticks: {
              color: '#cccccc',
              maxTicksLimit: 10
            },
            grid: {
              color: 'rgba(255, 255, 255, 0.1)'
            }
          },
          y: {
            title: {
              display: true,
              text: 'Value',
              color: '#ffffff'
            },
            ticks: {
              color: '#cccccc'
            },
            grid: {
              color: 'rgba(255, 255, 255, 0.1)'
            }
          }
        },
        plugins: {
          legend: {
            display: true,
            position: 'top',
            labels: {
              color: '#ffffff',
              usePointStyle: true,
              padding: 20
            }
          },
          tooltip: {
            mode: 'index',
            intersect: false,
            backgroundColor: 'rgba(0, 0, 0, 0.8)',
            titleColor: '#ffffff',
            bodyColor: '#ffffff',
            borderColor: '#4CAF50',
            borderWidth: 1
          }
        },
        interaction: {
          mode: 'nearest',
          axis: 'x',
          intersect: false
        },
        animation: {
          duration: 0 // 禁用动画以提高性能
        }
      }
    };

    this.chart = new Chart(ctx, config);
    console.log('Chart initialized successfully:', !!this.chart);
    
    // 验证Canvas尺寸是否正确设置
    console.log('Post-init Canvas dimensions:', {
      width: ctx.canvas.width,
      height: ctx.canvas.height,
      clientWidth: ctx.canvas.clientWidth,
      clientHeight: ctx.canvas.clientHeight,
      style: {
        width: ctx.canvas.style.width,
        height: ctx.canvas.style.height
      }
    });
    
    // 如果Canvas尺寸仍然为0，再次强制设置
    if (ctx.canvas.width === 0 || ctx.canvas.height === 0) {
      console.warn('Canvas dimensions still 0, forcing resize...');
      ctx.canvas.width = containerWidth;
      ctx.canvas.height = containerHeight;
      this.chart.resize(containerWidth, containerHeight);
    }
    
    console.log('Final Canvas dimensions:', {
      width: ctx.canvas.width,
      height: ctx.canvas.height
    });
    
    console.log('Chart initialization complete');
  }

  loadHistoryData(): void {
    console.log('📊 Starting loadHistoryData method');
    
    // 检查浏览器状态
    this.checkBrowserMemory();
    if (!this.checkNetworkStatus()) {
      this.hasLoadingError = true;
      this.loadingMessage = 'No network connection detected. Please check your internet connection.';
      this.isLoading = false;
      return;
    }
    
    this.isLoading = true;
    this.hasLoadingError = false;
    this.retryCount = 0;
    this.stopRealTimeUpdates();
    
    // 根据采样间隔设置加载提示信息
    if (this.sampleInterval === 1) {
      this.loadingMessage = 'Loading high-detail data... This may take up to 4 minutes';
    } else if (this.sampleInterval <= 5) {
      this.loadingMessage = 'Loading detailed data... This may take up to 2 minutes';
    } else {
      this.loadingMessage = 'Loading data...';
    }
    
    console.log('Loading 24h history data...');
    console.log('API URL will be: /api/system/status/history');
    console.log('SystemService available:', !!this.systemService);
    console.log(`Sample interval: ${this.sampleInterval}, Expected timeout: ${this.getExpectedTimeout()}s`);
    
    // 强制垃圾回收（如果浏览器支持）
    if ('gc' in window && typeof (window as any).gc === 'function') {
      console.log('🗑️ Triggering garbage collection before large data load');
      (window as any).gc();
    }
    
    this.subscription.add(
      this.systemService.getStatusHistory(this.sampleInterval)
        .pipe(
          retryWhen(errors =>
            errors.pipe(
              mergeMap((error, index) => {
                this.retryCount = index + 1;
                console.warn(`❌ History load attempt ${this.retryCount} failed:`, error);
                
                // 检查错误类型
                const isTimeout = error.message?.includes('timeout') || error.message?.includes('timed out');
                const isNetworkError = error.message?.includes('Network') || error.message?.includes('connection');
                const isParsingError = error.message?.includes('JSON parsing') || error.message?.includes('parsing failed');
                
                if (this.retryCount >= this.maxRetries) {
                  console.error(`🚫 Max retries (${this.maxRetries}) reached, giving up`);
                  return throwError(error);
                }
                
                // 对于解析错误，尝试更短的延迟
                let retryDelay = 2000 + (index * 1000);
                if (isTimeout) {
                  retryDelay = 8000 + (index * 5000); // 超时错误使用更长延迟
                } else if (isParsingError) {
                  retryDelay = 3000 + (index * 2000); // 解析错误使用中等延迟
                }
                
                if (isTimeout) {
                  this.loadingMessage = `Request timed out, retrying in ${retryDelay/1000}s... (${this.retryCount}/${this.maxRetries})`;
                } else if (isNetworkError) {
                  this.loadingMessage = `Network error, retrying in ${retryDelay/1000}s... (${this.retryCount}/${this.maxRetries})`;
                } else if (isParsingError) {
                  this.loadingMessage = `Data parsing error, retrying in ${retryDelay/1000}s... (${this.retryCount}/${this.maxRetries})`;
                } else {
                  this.loadingMessage = `Error occurred, retrying in ${retryDelay/1000}s... (${this.retryCount}/${this.maxRetries})`;
                }
                
                console.log(`🔄 Retrying in ${retryDelay}ms...`);
                
                // 每次重试前检查内存
                this.checkBrowserMemory();
                return timer(retryDelay);
              })
            )
          ),
          catchError(error => {
            console.error('❌ Final error after all retries:', error);
            this.hasLoadingError = true;
            
            // 根据错误类型设置不同的错误信息
            if (error.message?.includes('timeout') || error.message?.includes('timed out')) {
              this.loadingMessage = `Request timed out after ${this.getExpectedTimeout()}s. The high detail mode requires processing large amounts of data. Try using a lower detail level or check your connection.`;
            } else if (error.message?.includes('JSON parsing') || error.message?.includes('parsing failed')) {
              this.loadingMessage = `Data format error: ${error.message}. The server may be experiencing issues. Try refreshing the page or using a lower detail level.`;
            } else if (error.message?.includes('Network') || error.message?.includes('connection')) {
              this.loadingMessage = 'Network connection failed. Please check your connection and try again.';
            } else {
              this.loadingMessage = `Error loading data: ${error.message || 'Unknown error'}. Try refreshing the page or using a lower detail level.`;
            }
            
            // 即使加载失败，也要重新启动实时更新
            console.log('🔄 Restarting real-time updates after history load failure');
            this.startRealTimeUpdates();
            
            return throwError(error);
          })
        )
        .subscribe({
          next: (response: any) => {
            console.log('✅ History API called successfully after', this.retryCount, 'retries');
            console.log('History data loaded:', response);
            
            this.loadingMessage = 'Processing data...';
            
            if (response && response.statistics && Array.isArray(response.statistics)) {
              // 将数组数据转换为对象格式
              this.historyData = response.statistics.map((item: any[], index: number) => {
                const processed = {
                  hashrate: item[0] || '0',           // hashRate (GH/s) - 索引0
                  asic_temp: item[1] || '0',          // asicTemp (°C) - 索引1  
                  vcore_temp: item[2] || '0',         // vcoreTemp (°C) - 索引2
                  pbus: item[3] || '0',               // Pbus (W) - 索引3
                  vbus: item[4] || '0',               // Vbus (V) - 索引4
                  ibus: item[5] || '0',               // Ibus (A) - 索引5
                  vcore: item[6] || 0,                // Vcore (mV) - 索引6
                  fanspeed: item[7] || 0,             // fanspeed (%) - 索引7
                  fanrpm: item[8] || 0,               // fanrpm (RPM) - 索引8
                  wifi_rssi: item[9] || 0,            // wifiRSSI (dBm) - 索引9
                  free_heap: item[10] || 0,           // freeHeap (KB) - 索引10
                  free_psram: item[11] || 0,          // freePsram (KB) - 索引11
                  epoch: item[12] || Date.now()       // epoch (ms) - 索引12
                };
                
                if (index < 3 || index === response.statistics.length - 1) {
                  console.log(`Data point ${index}:`, {
                    raw: item,
                    processed: processed,
                    timestamp: new Date(processed.epoch).toLocaleString(),
                    currentTime: new Date().toLocaleString(),
                    timeDiff: (Date.now() - processed.epoch) / (1000 * 60) // 分钟差
                  });
                }
                
                return processed;
              });
              
              // 按时间戳排序，确保数据顺序正确
              this.historyData.sort((a, b) => a.epoch - b.epoch);
              
              console.log('History data time range:', {
                total: this.historyData.length,
                first: this.historyData[0] ? new Date(this.historyData[0].epoch).toLocaleString() : 'none',
                last: this.historyData[this.historyData.length - 1] ? new Date(this.historyData[this.historyData.length - 1].epoch).toLocaleString() : 'none',
                timeSpan: this.historyData.length > 1 ? (this.historyData[this.historyData.length - 1].epoch - this.historyData[0].epoch) / (1000 * 60 * 60) : 0 // 小时
              });
              
              this.dataSize = response.size || this.historyData.length;
              this.lastUpdateTime = new Date().toLocaleString();
              
              // 确保图表立即更新显示数据
              console.log('Triggering initial chart update...');
              this.updateChart();
              
              // 开始实时更新
              this.startRealTimeUpdates();
            }
            
            this.isLoading = false;
            this.hasLoadingError = false;
            this.loadingMessage = 'Loading...';
          },
          error: (error: any) => {
            console.error('❌ Final subscription error:', error);
            this.isLoading = false;
            // Error handling is already done in catchError above
          }
        })
    );
  }

  private getExpectedTimeout(): number {
    if (this.sampleInterval <= 1) return 240; // 4 minutes
    if (this.sampleInterval <= 5) return 120; // 2 minutes
    return 45; // 45 seconds
  }

  // 添加内存检查方法
  private checkBrowserMemory(): void {
    if ('memory' in performance) {
      const memory = (performance as any).memory;
      console.log('Browser memory usage:', {
        used: Math.round(memory.usedJSHeapSize / 1024 / 1024) + 'MB',
        total: Math.round(memory.totalJSHeapSize / 1024 / 1024) + 'MB',
        limit: Math.round(memory.jsHeapSizeLimit / 1024 / 1024) + 'MB'
      });
    }
  }

  // 检查网络状态
  private checkNetworkStatus(): boolean {
    if ('navigator' in window && 'onLine' in navigator) {
      const online = navigator.onLine;
      if (!online) {
        console.warn('🌐 Browser reports offline status');
      }
      return online;
    }
    return true; // 假设在线如果无法检测
  }

  // Auto-start real-time updates after loading history with adaptive interval
  private startRealTimeUpdates(): void {
    // Get adaptive interval based on current sample rate
    this.realtimeInterval = this.realtimeIntervalMap.get(this.sampleInterval) || 15;
    this.realtimeCountdown = this.realtimeInterval; // Initialize countdown
    
    console.log(`🔄 Starting adaptive real-time updates: ${this.realtimeInterval}s interval for sample rate 1/${this.sampleInterval}`);
    
    // Stop any existing subscriptions
    if (this.realTimeSubscription) {
      this.realTimeSubscription.unsubscribe();
    }
    if (this.countdownSubscription) {
      this.countdownSubscription.unsubscribe();
    }
    
    // Start countdown timer (updates every second)
    this.startCountdownTimer();
    
    // Start main update subscription with adaptive interval
    this.realTimeSubscription = interval(this.realtimeInterval * 1000)
      .pipe(takeWhile(() => this.isComponentActive))
      .subscribe(() => {
        this.getRealTimeData();
        this.realtimeCountdown = this.realtimeInterval; // Reset countdown after update
      });
  }

  private startCountdownTimer(): void {
    this.countdownSubscription = interval(1000) // Update every second
      .pipe(takeWhile(() => this.isComponentActive))
      .subscribe(() => {
        if (this.realtimeCountdown > 0) {
          this.realtimeCountdown--;
        } else {
          this.realtimeCountdown = this.realtimeInterval; // Reset if it goes below 0
        }
      });
  }

  private stopRealTimeUpdates(): void {
    if (this.realTimeSubscription) {
      this.realTimeSubscription.unsubscribe();
      this.realTimeSubscription = new Subscription();
    }
    if (this.countdownSubscription) {
      this.countdownSubscription.unsubscribe();
      this.countdownSubscription = new Subscription();
    }
    console.log('Real-time updates stopped');
  }

  private getRealTimeData(): void {
    console.log('Getting real-time data...');
    console.log('API URL will be: /api/system/status/realtime');
    
    this.systemService.getStatusRealtime().subscribe({
      next: (response: any) => {
        console.log('✅ Realtime API called successfully');
        console.log('Real-time response:', response);
        if (response && response.statistics && Array.isArray(response.statistics) && response.statistics.length > 0) {
          const latestData = response.statistics[0];
          const newNode: HistoryNode = {
            hashrate: latestData[0] || '0',      // hashRate (GH/s) - 索引0
            asic_temp: latestData[1] || '0',     // asicTemp (°C) - 索引1
            vcore_temp: latestData[2] || '0',    // vcoreTemp (°C) - 索引2
            pbus: latestData[3] || '0',          // Pbus (W) - 索引3
            vbus: latestData[4] || '0',          // Vbus (V) - 索引4
            ibus: latestData[5] || '0',          // Ibus (A) - 索引5
            vcore: latestData[6] || 0,           // Vcore (mV) - 索引6
            fanspeed: latestData[7] || 0,        // fanspeed (%) - 索引7
            fanrpm: latestData[8] || 0,          // fanrpm (RPM) - 索引8
            wifi_rssi: latestData[9] || 0,       // wifiRSSI (dBm) - 索引9
            free_heap: latestData[10] || 0,      // freeHeap (KB) - 索引10
            free_psram: latestData[11] || 0,     // freePsram (KB) - 索引11
            epoch: latestData[12] || Date.now()  // epoch (ms) - 索引12
          };
          
          // 添加新数据到历史数据数组
          this.historyData.push(newNode);
          
          // 保持最新的2000条数据，避免内存过载
          if (this.historyData.length > 2000) {
            this.historyData = this.historyData.slice(-2000);
          }
          
          this.lastUpdateTime = new Date().toLocaleString();
          this.updateChart();
          
          console.log('Real-time data updated');
        }
      },
      error: (error: any) => {
        console.error('❌ Failed to get real-time data:', error);
        
        // 检查错误类型并提供更好的日志信息
        if (error.name === 'TimeoutError' || error.message?.includes('timeout')) {
          console.warn('⏰ Real-time data request timed out - this is usually temporary');
        } else if (error.status === 0) {
          console.warn('🌐 Network connection issue for real-time data');
        } else {
          console.error('Error details:', {
            status: error.status,
            statusText: error.statusText,
            url: error.url,
            message: error.message
          });
        }
        
        // 实时数据获取失败不影响整体功能，继续运行
      }
    });
  }

  private updateChart(): void {
    if (!this.chart || !this.historyData.length) {
      console.log('Chart update skipped - chart:', !!this.chart, 'data length:', this.historyData.length);
      return;
    }

    console.log('Updating chart with data:', this.historyData.length, 'points');
    console.log('Selected fields:', this.selectedFields);
    console.log('Selected time range:', this.selectedTimeRange);

    // 根据时间范围过滤数据
    let filteredData: HistoryNode[];
    const timeRange = this.timeRanges.find(range => range.value === this.selectedTimeRange);
    
    if (this.selectedTimeRange === 'all' || (timeRange && timeRange.minutes === -1)) {
      // 显示所有历史数据
      filteredData = [...this.historyData];
      console.log('Showing all historical data');
    } else {
      // 根据选择的时间范围过滤
      const cutoffTime = Date.now() - (timeRange?.minutes || 60) * 60 * 1000;
      filteredData = this.historyData.filter(item => item.epoch >= cutoffTime);
      console.log(`Showing last ${timeRange?.minutes} minutes of data`);
    }
    
    console.log('Filtered data:', {
      total: this.historyData.length,
      filtered: filteredData.length,
      timeSpan: filteredData.length > 1 ? (filteredData[filteredData.length - 1].epoch - filteredData[0].epoch) / (1000 * 60) : 0 // 分钟
    });

    if (filteredData.length === 0) {
      console.warn('No data available for chart');
      return;
    }

    // 更新图表数据
    const datasets = this.selectedFields.map(field => {
      const fieldOption = this.fieldOptions.find(f => f.value === field);
      const data = filteredData.map(item => ({
        x: item.epoch,
        y: this.parseValue(item[field as keyof HistoryNode], field)
      }));
      
      if (data.length > 0) {
        console.log(`Dataset for ${field}: ${data.length} points, range: ${new Date(data[0].x).toLocaleTimeString()} - ${new Date(data[data.length - 1].x).toLocaleTimeString()}`);
      }
      
      return {
        label: `${fieldOption?.label} (${fieldOption?.unit})`,
        data: data,
        borderColor: fieldOption?.color || '#4CAF50',
        backgroundColor: `${fieldOption?.color || '#4CAF50'}20`,
        tension: 0.1,
        pointRadius: 2,
        pointHoverRadius: 6,
        borderWidth: 2,
        fill: false
      };
    });

    this.chart.data.datasets = datasets;
    
    // 打印时间轴信息用于调试
    const minTime = Math.min(...filteredData.map(item => item.epoch));
    const maxTime = Math.max(...filteredData.map(item => item.epoch));
    
    console.log('Chart time range:', {
      dataPoints: filteredData.length,
      timeSpan: (maxTime - minTime) / (1000 * 60), // 分钟
      firstData: new Date(minTime).toLocaleString(),
      lastData: new Date(maxTime).toLocaleString(),
      current: new Date().toLocaleString(),
      timeDiff: (Date.now() - maxTime) / (1000 * 60) // 距离最新数据的分钟数
    });
    
    console.log('Chart datasets updated:', this.chart.data.datasets.length);
    console.log('Chart data sample:', this.chart.data.datasets[0]?.data?.slice(0, 2));
    
    // 详细检查图表状态
    console.log('Chart canvas size:', {
      width: this.chart.canvas.width,
      height: this.chart.canvas.height,
      style: {
        width: this.chart.canvas.style.width,
        height: this.chart.canvas.style.height
      }
    });
    
    // 检查图表是否被CSS隐藏
    const canvas = this.chartCanvas?.nativeElement;
    if (canvas) {
      const computedStyle = window.getComputedStyle(canvas);
      const rect = canvas.getBoundingClientRect();
      console.log('Canvas computed style and dimensions:', {
        display: computedStyle.display,
        visibility: computedStyle.visibility,
        opacity: computedStyle.opacity,
        zIndex: computedStyle.zIndex,
        position: computedStyle.position,
        width: computedStyle.width,
        height: computedStyle.height,
        rect: {
          width: rect.width,
          height: rect.height,
          x: rect.x,
          y: rect.y
        },
        offsetDimensions: {
          width: canvas.offsetWidth,
          height: canvas.offsetHeight
        },
        clientDimensions: {
          width: canvas.clientWidth,
          height: canvas.clientHeight
        }
      });
      
      // 如果Canvas仍然是0尺寸，尝试强制修复
      if (this.chart.canvas.width === 0 || this.chart.canvas.height === 0) {
        console.error('❌ Canvas dimensions are still 0! Attempting emergency fix...');
        
        // 获取父容器尺寸
        const container = canvas.parentElement;
        if (container) {
          const containerRect = container.getBoundingClientRect();
          console.log('Container dimensions:', {
            width: containerRect.width,
            height: containerRect.height,
            clientWidth: container.clientWidth,
            clientHeight: container.clientHeight,
            offsetWidth: container.offsetWidth,
            offsetHeight: container.offsetHeight
          });
          
          // 使用容器尺寸来设置Canvas
          const newWidth = Math.max(containerRect.width - 40, 600); // 减去padding
          const newHeight = 400;
          
          canvas.width = newWidth;
          canvas.height = newHeight;
          canvas.style.width = newWidth + 'px';
          canvas.style.height = newHeight + 'px';
          
          console.log('Emergency Canvas resize applied:', {
            width: newWidth,
            height: newHeight
          });
          
          // 重新创建Chart
          this.chart.destroy();
          setTimeout(() => {
            this.initChart();
          }, 100);
          return;
        }
      }
    } else {
      console.error('❌ Canvas element not found in ViewChild!');
    }
    
    this.chart.update('none'); // 无动画更新
    
    // 强制重绘
    setTimeout(() => {
      if (this.chart) {
        this.chart.resize();
        console.log('Chart resized and redrawn');
      }
    }, 100);
  }

  private parseValue(value: any, field: string): number {
    if (typeof value === 'number') {
      // Backend data is already in correct units (KB for memory fields)
      return value;
    }
    
    if (typeof value === 'string') {
      const parsed = parseFloat(value);
      return isNaN(parsed) ? 0 : parsed;
    }
    
    return 0;
  }

  onFieldSelectionChange(): void {
    this.selectedFields = this.fieldOptions
      .filter(field => field.selected)
      .map(field => field.value);
    
    console.log('Field selection changed:', this.selectedFields);
    this.saveState(); // 保存状态
    this.updateChart();
  }

  onTimeRangeChange(): void {
    console.log('Time range changed to:', this.selectedTimeRange);
    this.saveState(); // 保存状态
    this.updateChart(); // 重新更新图表显示
  }

  // Get selected time range label
  getSelectedTimeRangeLabel(): string {
    const timeRange = this.timeRanges.find(range => range.value === this.selectedTimeRange);
    return timeRange ? timeRange.label : 'Unknown';
  }

  // Get status text
  getStatusText(): string {
    if (this.isLoading) {
      return this.loadingMessage;
    }
    if (this.hasLoadingError) {
      return 'Error - ' + this.loadingMessage;
    }
    return 'Live Monitor'; // Always live since we removed pause functionality
  }

  exportData(): void {
    if (!this.historyData.length) return;
    
    // Filter data based on selected time range
    let filteredData: HistoryNode[];
    const timeRange = this.timeRanges.find(range => range.value === this.selectedTimeRange);
    
    if (this.selectedTimeRange === 'all' || (timeRange && timeRange.minutes === -1)) {
      filteredData = [...this.historyData];
    } else {
      const cutoffTime = Date.now() - (timeRange?.minutes || 60) * 60 * 1000;
      filteredData = this.historyData.filter(item => item.epoch >= cutoffTime);
    }
    
    const headers = ['Timestamp', 'Time', 
                    ...this.selectedFields.map(field => this.getFieldLabel(field))];
    
    const csvContent = [
      headers.join(','),
      ...filteredData.map(item => [
        item.epoch,
        `"${new Date(item.epoch).toLocaleString()}"`,
        ...this.selectedFields.map(field => this.parseValue(item[field as keyof HistoryNode], field))
      ].join(','))
    ].join('\n');
    
    const blob = new Blob([csvContent], { type: 'text/csv' });
    const url = window.URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    
    // Generate filename: NMAxeGamma_monitor_20250801_112155.csv
    const now = new Date();
    const dateTime = now.getFullYear().toString() +
                    (now.getMonth() + 1).toString().padStart(2, '0') +
                    now.getDate().toString().padStart(2, '0') + '_' +
                    now.getHours().toString().padStart(2, '0') +
                    now.getMinutes().toString().padStart(2, '0') +
                    now.getSeconds().toString().padStart(2, '0');
    
    link.download = `${this.boardVersion}_monitor_${dateTime}.csv`;
    link.click();
    window.URL.revokeObjectURL(url);
  }

  getFieldLabel(fieldValue: string): string {
    const field = this.fieldOptions.find(f => f.value === fieldValue);
    return field ? `${field.label} (${field.unit})` : fieldValue;
  }

  getStatusBadgeClass(): string {
    if (this.isLoading) return 'status-loading';
    if (this.hasLoadingError) return 'status-error';
    return 'status-realtime'; // Always real-time now
  }

  onSampleIntervalChange(): void {
    // Ensure sampleInterval is a number (ngModel might bind as string)
    this.sampleInterval = Number(this.sampleInterval);
    
    console.log('Sample interval changed to:', this.sampleInterval, 'type:', typeof this.sampleInterval);
    this.saveState(); // Save state
    
    // Update realtime interval and countdown based on new sample interval
    this.realtimeInterval = this.realtimeIntervalMap.get(this.sampleInterval) || 15;
    this.realtimeCountdown = this.realtimeInterval; // Reset countdown to new interval
    console.log(`📊 Sample interval changed to 1/${this.sampleInterval}, realtime updates now every ${this.realtimeInterval}s`);
    
    // Provide user feedback for high detail modes
    if (this.sampleInterval === 1) {
      console.log('⚠️ High detail mode selected - this may take up to 2 minutes to load with large datasets');
      console.log('💡 If you experience timeouts, try reducing the time range or using Normal detail mode');
    } else if (this.sampleInterval <= 5) {
      console.log('⚠️ Medium detail mode selected - this may take up to 1 minute to load');
    }
    
    // Reset error state
    this.hasLoadingError = false;
    this.retryCount = 0;
    
    this.loadHistoryData();
    
    // Restart realtime updates with new adaptive interval and countdown
    if (this.isComponentActive) {
      this.startRealTimeUpdates();
    }
  }

}
