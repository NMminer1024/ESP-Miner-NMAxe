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
  avgTemp: number;
  effJTH: number;
  avgPwr: number;
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
  public results: BenchmarkResult[] = [];
  private pollSub?: Subscription;

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
        this.results   = Array.isArray(data.results) ? data.results : [];

        this.form = this.fb.group({
          freqMin:    [data.freqMin,    [Validators.required, Validators.min(100), Validators.max(1000)]],
          freqMax:    [data.freqMax,    [Validators.required, Validators.min(100), Validators.max(1000)]],
          freqStep:   [data.freqStep,   [Validators.required, Validators.min(1),   Validators.max(500)]],
          vcoreMin:   [data.vcoreMin,   [Validators.required, Validators.min(600), Validators.max(1500)]],
          vcoreMax:   [data.vcoreMax,   [Validators.required, Validators.min(600), Validators.max(1500)]],
          vcoreStep:  [data.vcoreStep,  [Validators.required, Validators.min(1),   Validators.max(200)]],
          sampleIntv: [data.sampleIntv, [Validators.required, Validators.min(5),   Validators.max(300)]],
          bmTime:     [data.bmTime,     [Validators.required, Validators.min(30),  Validators.max(3600)]],
          stabTime:   [data.stabTime,   [Validators.required, Validators.min(30),  Validators.max(3600)]],
        });

        if (this.isRunning) {
          this.startPolling();
        }
      });
  }

  ngOnDestroy(): void {
    this.pollSub?.unsubscribe();
  }

  private startPolling(): void {
    this.pollSub = interval(10000).pipe(
      switchMap(() => this.systemService.getBenchmark())
    ).subscribe((data: any) => {
      this.isRunning = data.mode === 1;
      this.curFreq   = data.curFreq;
      this.curVcore  = data.curVcore;
      this.results   = Array.isArray(data.results) ? data.results : [];
      if (!this.isRunning) {
        this.pollSub?.unsubscribe();
        this.toastr.success('Benchmark complete!', 'Done');
      }
    });
  }

  public start(): void {
    if (!this.form.valid) {
      this.toastr.warning('Please fix form errors before starting.', 'Invalid');
      return;
    }
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

  public clearResults(): void {
    this.systemService.deleteBenchmarkResults()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.results = [];
          this.toastr.success('Results cleared.', 'Done');
        },
        error: (err: any) => {
          this.toastr.error(`Could not clear: ${err.message}`, 'Error');
        }
      });
  }

  public reset(): void {
    this.systemService.resetBenchmark()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: (res: any) => {
          this.results = [];
          this.isRunning = false;
          this.pollSub?.unsubscribe();
          const msg: string = res?.message ?? 'Benchmark reset.';
          const willReboot = msg.includes('reboot');
          if (willReboot) {
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

  public downloadResults(): void {
    const blob = new Blob([JSON.stringify(this.results, null, 2)], { type: 'application/json' });
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href     = url;
    a.download = `benchmark_results_${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  /** Best efficiency entry (lowest J/TH) */
  public bestResult(): BenchmarkResult | null {
    if (!this.results.length) return null;
    return this.results.reduce((best, r) => r.effJTH < best.effJTH ? r : best);
  }
}
