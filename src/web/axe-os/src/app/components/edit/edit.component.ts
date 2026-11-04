import {HttpErrorResponse} from '@angular/common/http';
import {Component, Input, OnInit} from '@angular/core';
import {FormBuilder, FormGroup, Validators} from '@angular/forms';
import {ToastrService} from 'ngx-toastr';
import {forkJoin, of} from 'rxjs';
import {catchError} from 'rxjs/operators';
import {LoadingService} from 'src/app/services/loading.service';
import {SystemService} from 'src/app/services/system.service';
import {eASICModel} from 'src/models/enum/eASICModel';

interface BenchmarkEntry {
  freq: number;
  vcore: number;
  expHR: number;       // GH/s
  avgHR: number;       // GH/s
  avgAsicTemp: number;
  avgVcoreTemp: number;
  effJTH: number;      // J/TH
  avgPwr: number;      // W
  ts?: number;         // Unix seconds
  // Peak (worst-case) values recorded during the round — qualification uses these,
  // not the averages: a point whose average is inside the limit but whose peak
  // crosses it is exactly the "it tripped even though the number looked fine" case.
  maxIout?: number;    // A   — PMBus boards only (stack total peak)
  maxPwr?: number;     // W
  maxAsicTemp?: number;// °C
  maxVcoreTemp?: number; // °C
  // Heaviest-phase peak and the runtime phase count. The regulator divides the
  // stack-total OC limit across phases and trips on the heaviest one, so this is
  // the number that actually decides whether a point is safe. 0 on boards with
  // no per-phase telemetry.
  maxPhaseIout?: number; // A
  phases?: number;
  // Firmware-computed verdict: 1 = passed the safety margin, 0 = failed it.
  // ABSENT on results written before the margin rule existed — those are treated
  // as NOT qualified (assuming an unverified point is safe is the failure mode
  // this check exists to prevent).
  q?: number;
  fail?: number;       // 1 = this point tripped protection; must never be selected
}

interface ModePreset {
  key: 'eco' | 'normal' | 'turbo';
  label: string;
  freq: number;   // MHz
  vcore: number;  // mV
  hashRate?: number;    // GH/s, measured — only present for benchmark-derived presets
  power?: number;       // W, measured — only present for benchmark-derived presets
  bm?: BenchmarkEntry;  // source benchmark entry (tooltip detail)
  disabled?: boolean;
  disabledReason?: string;  // tooltip detail
  disabledHint?: string;    // short text shown inside the button
}

@Component({
  selector: 'app-edit',
  templateUrl: './edit.component.html',
  styleUrls: ['./edit.component.scss']
})
export class EditComponent implements OnInit {

  public form!: FormGroup;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;


  public devToolsOpen: boolean = false;
  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;

  @Input() uri = '';

  public DropdownFrequency: Array<{name: string, value: number}> = [];
  public CoreVoltage: Array<{name: string, value: number}> = [];

  public presets: ModePreset[] = [];
  public presetSource: 'benchmark' | 'default' = 'default';
  public selectedMode: 'eco' | 'normal' | 'turbo' | 'custom' = 'custom';
  public advancedOpen = false;
  public modeCooldown = false;
  private applyingPreset = false;
  private static readonly MODE_COOLDOWN_MS = 5000;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService
  ) {

    window.addEventListener('resize', this.checkDevTools);
    this.checkDevTools();

  }

  ngOnInit(): void {
    // Single call to /api/setting/mining returns OC/VC options + stratum config + current freq/vcore
    forkJoin({
      info: this.systemService.getSettingMining(this.uri),
      bm: this.systemService.getBenchmark(this.uri).pipe(catchError(() => of(null)))
    })
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(({info, bm}) => {
        // Populate dropdown options, injecting custom value at sorted position if needed
        const freqVal  = (info.asic as any)?.freqReq  ?? info.freqReq;
        const vcoreVal = (info.asic as any)?.vcoreReq ?? info.vcoreReq;
        if (info.overclock?.options) this.DropdownFrequency = this.ensureCustomOption(info.overclock.options, freqVal,  'MHz');
        if (info.vcore?.options)     this.CoreVoltage       = this.ensureCustomOption(info.vcore.options,     vcoreVal, 'mV');

        // Map ASIC model: /api/setting/mining returns asic as a plain string (e.g. "BM1366")
        this.ASICModel = (info.asic as any)?.model ?? info.asic ?? info.ASICModel;

        // Parse stratum URLs
        const stratumURL1 = info.stratum?.primary?.url  || '';
        const stratumURL2 = info.stratum?.fallback?.url || '';
        const stratumUser1 = info.stratum?.primary?.user  || '';
        const stratumUser2 = info.stratum?.fallback?.user || '';
        const stratumPwd1  = info.stratum?.primary?.pwd   || '';
        const stratumPwd2  = info.stratum?.fallback?.pwd  || '';

        this.form = this.fb.group({
          stratumURL1: [stratumURL1 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],
          stratumURL2: [stratumURL2 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],
          stratumUser1: [stratumUser1, [Validators.required]],
          stratumUser2: [stratumUser2, [Validators.required]],
          stratumPassword1: [stratumPwd1],
          stratumPassword2: [stratumPwd2],
          coreVoltage: [(info.asic as any)?.vcoreReq ?? info.vcoreReq, [Validators.required]],
          frequency:   [(info.asic as any)?.freqReq  ?? info.freqReq,  [Validators.required]],
        });

        // Performance mode presets: prefer benchmark results, fall back to board OC/VC lists
        this.computePresets(
          Array.isArray(bm?.results) ? bm.results : [],
          info.overclock?.options ?? [],
          info.vcore?.options ?? [],
          freqVal, vcoreVal
        );

        // Derive the current mode; any manual edit that leaves a preset marks the mode Custom
        const matched = this.presets.find(p => !p.disabled && p.freq === freqVal && p.vcore === vcoreVal);
        this.selectedMode = matched ? matched.key : 'custom';
        this.advancedOpen = false;  // always start collapsed; Custom users expand it manually
        this.form.valueChanges.subscribe(v => {
          if (this.applyingPreset) return;
          const m = this.presets.find(p => !p.disabled && p.freq === v.frequency && p.vcore === v.coreVoltage);
          this.selectedMode = m ? m.key : 'custom';
        });
      });
  }

  public onModeSelect(key: 'eco' | 'normal' | 'turbo'): void {
    if (this.modeCooldown) return;
    const p = this.presets.find(x => x.key === key);
    if (!p || p.disabled || p.key === this.selectedMode) return;

    const prev = {frequency: this.form.value.frequency, coreVoltage: this.form.value.coreVoltage};
    this.applyingPreset = true;
    this.DropdownFrequency = this.ensureCustomOption(this.DropdownFrequency, p.freq, 'MHz');
    this.CoreVoltage = this.ensureCustomOption(this.CoreVoltage, p.vcore, 'mV');
    this.form.patchValue({frequency: p.freq, coreVoltage: p.vcore});
    this.selectedMode = p.key;
    this.applyingPreset = false;

    // Apply immediately: firmware hot-switches freq/vcore and persists to NVS
    this.modeCooldown = true;
    setTimeout(() => this.modeCooldown = false, EditComponent.MODE_COOLDOWN_MS);
    this.systemService.patchSettingMining(this.uri, {asicVcoreReq: p.vcore, asicFreqReq: p.freq})
      .subscribe({
        next: () => {
          this.toastr.success(`${p.label}: ${p.freq}MHz @ ${p.vcore}mV`, 'Applied.');
        },
        error: (err: HttpErrorResponse) => {
          this.applyingPreset = true;
          this.form.patchValue(prev);
          this.applyingPreset = false;
          this.selectedMode = this.presets.find(m => !m.disabled && m.freq === prev.frequency && m.vcore === prev.coreVoltage)?.key ?? 'custom';
          this.toastr.error('Error.', `Could not apply ${p.label}. ${err.message}`);
        }
      });
  }

  public hashValue(ghs: number): string {
    return ghs >= 1000 ? (ghs / 1000).toFixed(2) : `${Math.round(ghs)}`;
  }

  public hashUnit(ghs: number): string {
    return ghs >= 1000 ? 'TH/s' : 'GH/s';
  }

  public formatHashRate(ghs: number): string {
    return `~${this.hashValue(ghs)}${this.hashUnit(ghs)}`;
  }

  // Keep tooltips inside the viewport: edge buttons open sideways instead of centered above.
  public tooltipPos(p: ModePreset): string {
    return p.key === 'eco' ? 'right' : p.key === 'turbo' ? 'left' : 'top';
  }

  public presetTooltip(p: ModePreset): string {
    if (p.disabled) return p.disabledReason || '';
    const wrap = (title: string, sub: string, rows: Array<[string, string]>) =>
      `<div class="bm-tip bm-tip-${p.key}">` +
      `<div class="bm-tip-title">${title}</div>` +
      `<div class="bm-tip-sub">${sub}</div>` +
      `<table>${rows.map(([k, v]) => `<tr><td class="k">${k}</td><td class="v">${v}</td></tr>`).join('')}</table>` +
      `</div>`;

    const b = p.bm;
    if (!b) {
      return wrap(p.label, `${p.freq}MHz @ ${p.vcore}mV`, this.presetSource === 'benchmark'
        ? [['Source', 'Board default'], ['Note', 'Not covered by benchmark results']]
        : [['Source', 'Board defaults'], ['Note', 'Run benchmark for measured data']]);
    }
    const rows: Array<[string, string]> = [
      ['Hashrate', this.formatHashRate(b.avgHR)],
      ['Expected', this.formatHashRate(b.expHR)],
      ['Efficiency', `${b.effJTH.toFixed(1)} J/TH`],
      ['Power', `${b.avgPwr.toFixed(1)} W`],
      ['Temp ASIC', `${b.avgAsicTemp.toFixed(1)} °C`],
      ['Temp Vcore', `${b.avgVcoreTemp.toFixed(1)} °C`],
    ];
    if (b.ts) rows.push(['Measured', new Date(b.ts * 1000).toLocaleString()]);
    // Show the per-phase peak when the board reports it: it is the value the
    // regulator actually trips on (limit / phases), not the stack total.
    if (b.maxPhaseIout && b.phases) {
      rows.push(['Peak phase', `${b.maxPhaseIout.toFixed(1)} A (of ${b.phases})`]);
    }
    return wrap(`${p.label} — from benchmark`, `${b.freq}MHz @ ${b.vcore}mV`, rows);
  }

  private computePresets(
    results: BenchmarkEntry[],
    oc: Array<{name: string, value: number}>,
    vc: Array<{name: string, value: number}>,
    curFreq: number,
    curVcore: number
  ): void {
    const pair = (freq: number, vcore: number) => `${freq}:${vcore}`;
    const uniq = new Map<string, BenchmarkEntry>();
    for (const r of results) {
      if (r && r.freq > 0 && r.vcore > 0 && !uniq.has(pair(r.freq, r.vcore))) uniq.set(pair(r.freq, r.vcore), r);
    }

    if (uniq.size >= 3) {
      // Benchmark-derived: ECO = best efficiency (lowest J/TH), Turbo = best hashrate,
      // Normal = board default pair if the benchmark covers it, otherwise the entry
      // closest to the default point (ties broken by lower measured power)
      this.presetSource = 'benchmark';
      const entries = [...uniq.values()];
      // Eligibility gate — mirrors the firmware rule (benchmark_threads.cpp):
      //   fail=1          -> this point tripped protection, never select it
      //   q != 1          -> did not pass the safety margin (or predates the rule)
      //   avgHR <= 0      -> no measured hashrate (tombstone / aborted round)
      const eligible = entries.filter(r => !r.fail && r.q === 1 && r.avgHR > 0);
      // ECO needs at least TWO qualified points to mean anything: with a single
      // point "lowest J/TH" and "highest hashrate" are the same entry, so ECO and
      // Turbo would be identical buttons. Field reports read that as a bug (Turbo
      // showing a LOWER hashrate than Normal, because Normal still quoted an
      // unqualified historical entry). Disable ECO instead of duplicating Turbo.
      const turboOk = eligible.length >= 1;
      const ecoOk   = eligible.length >= 2;
      // Display-only fallbacks when nothing qualifies: the best raw entry, so the
      // tooltip can still explain what the benchmark measured. Never applied —
      // the button is disabled in that case.
      const turbo = turboOk
        ? eligible.reduce((b, r) => r.avgHR > b.avgHR ? r : b)
        : entries.reduce((b, r) => r.avgHR > b.avgHR ? r : b);
      const eco = ecoOk
        ? eligible.reduce((b, r) => r.effJTH < b.effJTH ? r : b)
        : turbo;

      const fromBm = (key: 'eco' | 'normal' | 'turbo', label: string, b: BenchmarkEntry): ModePreset =>
        ({key, label, freq: b.freq, vcore: b.vcore, hashRate: b.avgHR, power: b.avgPwr, bm: b});

      let normal: ModePreset;
      if (oc.length && vc.length) {
        const nFreq = oc[this.defaultIdx(oc)].value;
        const nVcore = vc[this.defaultIdx(vc)].value;
        // Normal quotes measurements ONLY from a qualified entry. Previously it
        // looked the board-default pair up in the raw map, so an unqualified (or
        // outright FAILED) entry could supply the shown hashrate/power — the button
        // then advertised a point the safety rule had just rejected.
        const nBm = uniq.get(pair(nFreq, nVcore));
        if (nBm && !nBm.fail && nBm.q === 1 && nBm.avgHR > 0) {
          normal = fromBm('normal', 'Normal', nBm);
        } else {
          // Nearest QUALIFIED point for the numbers, but keep the board-default
          // freq/vcore as the actual target (Normal's definition is the default).
          const near = eligible.length
            ? eligible.reduce((b, r) => {
                const dr = Math.abs(r.freq - nFreq) + Math.abs(r.vcore - nVcore);
                const db = Math.abs(b.freq - nFreq) + Math.abs(b.vcore - nVcore);
                return dr < db || (dr === db && r.avgPwr < b.avgPwr) ? r : b;
              })
            : null;
          normal = {key: 'normal', label: 'Normal', freq: nFreq, vcore: nVcore, bm: near ?? undefined};
        }
      } else {
        const rest = eligible.filter(r => r !== turbo && r !== eco);
        const b = rest.length
          ? rest.reduce((best, r) => Math.abs(r.freq - curFreq) < Math.abs(best.freq - curFreq) ? r : best)
          : turbo;
        normal = fromBm('normal', 'Normal', b);
      }

      // A disabled preset must not carry measured values: the button would then
      // advertise a hashrate for a point the user is not allowed to select.
      const ecoPreset   = fromBm('eco',   'ECO',   eco);
      const turboPreset = fromBm('turbo', 'Turbo', turbo);
      if (!ecoOk) {
        ecoPreset.disabled = true;
        if (eligible.length === 1) {
          // One qualified point: ECO would just be a copy of Turbo. Say so plainly
          // instead of showing two buttons that do the same thing.
          ecoPreset.disabledHint = 'needs more data';
          ecoPreset.disabledReason = 'Only one benchmark point has passed the safety margin, so ECO and Turbo would be the same point. Run the benchmark over a wider frequency range to get a low-power option.';
        } else {
          ecoPreset.disabledHint = 'no safe point';
          ecoPreset.disabledReason = 'No benchmark point passed the safety margin.';
        }
        ecoPreset.hashRate = undefined;
        ecoPreset.power = undefined;
      }
      if (!turboOk) {
        turboPreset.disabled = true;
        turboPreset.disabledHint = 'no safe point';
        turboPreset.disabledReason = 'No benchmark point passed the safety margin (peak current or temperature too close to the limit). Re-run the benchmark, or pick a point manually in Advanced.';
        turboPreset.hashRate = undefined;
        turboPreset.power = undefined;
      }
      this.presets = [ecoPreset, normal, turboPreset];
      return;
    }

    // Fallback: anchor Normal on the board defaults, scale ECO/Turbo around them
    this.presetSource = 'default';
    if (!oc.length || !vc.length) { this.presets = []; return; }
    const df = this.defaultIdx(oc);
    const dv = this.defaultIdx(vc);
    const normalFreq = oc[df].value, normalVcore = vc[dv].value;

    const eco: ModePreset = {key: 'eco', label: 'ECO', freq: 0, vcore: 0};
    const fi = this.closestIdx(oc, normalFreq * 0.85, v => v < normalFreq);
    if (fi === -1) {
      eco.disabled = true;
      eco.disabledReason = 'No frequency option below default';
      eco.disabledHint = 'no lower option';
    } else {
      eco.freq = oc[fi].value;
      const vi = this.closestIdx(vc, normalVcore * (eco.freq / normalFreq), v => v <= normalVcore);
      eco.vcore = vc[vi === -1 ? 0 : vi].value;
    }

    const turbo: ModePreset = {key: 'turbo', label: 'Turbo', freq: 0, vcore: 0};
    // Without a qualified benchmark point, Turbo must NOT fall back to "highest
    // frequency at the highest reachable voltage". That combination is the LEAST
    // verified point on the board, and selecting it silently is exactly how units
    // ended up tripping. Keep the button visible but disabled, with the reason.
    turbo.disabled = true;
    if (df >= oc.length - 1) {
      turbo.disabledHint = 'already at max';
      turbo.disabledReason = 'Default is already the highest frequency option.';
    } else {
      turbo.disabledHint = 'needs benchmark';
      turbo.disabledReason = 'Turbo requires a benchmark result that passed the safety margin. Run a benchmark first — Advanced still allows manual tuning.';
    }

    this.presets = [eco, {key: 'normal', label: 'Normal', freq: normalFreq, vcore: normalVcore}, turbo];
  }

  private defaultIdx(options: Array<{name: string, value: number}>): number {
    const i = options.findIndex(o => /default/i.test(o.name));
    return i >= 0 ? i : Math.floor(options.length / 2);
  }

  // Index of the option closest to target; on ties the lower value wins.
  private closestIdx(
    options: Array<{name: string, value: number}>,
    target: number,
    accept: (v: number) => boolean
  ): number {
    let best = -1;
    options.forEach((o, i) => {
      if (!accept(o.value)) return;
      if (best === -1 || Math.abs(o.value - target) < Math.abs(options[best].value - target)) best = i;
    });
    return best;
  }

  // Insert curVal at its sorted position in options with '*' suffix if not already present.
  private ensureCustomOption(
    options: Array<{name: string, value: number}>,
    curVal: number,
    unit: string
  ): Array<{name: string, value: number}> {
    if (curVal == null || options.some(o => o.value === curVal)) return options;
    const result = [...options];
    const insertIdx = result.findIndex(o => o.value > curVal);
    const entry = { name: `${curVal} ${unit}*`, value: curVal };
    if (insertIdx === -1) result.push(entry);
    else result.splice(insertIdx, 0, entry);
    return result;
  }


  private checkDevTools = () => {
    if (
      window.outerWidth - window.innerWidth > 160 ||
      window.outerHeight - window.innerHeight > 160
    ) {
      this.devToolsOpen = true;
    } else {
      this.devToolsOpen = false;
    }
  };

  public updateSystem() {

    const formValue = this.form.getRawValue();

    // Transform flat form data to nested stratum structure
    const form = {
      stratum: {
        primary: {
          url: formValue.stratumURL1,
          user: formValue.stratumUser1,
          pwd: formValue.stratumPassword1 || 'x'
        },
        fallback: {
          url: formValue.stratumURL2,
          user: formValue.stratumUser2,
          pwd: formValue.stratumPassword2 || 'x'
        }
      },
      asicVcoreReq: formValue.coreVoltage,
      asicFreqReq: formValue.frequency
    };

    this.systemService.patchSettingMining(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('All settings applied in real time.', 'Saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }

}
