import { Component, OnInit, OnDestroy } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { interval, Subscription } from 'rxjs';
import { switchMap } from 'rxjs/operators';
import { SystemService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';

export interface BenchmarkResult {
  freq: number;
  vcore: number;
  expHR: number;
  avgHR: number;
  avgAsicTemp: number;
  avgVcoreTemp: number;
  effJTH: number;
  avgPwr: number;
  ts?: number;  // Unix timestamp (seconds)
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
  public sortCol: keyof BenchmarkResult = 'ts';
  public sortDir: 1 | -1 = 1;

  // Pre-computed best entries (updated whenever results change)
  public bestEff: BenchmarkResult | null = null;
  public bestHR:  BenchmarkResult | null = null;

  // Confirmation dialog visibility
  public showStartConfirm = false;
  public showResetConfirm = false;
  public showApplyConfirm = false;
  public pendingApply: BenchmarkResult | null = null;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {}

  ngOnInit(): void {
    this.systemService.getBenchmark()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe((data: any) => {
        this.isRunning = data.mode === 1;
        this.curFreq   = data.curFreq;
        this.curVcore  = data.curVcore;
        this.totalSec  = data.totalSec ?? 0;
        this.startTs   = data.startTs  ?? 0;
        this.setResults(Array.isArray(data.results) ? data.results : []);

        if (this.isRunning) this.startElapsedTimer();

        this.form = this.fb.group({
          freqMin:    [data.freqMin,    [Validators.required, Validators.min(100), Validators.max(5000)]],
          freqMax:    [data.freqMax,    [Validators.required, Validators.min(100), Validators.max(5000)]],
          freqStep:   [data.freqStep,   [Validators.required, Validators.min(1),   Validators.max(500)]],
          vcoreMin:   [data.vcoreMin,   [Validators.required, Validators.min(600), Validators.max(2000)]],
          vcoreMax:   [data.vcoreMax,   [Validators.required, Validators.min(600), Validators.max(2000)]],
          vcoreStep:  [data.vcoreStep,  [Validators.required, Validators.min(1),   Validators.max(200)]],
          sampleIntv: [data.sampleIntv, [Validators.required, Validators.min(1),   Validators.max(300)]],
          bmTime:     [data.bmTime,     [Validators.required, Validators.min(30),  Validators.max(7200)]],
          stabTime:   [data.stabTime,   [Validators.required, Validators.min(30),  Validators.max(3600)]],
        });

        if (this.isRunning) {
          this.startPolling();
        }
      });
  }

  ngOnDestroy(): void {
    this.pollSub?.unsubscribe();
    this.timerSub?.unsubscribe();
  }

  // ── Disabled button tooltip messages ─────────────────────────────────────
  public get startDisabledTip(): string {
    if (this.isRunning) return 'Benchmark is currently running — stop it first';
    if (this.form && !this.form.valid) return this.formInvalidReason;
    return '';
  }

  public get stopDisabledTip(): string {
    return !this.isRunning ? 'No benchmark is currently running' : '';
  }

  public get clearDisabledTip(): string {
    return this.isRunning ? 'Cannot clear results while a benchmark is running' : '';
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

  // ── Results management ────────────────────────────────────────────────────
  private setResults(results: BenchmarkResult[]): void {
    this.results = results;
    this.bestEff = results.length ? results.reduce((b, r) => r.effJTH < b.effJTH ? r : b) : null;
    this.bestHR  = results.length ? results.reduce((b, r) => r.avgHR  > b.avgHR  ? r : b) : null;
  }

  private startPolling(): void {
    this.pollSub = interval(10000).pipe(
      switchMap(() => this.systemService.getBenchmark())
    ).subscribe((data: any) => {
      this.isRunning = data.mode === 1;
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

  private doStart(): void {
    const payload = this.form.getRawValue();
    this.systemService.startBenchmark('', payload)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.isRunning = true;
          this.toastr.info('Benchmark starting — device will reboot.', 'Starting...');
          this.startPolling();
        },
        error: (err: any) => {
          this.toastr.error(`Could not start: ${err.message}`, 'Error');
        }
      });
  }

  public stop(): void {
    this.systemService.stopBenchmark()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.isRunning = false;
          this.pollSub?.unsubscribe();
          this.toastr.info('Benchmark stopped — device will reboot.', 'Stopped');
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
          this.setResults([]);
          this.isRunning = false;
          this.pollSub?.unsubscribe();
          // Re-fetch config so the form reflects board defaults after NVS keys are erased.
          this.systemService.getBenchmark().subscribe((data: any) => {
            this.form.patchValue({
              freqMin:    data.freqMin,
              freqMax:    data.freqMax,
              freqStep:   data.freqStep,
              vcoreMin:   data.vcoreMin,
              vcoreMax:   data.vcoreMax,
              vcoreStep:  data.vcoreStep,
              sampleIntv: data.sampleIntv,
              bmTime:     data.bmTime,
              stabTime:   data.stabTime,
            });
          });
          const msg: string = res?.message ?? 'Benchmark reset.';
          if (msg.includes('reboot')) {
            this.toastr.info(msg, 'Reset');
          } else {
            this.toastr.success(msg, 'Reset');
          }
        },
        error: (err: any) => {
          this.toastr.error(`Could not reset: ${err.message}`, 'Error');
        }
      });
  }

  // ── Clear results ─────────────────────────────────────────────────────────
  public clearResults(): void {
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

  // ── Download ──────────────────────────────────────────────────────────────
  public downloadResults(): void {
    const blob = new Blob([JSON.stringify(this.results, null, 2)], { type: 'application/json' });
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href     = url;
    a.download = `benchmark_results_${Date.now()}.json`;
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
          this.toastr.info(`Applied ${r.freq} MHz / ${r.vcore} mV — device will reboot.`, 'Applied');
        },
        error: (err: any) => {
          this.toastr.error(`Could not apply: ${err.message}`, 'Error');
        }
      });
  }
}
