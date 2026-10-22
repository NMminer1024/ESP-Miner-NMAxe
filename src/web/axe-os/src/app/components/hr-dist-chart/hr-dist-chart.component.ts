import { Component, OnInit, ViewChild, AfterViewInit, ElementRef, OnDestroy } from '@angular/core';
import { Observable, interval, switchMap, tap, Subscription } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { Chart, ChartConfiguration, ChartOptions, registerables } from 'chart.js';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';

// 注册Chart.js组件
Chart.register(...registerables);

interface TopDiffsResponse {
  top: string[]; // lifetime Top-10 share difficulties, descending, e.g. "90.12T"
  ts?: number[]; // UTC epoch (seconds) per entry, index-aligned; 0 = unknown
  pool?: string[]; // pool "host:port" in use when achieved, index-aligned; "" = unknown
  user?: string[]; // stratum user (wallet[.worker]) in use, index-aligned; "" = unknown
  net?: string[]; // pool-reported network difficulty at achievement, e.g. "90.12T"; "0" = unknown
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

  public topData$!: Observable<TopDiffsResponse>; // 延迟初始化
  public bestDiff: string = '';                    // Top1 difficulty, banner
  private topValues: number[] = [];                // numeric difficulties, descending

  constructor(private systemService: SystemService) {
    // Constructor中不创建数据流，将在ngOnInit中创建
  }

  ngOnInit(): void {
    // 创建数据流但不立即订阅（Top10 榜单变化缓慢，60s 轮询即可）
    this.topData$ = interval(60000).pipe(
      switchMap(() => this.systemService.getTopDiffs()),
      tap((data: TopDiffsResponse) => {
        this.updateChart(data);
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
    if (this.topDiffTooltipEl) {
      this.topDiffTooltipEl.remove();
      this.topDiffTooltipEl = null;
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

    // 检测是否为移动端，针对性优化布局
    const isMobile = window.innerWidth <= 768;
    const layoutPadding = isMobile ? 
      { top: 14, right: 4, bottom: 30, left: 4 } : // 移动端：顶部留白容纳Top3难度标注，底部防截断
      { top: 18, right: 8, bottom: 15, left: 8 };  // PC端：顶部留白容纳Top3难度标注

    const chartConfig: ChartConfiguration = {
      type: 'bar',
      data: {
        labels: [],
        datasets: [{
          label: 'Best Difficulty',
          data: [],
          backgroundColor: 'transparent', // 设为透明，由自定义插件绘制
          borderColor: 'transparent', // 设为透明，由自定义插件绘制
          borderWidth: 0
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
            // Custom HTML tooltip: label/value columns aligned, per-field
            // colors, long wallet addresses wrap instead of blowing up width.
            enabled: false,
            mode: 'index',
            intersect: false,
            external: (context: any) => this.renderTopDiffTooltip(context)
          }
        },
        layout: {
          padding: layoutPadding // 使用响应式边距配置
        },
        scales: {
          x: {
            title: {
              display: true,
              text: 'Rank',
              color: textColorSecondary,
              font: {
                size: 11
              }
            },
            ticks: {
              color: textColorSecondary,
              font: {
                size: isMobile ? 8 : 10 // 移动端使用更小字体节省空间
              },
              maxRotation: isMobile ? 30 : 45, // 移动端减少旋转角度
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
            ticks: {
              color: textColorSecondary,
              font: {
                size: 10
              },
              callback: (value: any) => DiffSuffixPipe.transform(value)
            },
            grid: {
              color: surfaceBorder
            }
          }
        }
      },
      plugins: [this.createModernBarPlugin()] // 添加现代化柱子插件
    };

    this.chart = new Chart(ctx, chartConfig);
    this.setupCanvasStyles();
  }

  // Clear the lifetime Top-10 history on the device (NVS included), then
  // refresh the chart immediately so the empty state is visible right away.
  // Confirmation uses the in-page p-dialog instead of a native confirm().
  public clearTopDiffsDialogVisible: boolean = false;

  public showClearTopDiffsDialog(): void {
    this.clearTopDiffsDialogVisible = true;
  }

  public clearTopDiffs(): void {
    this.clearTopDiffsDialogVisible = false;
    this.systemService.clearTopDiffs().subscribe({
      next: () => this.loadInitialData(),
      error: (error) => {
        console.error('❌ Failed to clear top diffs:', error);
      }
    });
  }

  private loadInitialData(): void {
    // 立即获取一次数据
    this.systemService.getTopDiffs().subscribe({
      next: (data: TopDiffsResponse) => {
        this.updateChart(data);
      },
      error: (error) => {
        console.error('❌ Failed to load initial top diffs data:', error);
      }
    });
  }

  private startPeriodicUpdates(): void {
    // 启动定期更新（从第二次开始）
    this.dataSubscription = this.topData$.subscribe();
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

  // Raw difficulty strings per rank, for tooltips (index 0 = #1 best).
  public rankLabels: string[] = [];

  // Achievement epoch (UTC seconds) per rank; 0 = unknown.
  public rankTimes: number[] = [];

  // Pool ("host:port") and stratum user (wallet) snapshot per rank; '' = unknown.
  public rankPools: string[] = [];
  public rankUsers: string[] = [];

  // Pool-reported network difficulty (formatted, e.g. "90.12T") per rank;
  // '0'/'' = unknown. Share diff >= net diff means the share solved a block.
  public rankNets: string[] = [];

  // Custom HTML tooltip: "#N Best Difficulty" title, then label/value rows
  // with aligned columns and per-field colors. Unknown fields (pre-snapshot
  // firmware) are simply omitted. Styles live in the component SCSS under
  // .topdiff-tooltip.
  private topDiffTooltipEl: HTMLDivElement | null = null;

  private renderTopDiffTooltip(context: any): void {
    const { chart, tooltip } = context;
    if (!this.topDiffTooltipEl) {
      this.topDiffTooltipEl = document.createElement('div');
      this.topDiffTooltipEl.className = 'topdiff-tooltip';
      document.body.appendChild(this.topDiffTooltipEl);
    }
    const el = this.topDiffTooltipEl;

    if (tooltip.opacity === 0) {
      el.style.opacity = '0';
      return;
    }

    const i = tooltip.dataPoints?.[0]?.dataIndex ?? 0;
    const esc = (s: string) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    const row = (label: string, value: string, cls: string) =>
      `<div class="tt-row"><span class="tt-label">${label}</span><span class="tt-value ${cls}">${esc(value)}</span></div>`;

    let html = `<div class="tt-title">#${i + 1} Best Difficulty</div>`;
    html += row('Difficulty', this.rankLabels[i] ?? String(tooltip.dataPoints?.[0]?.parsed?.y ?? ''), 'tt-diff');
    const epoch = this.rankTimes[i] ?? 0;
    if (epoch > 0) html += row('Achieved', this.formatLocal(epoch), 'tt-time');
    const pool = this.rankPools[i] ?? '';
    if (pool) html += row('Pool', pool, 'tt-pool');
    const user = this.rankUsers[i] ?? '';
    // Show only the wallet part: stratum user is "wallet[.worker]" — BTC
    // addresses never contain '.', so everything after the first dot is the
    // worker suffix and gets trimmed for display.
    if (user) html += row('Wallet', user.split('.')[0], 'tt-wallet');
    // Network difficulty the pool reported at achievement time. When the
    // share reached it, the share was a real block solve — flag it.
    const net = this.rankNets[i] ?? '';
    const netNum = net ? DiffSuffixPipe.revert(net) : 0;
    if (netNum > 0) {
      const shareNum = this.topValues[i] ?? 0;
      const solved = shareNum >= netNum;
      html += row('Net Diff', net + (solved ? '  ⚡ block solve' : ''), solved ? 'tt-net-solved' : 'tt-net');
    }
    el.innerHTML = html;

    // Position near the hovered bar, clamped inside the viewport.
    const rect = chart.canvas.getBoundingClientRect();
    el.style.opacity = '1';
    let left = rect.left + window.scrollX + tooltip.caretX + 12;
    let top = rect.top + window.scrollY + tooltip.caretY - el.offsetHeight / 2;
    if (left + el.offsetWidth > window.innerWidth - 8) {
      left = rect.left + window.scrollX + tooltip.caretX - el.offsetWidth - 12;
    }
    left = Math.max(8, left);
    top = Math.max(8, Math.min(top, window.innerHeight + window.scrollY - el.offsetHeight - 8));
    el.style.left = left + 'px';
    el.style.top = top + 'px';
  }

  private formatLocal(epochSec: number): string {
    const d = new Date(epochSec * 1000);
    const p = (n: number) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
  }

  private updateChart(data: TopDiffsResponse): void {
    let top = Array.isArray(data?.top) ? data.top : [];

    // Parse "90.12T"-style strings into plain numbers for the bar heights.
    this.topValues = top.map(v => DiffSuffixPipe.revert(v));

    // A freshly upgraded device may have fewer than 10 entries (zero-filled
    // tail slots); drop them so only real ranks are drawn.
    const filled = this.topValues.findIndex(v => !(v > 0));
    if (filled >= 0) {
      top = top.slice(0, filled);
      this.topValues = this.topValues.slice(0, filled);
    }
    this.rankLabels = [...top];
    // Timestamps / pool / user are index-aligned with the raw arrays; keep
    // only filled ranks. Older firmware omits these fields entirely.
    const rawTs = Array.isArray(data?.ts) ? data.ts : [];
    this.rankTimes = rawTs.slice(0, this.topValues.length).map(v => Number(v) || 0);
    const rawPool = Array.isArray(data?.pool) ? data.pool : [];
    this.rankPools = rawPool.slice(0, this.topValues.length).map(v => String(v ?? ''));
    const rawUser = Array.isArray(data?.user) ? data.user : [];
    this.rankUsers = rawUser.slice(0, this.topValues.length).map(v => String(v ?? ''));
    const rawNet = Array.isArray(data?.net) ? data.net : [];
    this.rankNets = rawNet.slice(0, this.topValues.length).map(v => String(v ?? ''));
    // Expose labels to the custom bar plugin (draws Top3 value captions).
    if (this.chart) (this.chart as any).$rankLabels = this.rankLabels;

    // Banner: Top1 difficulty at the card top, centered.
    this.bestDiff = this.topValues.length > 0 && this.topValues[0] > 0 ? top[0] : '';

    if (!this.chart) return;

    // Left-to-right descending rank labels (#1 … #10).
    const labels: string[] = [];
    const values: number[] = [];
    for (let i = 0; i < this.topValues.length; i++) {
      labels.push(`#${i + 1}`);
      values.push(this.topValues[i]);
    }

    // Update chart data
    this.chart.data.labels = labels;
    this.chart.data.datasets[0].data = values;

    // Update chart
    this.chart.update('none'); // 使用'none'模式避免动画
  }

  /**
   * 创建现代化柱子样式插件
   */
  private createModernBarPlugin() {
    return {
      id: 'modernBarPlugin',
      beforeDatasetDraw: function(chart: any, args: any) {
        const { ctx, chartArea } = chart;
        const meta = chart.getDatasetMeta(0); // 柱子数据集
        
        // 检查数据集是否可见，如果隐藏则不绘制
        if (!meta || !meta.data || meta.hidden) return;
        
        ctx.save();
        
        // 为每个柱子添加圆角和渐变效果
        meta.data.forEach((element: any, index: number) => {
          if (!element || element.skip) return;
          
          const { x, y, base, width } = element;
          const barHeight = Math.abs(base - y);
          
          if (barHeight <= 0) return;
          
          // 名次配色：#1 金 / #2 银 / #3 铜，其余默认蓝
          const rankColors = [
            { fill: 'rgba(255, 193, 7, 0.85)',  edge: 'rgba(255, 193, 7, 1)'  }, // gold
            { fill: 'rgba(192, 196, 202, 0.85)', edge: 'rgba(192, 196, 202, 1)' }, // silver
            { fill: 'rgba(205, 127, 50, 0.85)',  edge: 'rgba(205, 127, 50, 1)'  }, // bronze
          ];
          const color = rankColors[index] ?? { fill: 'rgba(33, 150, 243, 0.8)', edge: 'rgba(33, 150, 243, 1)' };
          
          // 创建渐变效果
          const gradient = ctx.createLinearGradient(0, y, 0, base);
          gradient.addColorStop(0, color.fill);
          gradient.addColorStop(1, color.fill.replace(/0\.85?\)$/, '0.35)')); // 底部更透明
          
          // 绘制圆角矩形
          const cornerRadius = 4; // 圆角半径
          const barX = x - width / 2;
          const barY = Math.min(y, base);
          
          ctx.beginPath();
          ctx.roundRect(barX, barY, width, barHeight, [cornerRadius, cornerRadius, 0, 0]); // 只有顶部圆角
          ctx.fillStyle = gradient;
          ctx.fill();
          
          // 添加细微的边框
          ctx.strokeStyle = color.edge;
          ctx.lineWidth = 1;
          ctx.stroke();

          // Top3 柱顶标注具体难度值（如 "32.84G"）
          if (index < 3) {
            const label = (chart as any).$rankLabels?.[index];
            if (label) {
              ctx.save();
              ctx.font = '600 10px sans-serif';
              ctx.textAlign = 'center';
              ctx.textBaseline = 'bottom';
              ctx.fillStyle = color.edge;
              ctx.fillText(label, x, barY - 2);
              ctx.restore();
            }
          }
        });
        
        ctx.restore();
      }
    };
  }
}
