import { Component, OnInit, OnDestroy, ViewChild, ElementRef } from '@angular/core';
import { AbstractControl, FormBuilder, FormGroup, ValidationErrors, ValidatorFn, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { forkJoin, interval, of, Subscription } from 'rxjs';
import { catchError, switchMap } from 'rxjs/operators';
import { Chart, ChartConfiguration, registerables } from 'chart.js';
import { SystemService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';

Chart.register(...registerables);

export interface BenchmarkResult {
  freq: number;
  vcore: number;
  expHR: number;
  avgHR: number;
  // Temperatures are NULLABLE: a failed telemetry read is stored as JSON null
  // (it used to be averaged into `nan`, which made the whole response
  // unparseable). Anything that formats them must tolerate null.
  avgAsicTemp: number | null;
  avgVcoreTemp: number | null;
  effJTH: number;
  avgPwr: number;
  ts?: number;  // Unix timestamp (seconds)
  // Peak fan duty (%) observed while sampling this point. ABSENT on boards
  // without that fan and on results written before the fields existed.
  // fanAsic  = the ASIC heatsink fan (id 0)
  // fanVcore = the Vcore / VRM fan (id 1) — only present on two-fan models
  fanAsic?: number;
  fanVcore?: number;
  // Firmware build that produced this row. ABSENT on results written before the
  // field existed. Kept per row because results outlive an upgrade, and the
  // safety verdicts are only comparable within one build.
  fw?: string;
  // Set when the point survived the post-sweep soak under the normal fan policy.
  // The sweep measures at 100 % cooling, which is comparable but optimistic; only
  // a soaked point is known to be sustainable in real operation.
  soak?: number;
  // Duty each fan settled at during the soak. fanNeed is the ASIC fan (id 0) and
  // is always present once soaked; fanNeedVcore is the VRM fan (id 1) and is
  // absent on single-fan boards. They are independent regulators with their own
  // target temperature, so one can be saturated while the other still has room.
  fanNeed?: number;
  fanNeedVcore?: number;
  // Firmware-computed verdicts. ABSENT on results measured before the
  // safety-margin rule existed (older firmware, or an entry that survived an
  // upgrade). Those are legacy measurements: the numbers are real, but nothing
  // ever checked them against the peak current / adapter budget / temperature
  // limits, so they must never be selected automatically.
  q?: number;     // 1 = passed the safety margin, 0 = failed it
  fail?: number;  // 1 = this point tripped protection
  // Highest voltage that FAILED at this frequency, and the hashrate it reached
  // (% of expected). ABSENT when the first voltage tried already passed, which
  // means "the floor was never probed" — a different statement from "the step
  // below failed". Written by firmware from v3.2.02a on.
  vFail?: number;
  vFailPct?: number;
}

@Component({
  selector: 'app-benchmark',
  templateUrl: './benchmark.component.html',
  styleUrls: ['./benchmark.component.scss']
})
export class BenchmarkComponent implements OnInit, OnDestroy {

  public form!: FormGroup;
  public isRunning = false;
  public curFreq = 0;
  public curVcore = 0;
  public totalSec = 0;
  public startTs = 0;
  public elapsedSec = 0;
  public results: BenchmarkResult[] = [];
  private pollSub?: Subscription;
  private timerSub?: Subscription;

  // Sorting state
  // Default: frequency ascending, so the table reads as a V/f sweep from the
  // lowest clock up — the same order the sweep measures in and the same axis the
  // chart uses. (Time ascending used to be the default, which put the newest
  // measurement at the bottom of the table.)
  public sortCol: keyof BenchmarkResult = 'freq';
  public sortDir: 1 | -1 = 1;

  // Pre-computed best entries (updated whenever results change)
  public bestEff: BenchmarkResult | null = null;
  public bestHR:  BenchmarkResult | null = null;

  // Confirmation dialog visibility
  public showStartConfirm = false;
  public showResumeConfirm = false;
  public showStopConfirm = false;
  public showResetConfirm = false;
  public showClearConfirm = false;
  public showApplyConfirm = false;
  public pendingApply: BenchmarkResult | null = null;

  // Per-row delete confirmation
  public showDeleteRowConfirm = false;
  public pendingDelete: BenchmarkResult | null = null;

  // Device identity for download filename
  public deviceDisplayName = 'NMAxe';
  private deviceIp = '';

  // ── Chart view state ──────────────────────────────────────────────────────
  public activeView: 'table' | 'chart' = 'table';
  private hrChart: Chart | null = null;
  private effChart: Chart | null = null;
  private hrCanvas?: ElementRef<HTMLCanvasElement>;
  private effCanvas?: ElementRef<HTMLCanvasElement>;
  // Rows backing the chart points (same order as chart data, for tooltips/click-apply)
  private hrRows: BenchmarkResult[] = [];
  private effRows: BenchmarkResult[] = [];
  private effBestIdx = -1;

  // Canvases live inside *ngIf, so use setters: (re)init when they appear,
  // destroy the stale Chart instance when the view removes them.
  @ViewChild('hrChartCanvas') set hrChartCanvas(el: ElementRef<HTMLCanvasElement> | undefined) {
    if (!el) { this.hrChart?.destroy(); this.hrChart = null; this.hrCanvas = undefined; return; }
    this.hrCanvas = el;
    this.maybeInitCharts();
  }
  @ViewChild('effChartCanvas') set effChartCanvas(el: ElementRef<HTMLCanvasElement> | undefined) {
    if (!el) { this.effChart?.destroy(); this.effChart = null; this.effCanvas = undefined; return; }
    this.effCanvas = el;
    this.maybeInitCharts();
  }

  // Baseline sweep range (from initial GET response, used for resume detection)
  private loadedFreqMin  = 0;
  private loadedVcoreMin = 0;

  // Require whole numbers for all benchmark numeric fields.
  private readonly integerValidator: ValidatorFn = (control: AbstractControl): ValidationErrors | null => {
    const v = control.value;
    if (v === null || v === undefined || v === '') return null;
    const n = typeof v === 'number' ? v : Number(v);
    if (!Number.isFinite(n)) return { number: true };
    if (!Number.isInteger(n)) return { integer: true };
    return null;
  };

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {}

  // Device-persisted sweep parameters can sit outside the ranges this UI
  // accepts: they may have been written through the REST API (e.g. a short
  // smoke run with stabTime=10) or by an older firmware whose limits differed.
  // Feeding such a value straight into the FormControl leaves the form
  // permanently invalid, which disables the primary button and shows
  // "Invalid fields: Stabilize Time" even though the device is idle and fine.
  // Clamp on load so the form always starts usable; the clamped value is what
  // gets written back on the next Start.
  private clampField(v: any, min: number, max: number, fallback: number): number {
    const n = Number(v);
    if (!isFinite(n)) return fallback;
    return Math.min(max, Math.max(min, Math.round(n)));
  }

  ngOnInit(): void {
    // Load device identity for download filename
    forkJoin({
      info:    this.systemService.getInfo(),
      network: this.systemService.getSettingNetwork()
    }).subscribe(({ info, network }) => {
      this.deviceDisplayName = (info as any)?.identity?.displayName || 'NMAxe';
      this.deviceIp          = (network as any)?.ip || '';
    });

    this.systemService.getBenchmark()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: (data: any) => {
          this.isRunning = data.mode === 1;
          this.curFreq   = data.curFreq;
          this.curVcore  = data.curVcore;
          this.totalSec  = data.totalSec ?? 0;
          this.startTs   = data.startTs  ?? 0;
          this.loadedFreqMin  = data.freqMin;
          this.loadedVcoreMin = data.vcoreMin;
          this.setResults(Array.isArray(data.results) ? data.results : []);

          if (this.isRunning) this.startElapsedTimer();

          this.createForm(data);

          if (this.isRunning) {
            this.startPolling();
          }
        },
        // Without this branch a failed request (or a response the browser cannot
        // parse) left `form` undefined forever: the template gates on
        // `form != null`, so the page sat on "Waiting..." with no error and no
        // recovery - reported on Nexus with v3.2.02k, where one row carried
        // "avgVcoreTemp":nan and made the whole document invalid JSON. The
        // firmware now emits null instead, but the page must still say what
        // happened rather than spin.
        error: (err: any) => {
          this.toastr.error(
            `Could not load benchmark data: ${err?.message ?? err}. ` +
            'The form below uses board defaults; check the device log.',
            'Benchmark data unavailable');
          this.createForm({});
        }
      });
  }

  private createForm(data: any): void {
    this.form = this.fb.group({
      freqMin:    [this.clampField(data.freqMin,    100, 5000, 400),  [Validators.required, this.integerValidator, Validators.min(100), Validators.max(5000)]],
      freqMax:    [this.clampField(data.freqMax,    100, 5000, 625),  [Validators.required, this.integerValidator, Validators.min(100), Validators.max(5000)]],
      freqStep:   [this.clampField(data.freqStep,   1,   500,  25),   [Validators.required, this.integerValidator, Validators.min(1),   Validators.max(500)]],
      vcoreMin:   [this.clampField(data.vcoreMin,   600, 2000, 1000), [Validators.required, this.integerValidator, Validators.min(600), Validators.max(2000)]],
      vcoreMax:   [this.clampField(data.vcoreMax,   600, 2000, 1300), [Validators.required, this.integerValidator, Validators.min(600), Validators.max(2000)]],
      vcoreStep:  [this.clampField(data.vcoreStep,  1,   200,  25),   [Validators.required, this.integerValidator, Validators.min(1),   Validators.max(200)]],
      sampleIntv: [this.clampField(data.sampleIntv, 1,   300,  2),    [Validators.required, this.integerValidator, Validators.min(1),   Validators.max(300)]],
      bmTime:     [this.clampField(data.bmTime,     15,  7200, 120),  [Validators.required, this.integerValidator, Validators.min(15),  Validators.max(7200)]],
      stabTime:   [this.clampField(data.stabTime,   15,  3600, 30),   [Validators.required, this.integerValidator, Validators.min(15),  Validators.max(3600)]],
    });
    this.syncFormLock();
  }

  ngOnDestroy(): void {
    this.pollSub?.unsubscribe();
    this.timerSub?.unsubscribe();
    this.hrChart?.destroy();
    this.effChart?.destroy();
    this.hrChart = null;
    this.effChart = null;
  }

  // ── Field validation error messages ─────────────────────────────────────
  public fieldError(name: string): string {
    const ctrl = this.form?.get(name);
    if (!ctrl || ctrl.disabled || !ctrl.touched || !ctrl.invalid) return '';
    if (ctrl.hasError('required')) return 'This field is required.';
    if (ctrl.hasError('number')) return 'Please enter a valid number.';
    if (ctrl.hasError('integer')) return 'Must be a whole number (no decimals).';
    if (ctrl.hasError('min')) return `Must be ≥ ${ctrl.getError('min').min}.`;
    if (ctrl.hasError('max')) return `Must be ≤ ${ctrl.getError('max').max}.`;
    return 'Invalid value.';
  }

  // ── Button state helpers ──────────────────────────────────────────────────
  public get primaryDisabledTip(): string {
    if (!this.isRunning && !this.canResume && this.form && !this.form.valid)
      return this.formInvalidReason;
    return '';
  }

  public get clearDisabledTip(): string {
    return this.isRunning ? 'Cannot clear results while a benchmark is running' : '';
  }

  // ── Merged primary button (Start / Resume / Stop) ─────────────────────────
  public get canResume(): boolean {
    // A run is resumable when:
    //  - not currently running
    //  - a run was previously started (startTs > 0, set by post_benchmark_start)
    //  - the run did NOT complete normally (totalSec === 0; completion sets totalSec > 0)
    //  - curFreq is valid (> 0 means NVS has a saved position)
    // This correctly handles stopping at the very first round (curFreq === loadedFreqMin)
    // without falsely triggering after a completed run or after reset.
    return !this.isRunning && this.startTs > 0 && this.totalSec === 0 && this.curFreq > 0;
  }

  public get primaryButtonLabel(): string {
    if (this.isRunning) return 'Stop';
    if (this.canResume) return 'Resume';
    return 'Start Benchmark';
  }

  public get primaryButtonIcon(): string {
    if (this.isRunning) return 'pi pi-stop';
    if (this.canResume) return 'pi pi-step-forward';
    return 'pi pi-play';
  }

  // PrimeNG 17: severity is managed via HostBinding inside pButton directive;
  // [ngClass] additions are overridden by updateClass(). Must use [severity] input.
  public get primaryButtonSeverity(): 'success' | 'warning' | 'danger' {
    if (this.isRunning) return 'danger';
    if (this.canResume) return 'warning';
    return 'success';
  }

  public get primaryButtonDisabled(): boolean {
    return !this.isRunning && !this.canResume && !this.form?.valid;
  }

  public primaryAction(): void {
    if (this.isRunning) { this.openStopConfirm(); return; }
    if (this.canResume) { this.openResumeConfirm(); return; }
    this.openStartConfirm();
  }

  // Start from scratch even when a resumable position exists
  public startFresh(): void {
    this.showResumeConfirm = false;
    this.doStart();
  }

  private get formInvalidReason(): string {
    if (!this.form) return 'Form not ready';
    const ctrl = this.form.controls;
    const fieldNames: Record<string, string> = {
      freqMin: 'Freq Min', freqMax: 'Freq Max', freqStep: 'Freq Step',
      vcoreMin: 'Vcore Min', vcoreMax: 'Vcore Max', vcoreStep: 'Vcore Step',
      sampleIntv: 'Sample Interval', bmTime: 'Benchmark Time', stabTime: 'Stabilize Time'
    };
    const errs: string[] = [];
    for (const [key, label] of Object.entries(fieldNames)) {
      if (ctrl[key]?.invalid) errs.push(label);
    }
    return errs.length ? `Invalid fields: ${errs.join(', ')}` : 'Form has validation errors';
  }

  // ── Result verdicts ───────────────────────────────────────────────────────
  // A row with no "q" was measured before the safety-margin rule existed. It is
  // NOT the same as "unsafe" - the numbers are genuine measurements - but it was
  // never checked against the peak current / adapter budget / temperature limits,
  // so the firmware refuses to auto-select it and so must the presets.
  public isLegacy(r: BenchmarkResult): boolean {
    return r.q === undefined || r.q === null;
  }

  public isFailed(r: BenchmarkResult): boolean {
    return r.fail === 1;
  }

  public isQualified(r: BenchmarkResult): boolean {
    return !this.isFailed(r) && r.q === 1 && r.avgHR > 0;
  }

  // Soak is no longer a verdict shown next to a row: a point that fails it is
  // deleted from the results, so everything in the table has already been judged.
  // The flag is still read, to say whether the fan figures are a soak measurement
  // or a sweep one.
  public isSoaked(r: BenchmarkResult): boolean {
    return r.soak === 1;
  }

  public verdictLabel(r: BenchmarkResult): string {
    if (this.isFailed(r)) return 'failed';
    if (this.isLegacy(r)) return 'legacy';
    return r.q === 1 ? 'verified' : 'over limit';
  }

  public verdictClass(r: BenchmarkResult): string {
    if (this.isFailed(r)) return 'bm-badge-failed';
    if (this.isLegacy(r)) return 'bm-badge-warn';
    return r.q === 1 ? 'bm-badge-ok' : 'bm-badge-over';
  }

  public verdictTip(r: BenchmarkResult): string {
    if (this.isFailed(r)) {
      return 'Protection tripped at this point. The result invalidates any earlier measurement of the same frequency.';
    }
    if (this.isLegacy(r)) {
      return 'Measured by an older firmware, before results were checked against the safety margin. ' +
             'The numbers are real, but ECO/Turbo ignore this row. Re-run the benchmark to qualify it.';
    }
    return r.q === 1
      ? 'Passed the safety margin (peak current, adapter budget, temperature) and held its temperature under your normal fan policy.'
      : 'Stable hashrate, but a peak value sat too close to a limit, so ECO/Turbo will not select this point.';
  }

  // Number of rows that predate the margin check (tombstones excluded - those
  // carry their own verdict).
  public get legacyCount(): number {
    return this.results.filter(r => this.isLegacy(r) && !this.isFailed(r)).length;
  }

  // ── Results management ────────────────────────────────────────────────────
  private setResults(results: BenchmarkResult[]): void {
    this.results = results;
    // The best-of tags must not land on a FAILED tombstone: its avgHR and effJTH
    // are both 0, and "lowest J/TH" therefore awarded it the trophy outright.
    // Unverified legacy rows are also excluded whenever at least one qualified
    // row exists, so the table never advertises an unchecked point as the
    // recommended operating point. With nothing qualified the tag falls back to
    // the best raw entry, which the banner above the table then qualifies.
    const usable = results.filter(r => !this.isFailed(r) && r.avgHR > 0);
    const qualified = usable.filter(r => this.isQualified(r));
    // ── The two tags deliberately use DIFFERENT candidate pools ─────────────
    //
    // Hashrate is recorded while the point runs, and a soak is what makes that
    // figure trustworthy: the sweep samples it at pinned 100 % cooling, which is
    // not how the unit will be operated. ⚡ therefore prefers soaked rows and
    // falls back to all qualified rows when nothing has been soaked yet.
    const soaked = qualified.filter(r => this.isSoaked(r));
    const hrPool = soaked.length ? soaked : (qualified.length ? qualified : usable);

    // Efficiency must NOT share that pool. Narrowing the candidates to the
    // soaked rows and then asking "which is most efficient" can leave a single
    // row, and one row is trivially both fastest and most efficient: measured on
    // a bench board, the only soaked point (705MHz, 14.69 J/TH) was given both
    // tags while 605MHz sat at 13.11 J/TH, and the chart panel still pointed at
    // 605 - two views of one dataset disagreeing. Soak verifies thermals; it
    // says nothing about efficiency.
    //
    // The trophy also needs something to compare against: with fewer than two
    // qualified rows "lowest J/TH" is not a choice at all. That mirrors the rule
    // the mode presets apply before offering ECO (edit.component.ts, ecoOk >= 2).
    // When nothing is qualified the table is showing raw/legacy numbers, so fall
    // back to the pool ⚡ used and let the banner above the table qualify it.
    const effPool = qualified.length ? qualified.filter(r => r.effJTH > 0) : usable.filter(r => r.effJTH > 0);
    this.bestEff = (qualified.length >= 2 && effPool.length) ? effPool.reduce((b, r) => r.effJTH < b.effJTH ? r : b) : null;
    this.bestHR  = hrPool.length    ? hrPool.reduce((b, r) => r.avgHR  > b.avgHR  ? r : b)    : null;
    this.buildChartSeries();
    this.updateCharts();
  }

  // ── Chart view ────────────────────────────────────────────────────────────
  public setView(v: 'table' | 'chart'): void {
    this.activeView = v;
  }

  // Distinct frequency count across usable rows — curves need at least 2 points
  public get chartFreqCount(): number {
    return new Set(this.results.filter(r => r.avgHR > 0 || r.effJTH > 0).map(r => r.freq)).size;
  }

  // Collapse the freq × vcore grid to one row per frequency, matching the
  // meaning of each chart: HR/power chart keeps the max-hashrate row per freq,
  // efficiency chart keeps the min-J/TH row per freq.
  private buildChartSeries(): void {
    const byFreqHR  = new Map<number, BenchmarkResult>();
    const byFreqEff = new Map<number, BenchmarkResult>();
    for (const r of this.results) {
      if (r.avgHR > 0) {
        const cur = byFreqHR.get(r.freq);
        if (!cur || r.avgHR > cur.avgHR) byFreqHR.set(r.freq, r);
      }
      if (r.effJTH > 0) {
        const cur = byFreqEff.get(r.freq);
        if (!cur || r.effJTH < cur.effJTH) byFreqEff.set(r.freq, r);
      }
    }
    this.hrRows  = [...byFreqHR.values()].sort((a, b) => a.freq - b.freq);
    this.effRows = [...byFreqEff.values()].sort((a, b) => a.freq - b.freq);
    this.effBestIdx = this.effRows.length
      ? this.effRows.reduce((bi, r, i, arr) => r.effJTH < arr[bi].effJTH ? i : bi, 0)
      : -1;
  }

  private maybeInitCharts(): void {
    if (this.activeView !== 'chart' || !this.hrCanvas || !this.effCanvas) return;
    if (!this.hrChart)  this.initHrChart();
    if (!this.effChart) this.initEffChart();
    this.updateCharts();
  }

  private chartTheme() {
    const style = getComputedStyle(document.documentElement);
    return {
      text:   style.getPropertyValue('--text-color-secondary').trim() || '#cccccc',
      grid:   style.getPropertyValue('--surface-border').trim() || 'rgba(255, 255, 255, 0.1)',
      mobile: window.innerWidth <= 768
    };
  }

  private initHrChart(): void {
    const ctx = this.hrCanvas?.nativeElement.getContext('2d');
    if (!ctx) return;
    const t = this.chartTheme();

    const config: ChartConfiguration = {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'Avg HR (GH/s)',
            data: [],
            borderColor: '#F79646',
            backgroundColor: '#F79646',
            yAxisID: 'y',
            tension: 0.2,
            pointRadius: 3,
            borderWidth: 2
          },
          {
            label: 'Avg Power (W)',
            data: [],
            borderColor: '#4A7EBB',
            backgroundColor: '#4A7EBB',
            yAxisID: 'y1',
            tension: 0.2,
            pointRadius: 3,
            pointStyle: 'rect',
            borderWidth: 2
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        interaction: { intersect: false, mode: 'index' },
        onHover: (e: any, elements: any[]) => {
          const target = e?.native?.target as HTMLElement | undefined;
          if (target) target.style.cursor = elements?.length ? 'pointer' : 'default';
        },
        onClick: (_e: any, elements: any[]) => {
          if (!elements?.length) return;
          const r = this.hrRows[elements[0].index];
          if (r) this.openApplyConfirm(r);
        },
        plugins: {
          legend: { labels: { color: t.text, boxWidth: 14, font: { size: t.mobile ? 9 : 11 } } },
          tooltip: {
            backgroundColor: 'rgba(30, 30, 30, 0.95)',
            titleColor: '#ffffff',
            bodyColor: '#e2e8f0',
            footerColor: '#7d8ba0',
            borderColor: '#4a4a4a',
            borderWidth: 1,
            cornerRadius: 8,
            padding: 12,
            boxPadding: 5,
            usePointStyle: true,
            titleFont: { size: 13, weight: 'bold' },
            titleMarginBottom: 8,
            bodyFont: { size: 11 },
            bodySpacing: 5,
            footerFont: { size: 10, style: 'italic' },
            footerMarginTop: 8,
            callbacks: {
              title: (items: any[]) => {
                const r = this.hrRows[items[0]?.dataIndex];
                return r ? `${r.freq} MHz  ·  ${r.vcore} mV` : '';
              },
              label: (item: any) => {
                const r = this.hrRows[item.dataIndex];
                if (!r) return '';
                if (item.datasetIndex === 0) {
                  return r.avgHR >= 1000
                    ? ` HR   ${(r.avgHR / 1000).toFixed(2)} TH/s`
                    : ` HR   ${r.avgHR.toFixed(1)} GH/s`;
                }
                return ` Pwr  ${r.avgPwr.toFixed(1)} W`;
              },
              afterBody: (items: any[]) => {
                const r = this.hrRows[items[0]?.dataIndex];
                if (!r) return [];
                return [
                  ` Eff    ${r.effJTH.toFixed(2)} J/TH`,
                  ` ASIC ${this.fmtC(r.avgAsicTemp)}  ·  VRM ${this.fmtC(r.avgVcoreTemp)}`
                ];
              },
              footer: () => 'Click to apply this point'
            }
          }
        },
        scales: {
          x: {
            title: { display: true, text: 'Frequency (MHz)', color: t.text, font: { size: t.mobile ? 9 : 11 } },
            ticks: { color: t.text, font: { size: t.mobile ? 8 : 10 } },
            grid: { color: t.grid }
          },
          y: {
            position: 'left',
            title: { display: true, text: 'Hashrate (GH/s)', color: '#F79646', font: { size: t.mobile ? 9 : 11 } },
            ticks: { color: '#F79646', font: { size: t.mobile ? 8 : 10 } },
            grid: { color: t.grid }
          },
          y1: {
            position: 'right',
            title: { display: true, text: 'Power (W)', color: '#4A7EBB', font: { size: t.mobile ? 9 : 11 } },
            ticks: { color: '#4A7EBB', font: { size: t.mobile ? 8 : 10 } },
            grid: { drawOnChartArea: false }
          }
        }
      }
    };
    this.hrChart = new Chart(ctx, config);
  }

  private initEffChart(): void {
    const ctx = this.effCanvas?.nativeElement.getContext('2d');
    if (!ctx) return;
    const t = this.chartTheme();

    const config: ChartConfiguration = {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'Efficiency (J/TH)',
            data: [],
            borderColor: '#10b981',
            backgroundColor: '#10b981',
            tension: 0.2,
            pointRadius: 3,
            borderWidth: 2
          },
          {
            // Best-point marker: null everywhere except at effBestIdx
            label: 'Best',
            data: [],
            showLine: false,
            pointStyle: 'star',
            pointRadius: 8,
            pointHoverRadius: 9,
            borderColor: '#fbbf24',
            backgroundColor: '#fbbf24'
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        interaction: { intersect: false, mode: 'index' },
        onHover: (e: any, elements: any[]) => {
          const target = e?.native?.target as HTMLElement | undefined;
          if (target) target.style.cursor = elements?.length ? 'pointer' : 'default';
        },
        onClick: (_e: any, elements: any[]) => {
          if (!elements?.length) return;
          const r = this.effRows[elements[0].index];
          if (r) this.openApplyConfirm(r);
        },
        plugins: {
          legend: {
            labels: {
              color: t.text, boxWidth: 14, font: { size: t.mobile ? 9 : 11 },
              filter: (item: any) => item.text !== 'Best'
            }
          },
          tooltip: {
            backgroundColor: 'rgba(30, 30, 30, 0.95)',
            titleColor: '#ffffff',
            bodyColor: '#e2e8f0',
            footerColor: '#7d8ba0',
            borderColor: '#4a4a4a',
            borderWidth: 1,
            cornerRadius: 8,
            padding: 12,
            boxPadding: 5,
            usePointStyle: true,
            titleFont: { size: 13, weight: 'bold' },
            titleMarginBottom: 8,
            bodyFont: { size: 11 },
            bodySpacing: 5,
            footerFont: { size: 10, style: 'italic' },
            footerMarginTop: 8,
            filter: (item: any) => item.datasetIndex === 0,
            callbacks: {
              title: (items: any[]) => {
                const r = this.effRows[items[0]?.dataIndex];
                return r ? `${r.freq} MHz  ·  ${r.vcore} mV` : '';
              },
              label: (item: any) => {
                const r = this.effRows[item.dataIndex];
                return r ? ` Eff   ${r.effJTH.toFixed(2)} J/TH` : '';
              },
              afterBody: (items: any[]) => {
                const r = this.effRows[items[0]?.dataIndex];
                if (!r) return [];
                const hr = r.avgHR >= 1000 ? `${(r.avgHR / 1000).toFixed(2)} TH/s` : `${r.avgHR.toFixed(1)} GH/s`;
                return [
                  ` HR    ${hr}`,
                  ` Pwr  ${r.avgPwr.toFixed(1)} W`,
                  ` ASIC ${this.fmtC(r.avgAsicTemp)}  ·  VRM ${this.fmtC(r.avgVcoreTemp)}`
                ];
              },
              footer: () => 'Click to apply this point'
            }
          }
        },
        scales: {
          x: {
            title: { display: true, text: 'Frequency (MHz)', color: t.text, font: { size: t.mobile ? 9 : 11 } },
            ticks: { color: t.text, font: { size: t.mobile ? 8 : 10 } },
            grid: { color: t.grid }
          },
          y: {
            title: { display: true, text: 'Efficiency (J/TH)', color: '#10b981', font: { size: t.mobile ? 9 : 11 } },
            ticks: { color: '#10b981', font: { size: t.mobile ? 8 : 10 } },
            grid: { color: t.grid }
          }
        }
      },
      plugins: [this.createEffBandPlugin(), this.createBestLabelPlugin()]
    };
    this.effChart = new Chart(ctx, config);
  }

  private updateCharts(): void {
    if (this.hrChart) {
      const maxHR = this.hrRows.reduce((m, r) => Math.max(m, r.avgHR), 0);
      const useTH = maxHR >= 1000;
      this.hrChart.data.labels = this.hrRows.map(r => String(r.freq));
      this.hrChart.data.datasets[0].data = this.hrRows.map(r => useTH ? r.avgHR / 1000 : r.avgHR);
      this.hrChart.data.datasets[0].label = useTH ? 'Avg HR (TH/s)' : 'Avg HR (GH/s)';
      this.hrChart.data.datasets[1].data = this.hrRows.map(r => r.avgPwr > 0 ? r.avgPwr : null) as any;
      const yTitle = (this.hrChart.options.scales?.['y'] as any)?.title;
      if (yTitle) yTitle.text = useTH ? 'Hashrate (TH/s)' : 'Hashrate (GH/s)';
      this.hrChart.update('none');
    }
    if (this.effChart) {
      this.effChart.data.labels = this.effRows.map(r => String(r.freq));
      this.effChart.data.datasets[0].data = this.effRows.map(r => r.effJTH);
      this.effChart.data.datasets[1].data = this.effRows.map((r, i) => i === this.effBestIdx ? r.effJTH : null) as any;
      this.effChart.update('none');
    }
  }

  // Shades the contiguous "high efficiency" band (eff ≤ 105% of best) around the best point
  private createEffBandPlugin() {
    const self = this;
    return {
      id: 'effBand',
      beforeDatasetsDraw(chart: any) {
        const rows = self.effRows;
        if (rows.length < 2 || self.effBestIdx < 0) return;
        const threshold = rows[self.effBestIdx].effJTH * 1.05;
        let lo = self.effBestIdx, hi = self.effBestIdx;
        while (lo > 0 && rows[lo - 1].effJTH <= threshold) lo--;
        while (hi < rows.length - 1 && rows[hi + 1].effJTH <= threshold) hi++;
        if (lo === hi) return;
        const { ctx, chartArea, scales } = chart;
        if (!chartArea) return;
        const x0 = scales.x.getPixelForValue(lo);
        const x1 = scales.x.getPixelForValue(hi);
        ctx.save();
        ctx.fillStyle = 'rgba(16, 185, 129, 0.10)';
        ctx.fillRect(x0, chartArea.top, x1 - x0, chartArea.bottom - chartArea.top);
        ctx.restore();
      }
    };
  }

  // Draws the "Best x J/TH @ freq / vcore" label next to the best-point marker
  private createBestLabelPlugin() {
    const self = this;
    return {
      id: 'bestLabel',
      afterDatasetsDraw(chart: any) {
        if (self.effBestIdx < 0 || !chart.chartArea) return;
        const r = self.effRows[self.effBestIdx];
        const el = chart.getDatasetMeta(1)?.data?.[self.effBestIdx];
        if (!r || !el) return;
        const ctx = chart.ctx;
        const label = `Best ${r.effJTH.toFixed(2)} J/TH @ ${r.freq} MHz / ${r.vcore} mV`;
        ctx.save();
        ctx.font = '11px sans-serif';
        ctx.fillStyle = '#fbbf24';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';
        const w = ctx.measureText(label).width;
        let tx = el.x + 12;
        let ty = el.y - 12;
        if (tx + w > chart.chartArea.right) tx = el.x - 12 - w;
        if (ty < chart.chartArea.top + 8) ty = el.y + 16;
        ctx.fillText(label, tx, ty);
        ctx.restore();
      }
    };
  }

  private startPolling(): void {
    this.pollSub = interval(10000).pipe(
      // A failed poll must not kill the poller. Without catchError the first
      // error terminated the subscription, so a single dropped request (device
      // busy writing NVS, OTA, WiFi hiccup) stopped the page from ever noticing
      // the sweep had finished - it kept showing the last state and the
      // "elapsed" clock forever.
      switchMap(() => this.systemService.getBenchmark().pipe(
        catchError((err: any) => {
          console.warn('benchmark poll failed', err);
          return of(null);
        })
      ))
    ).subscribe((data: any) => {
      if (!data) return;   // transient failure: keep the existing state
      this.isRunning = data.mode === 1;
      this.syncFormLock();
      this.curFreq   = data.curFreq;
      this.curVcore  = data.curVcore;
      this.totalSec  = data.totalSec ?? 0;
      this.startTs   = data.startTs  ?? 0;
      this.setResults(Array.isArray(data.results) ? data.results : []);
      if (!this.isRunning) {
        this.timerSub?.unsubscribe();
        this.pollSub?.unsubscribe();
        this.toastr.success('Benchmark complete!', 'Done');
      }
    });
  }
  private syncFormLock(): void {
    if (!this.form) return;
    this.isRunning ? this.form.disable() : this.form.enable();
  }
  // ── Sorting ───────────────────────────────────────────────────────────────
  public sortBy(col: keyof BenchmarkResult): void {
    if (this.sortCol === col) {
      this.sortDir = this.sortDir === 1 ? -1 : 1;
    } else {
      this.sortCol = col;
      this.sortDir = 1;
    }
  }

  public get sortedResults(): BenchmarkResult[] {
    if (!this.results.length) return [];
    const col = this.sortCol;
    const dir = this.sortDir;
    return [...this.results].sort((a, b) => {
      const av = (a[col] ?? 0) as number;
      const bv = (b[col] ?? 0) as number;
      return av < bv ? -dir : av > bv ? dir : 0;
    });
  }

  // ── Elapsed timer ───────────────────────────────────────────────────────
  private startElapsedTimer(): void {
    this.timerSub?.unsubscribe();
    if (!this.startTs) return;
    this.elapsedSec = Math.floor(Date.now() / 1000) - this.startTs;
    this.timerSub = interval(1000).subscribe(() => {
      this.elapsedSec = Math.floor(Date.now() / 1000) - this.startTs;
    });
  }

  // ── Utilities ─────────────────────────────────────────────────────────────
  public formatDuration(sec: number): string {
    if (!sec || sec <= 0) return '—';
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `${h}h ${m}m ${s}s`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  }

  public formatTs(ts?: number): string {
    if (!ts) return '—';
    return new Date(ts * 1000).toLocaleString([], {
      month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit', hour12: false
    });
  }

  // Temperature with a unit, tolerating "not measured" (null).
  // A failed Vcore read is stored as null; calling toFixed() on it threw and took
  // the whole chart tooltip down with it.
  private fmtC(v: number | null | undefined): string {
    return (typeof v === 'number' && isFinite(v)) ? `${v.toFixed(1)} °C` : 'n/a';
  }

  public sortIcon(col: keyof BenchmarkResult): string {
    if (this.sortCol !== col) return 'pi pi-sort-alt';
    return this.sortDir === 1 ? 'pi pi-sort-amount-up-alt' : 'pi pi-sort-amount-down-alt';
  }

  // ── Start / Stop ──────────────────────────────────────────────────────────
  public openStartConfirm(): void {
    if (!this.form.valid) {
      this.toastr.warning('Please fix form errors before starting.', 'Invalid');
      return;
    }
    this.showStartConfirm = true;
  }

  public confirmStart(): void {
    this.showStartConfirm = false;
    this.doStart();
  }

  public openResumeConfirm(): void {
    this.showResumeConfirm = true;
  }

  public confirmResume(): void {
    this.showResumeConfirm = false;
    this.doResume();
  }

  private doResume(): void {
    if (this.form?.dirty) {
      this.toastr.warning('Parameters changed. Resume keeps the old position. Use Start Fresh to apply new values.', 'Resume blocked');
      return;
    }
    this.systemService.startBenchmark('', { resume: true })
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.isRunning = true;
          this.syncFormLock();
          this.toastr.info('Resuming from last position.', 'Resuming...');
          this.startPolling();
        },
        error: (err: any) => {
          this.toastr.error(`Could not resume: ${err.message}`, 'Error');
        }
      });
  }

  private doStart(): void {
    const payload = this.form.getRawValue();
    this.systemService.startBenchmark('', payload)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.isRunning = true;
          this.syncFormLock();
          this.toastr.info('Benchmark starting — no reboot needed.', 'Starting...');
          this.startPolling();
        },
        error: (err: any) => {
          this.toastr.error(`Could not start: ${err.message}`, 'Error');
        }
      });
  }

  // ── Stop ──────────────────────────────────────────────────────────────────
  public openStopConfirm(): void {
    this.showStopConfirm = true;
  }

  public confirmStop(): void {
    this.showStopConfirm = false;
    this.stop();
  }

  public stop(): void {
    this.systemService.stopBenchmark()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.isRunning = false;
          this.syncFormLock();
          this.pollSub?.unsubscribe();
          this.toastr.info('Benchmark stopped — returning to the previous operating point.', 'Stopped');
        },
        error: (err: any) => {
          this.toastr.error(`Could not stop: ${err.message}`, 'Error');
        }
      });
  }

  // ── Reset ─────────────────────────────────────────────────────────────────
  public openResetConfirm(): void {
    this.showResetConfirm = true;
  }

  public confirmReset(): void {
    this.showResetConfirm = false;
    this.doReset();
  }

  private doReset(): void {
    this.systemService.resetBenchmark()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: (res: any) => {
          // NOTE: results are intentionally kept — only Clear Results can remove them.
          this.isRunning = false;
          this.syncFormLock();
          this.pollSub?.unsubscribe();
          this.timerSub?.unsubscribe();
          // Re-fetch config so the form and all state reflect board defaults after NVS keys are erased.
          this.systemService.getBenchmark().subscribe((data: any) => {
            // Update all runtime state (reset deletes cur_freq/cur_vcore/start_ts/total_sec)
            this.curFreq        = data.curFreq  ?? 0;
            this.curVcore       = data.curVcore ?? 0;
            this.startTs        = data.startTs  ?? 0;
            this.totalSec       = data.totalSec ?? 0;
            this.elapsedSec     = 0;
            this.loadedFreqMin  = data.freqMin;
            this.loadedVcoreMin = data.vcoreMin;
            this.form.patchValue({
              freqMin:    this.clampField(data.freqMin,    100, 5000, 400),
              freqMax:    this.clampField(data.freqMax,    100, 5000, 625),
              freqStep:   this.clampField(data.freqStep,   1,   500,  25),
              vcoreMin:   this.clampField(data.vcoreMin,   600, 2000, 1000),
              vcoreMax:   this.clampField(data.vcoreMax,   600, 2000, 1300),
              vcoreStep:  this.clampField(data.vcoreStep,  1,   200,  25),
              sampleIntv: this.clampField(data.sampleIntv, 1,   300,  2),
              bmTime:     this.clampField(data.bmTime,     15,  7200, 120),
              stabTime:   this.clampField(data.stabTime,   15,  3600, 30),
            });
          });
          const msg: string = res?.message ?? 'Benchmark reset.';
          this.toastr.success(msg, 'Reset');
        },
        error: (err: any) => {
          this.toastr.error(`Could not reset: ${err.message}`, 'Error');
        }
      });
  }

  // ── Clear results ─────────────────────────────────────────────────────────
  public confirmClear(): void {
    this.showClearConfirm = false;
    this.systemService.deleteBenchmarkResults()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.setResults([]);
          this.toastr.success('Results cleared.', 'Done');
        },
        error: (err: any) => {
          this.toastr.error(`Could not clear: ${err.message}`, 'Error');
        }
      });
  }

  // ── Firmware column ───────────────────────────────────────────────────────
  // Results survive an upgrade, so one table can mix rows from several builds.
  public fwText(r: BenchmarkResult): string {
    return r.fw && r.fw.length ? r.fw : '--';
  }

  public fwTip(r: BenchmarkResult): string {
    return r.fw && r.fw.length
      ? `Measured by ${r.fw}.`
      : 'Firmware version not recorded — this result predates the field.';
  }

  // ── Voltage floor ─────────────────────────────────────────────────────────
  // The row stores only the voltage that PASSED, and the sweep starts each
  // frequency one step below the previous success — so "+10 mV above factory"
  // is ambiguous on its own. vFail answers it: either a lower voltage was tried
  // and failed (with how close it came), or the floor was never probed.
  public vcoreTip(r: BenchmarkResult): string {
    if (r.vFail && r.vFail > 0) {
      const pct = (r.vFailPct != null) ? ` at ${r.vFailPct.toFixed(2)} % of expected hashrate` : '';
      return `Lowest voltage that passed. The highest voltage that failed was ` +
             `${r.vFail} mV${pct} — this is a measured floor, not a deliberate margin.`;
    }
    return 'Passed on the first voltage tried at this frequency, so no lower voltage was ' +
           'attempted. This row says nothing about the voltage floor.';
  }

  // ── Per-row delete ────────────────────────────────────────────────────────
  public openDeleteRow(r: BenchmarkResult): void {
    this.pendingDelete = r;
    this.showDeleteRowConfirm = true;
  }

  public confirmDeleteRow(): void {
    const r = this.pendingDelete;
    this.showDeleteRowConfirm = false;
    if (!r) return;
    this.systemService.deleteBenchmarkResult(r.freq)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          // Drop it locally too: the firmware keys rows by frequency, so filtering
          // by freq matches exactly the entry that was removed.
          this.setResults(this.results.filter(x => x.freq !== r.freq));
          this.toastr.success(`Deleted ${r.freq} MHz / ${r.vcore} mV.`, 'Deleted');
          this.pendingDelete = null;
        },
        error: (err: any) => {
          this.toastr.error(`Could not delete: ${err.message}`, 'Error');
        }
      });
  }

  // ── Download ──────────────────────────────────────────────────────────────
  public downloadResults(): void {
    const now  = new Date();
    const pad  = (n: number, len = 2) => String(n).padStart(len, '0');
    const ts   = `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
    const name = [this.deviceDisplayName, this.deviceIp, ts].filter(Boolean).join('_') + '.txt';

    const blob = new Blob([JSON.stringify(this.results, null, 2)], { type: 'application/json' });
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href     = url;
    a.download = name;
    a.click();
    URL.revokeObjectURL(url);
  }

  // ── Apply with confirmation ───────────────────────────────────────────────
  public openApplyConfirm(r: BenchmarkResult): void {
    this.pendingApply = r;
    this.showApplyConfirm = true;
  }

  public confirmApply(): void {
    if (!this.pendingApply) return;
    this.showApplyConfirm = false;
    const r = this.pendingApply;
    this.pendingApply = null;
    this.systemService.applyBenchmarkResult('', r)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success(`Applied ${r.freq} MHz / ${r.vcore} mV — settings active immediately.`, 'Applied');
        },
        error: (err: any) => {
          this.toastr.error(`Could not apply: ${err.message}`, 'Error');
        }
      });
  }
}
