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
      // Normal = board default pair; measured data attached when the benchmark covers it
      this.presetSource = 'benchmark';
      const entries = [...uniq.values()];
      const turbo = entries.reduce((b, r) => r.avgHR > b.avgHR ? r : b);
      const eco = entries.reduce((b, r) => r.effJTH < b.effJTH ? r : b);

      const fromBm = (key: 'eco' | 'normal' | 'turbo', label: string, b: BenchmarkEntry): ModePreset =>
        ({key, label, freq: b.freq, vcore: b.vcore, hashRate: b.avgHR, power: b.avgPwr, bm: b});

      let normal: ModePreset;
      if (oc.length && vc.length) {
        const nFreq = oc[this.defaultIdx(oc)].value;
        const nVcore = vc[this.defaultIdx(vc)].value;
        const nBm = uniq.get(pair(nFreq, nVcore));
        normal = nBm ? fromBm('normal', 'Normal', nBm)
                     : {key: 'normal', label: 'Normal', freq: nFreq, vcore: nVcore};
      } else {
        const rest = entries.filter(r => r !== turbo && r !== eco);
        const b = rest.length
          ? rest.reduce((best, r) => Math.abs(r.freq - curFreq) < Math.abs(best.freq - curFreq) ? r : best)
          : turbo;
        normal = fromBm('normal', 'Normal', b);
      }

      this.presets = [
        fromBm('eco', 'ECO', eco),
        normal,
        fromBm('turbo', 'Turbo', turbo),
      ];
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
    if (df >= oc.length - 1) {
      turbo.disabled = true;
      turbo.disabledReason = 'Default is already the highest frequency option';
      turbo.disabledHint = 'already at max';
    } else {
      turbo.freq = oc[oc.length - 1].value;
      const vi = this.closestIdx(vc, normalVcore * (turbo.freq / normalFreq), v => v >= normalVcore);
      turbo.vcore = vc[vi === -1 ? dv : vi].value;
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
          this.toastr.success('Freq and Vcore applied.', 'Saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }

}
