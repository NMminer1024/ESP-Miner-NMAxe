import { Component, OnInit, OnDestroy, ViewChild, ElementRef, AfterViewInit } from '@angular/core';
import { SystemService } from '../../services/system.service';
import { Subscription, interval } from 'rxjs';
import { takeWhile } from 'rxjs/operators';
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
  sampleInterval = 10; // 添加采样间隔属性
  
  chart: Chart | null = null;
  isLoading = false;
  lastUpdateTime = '';
  dataSize = 0;
  boardVersion = 'ESP-Miner'; // Default board version
  
  private subscription: Subscription = new Subscription();
  private realTimeSubscription: Subscription = new Subscription();
  private isComponentActive = true;
  
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

  constructor(private systemService: SystemService) { }

  ngOnInit(): void {
    console.log('🚀 Monitor component ngOnInit called');
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
    this.stopRealTimeUpdates();
    if (this.chart) {
      this.chart.destroy();
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
    this.isLoading = true;
    this.stopRealTimeUpdates();
    
    console.log('Loading 24h history data...');
    console.log('API URL will be: /api/system/status/history');
    console.log('SystemService available:', !!this.systemService);
    
    this.subscription.add(
      this.systemService.getStatusHistory(this.sampleInterval).subscribe({
        next: (response: any) => {
          console.log('✅ History API called successfully');
          console.log('History data loaded:', response);
          
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
        },
        error: (error: any) => {
          console.error('❌ Failed to load history data:', error);
          console.error('Error details:', {
            status: error.status,
            statusText: error.statusText,
            url: error.url,
            message: error.message
          });
          this.isLoading = false;
        }
      })
    );
  }

  // Auto-start real-time updates after loading history
  private startRealTimeUpdates(): void {
    console.log('Starting continuous real-time updates...');
    
    // Get real-time data every 5 seconds
    this.realTimeSubscription = interval(5000)
      .pipe(takeWhile(() => this.isComponentActive))
      .subscribe(() => {
        this.getRealTimeData();
      });
  }

  private stopRealTimeUpdates(): void {
    this.realTimeSubscription.unsubscribe();
    this.realTimeSubscription = new Subscription();
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
        console.error('Error details:', {
          status: error.status,
          statusText: error.statusText,
          url: error.url,
          message: error.message
        });
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
    this.updateChart();
  }

  onTimeRangeChange(): void {
    console.log('Time range changed to:', this.selectedTimeRange);
    this.updateChart(); // 重新更新图表显示
  }

  // Get selected time range label
  getSelectedTimeRangeLabel(): string {
    const timeRange = this.timeRanges.find(range => range.value === this.selectedTimeRange);
    return timeRange ? timeRange.label : 'Unknown';
  }

  // Get status text
  getStatusText(): string {
    if (this.isLoading) return 'Loading...';
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
    return 'status-realtime'; // Always real-time now
  }

  onSampleIntervalChange(): void {
    console.log('Sample interval changed to:', this.sampleInterval);
    this.loadHistoryData();
  }

}
