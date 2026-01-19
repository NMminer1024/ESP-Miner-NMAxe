export interface LimitRange {
  min: number;
  max: number;
}

export interface PowerLimits {
  vbus: LimitRange;
  ibus: LimitRange;
  power: LimitRange;
}

export interface HeatLimits {
  mcu: LimitRange;
  asic: LimitRange;
  vcore: LimitRange;
  fan: LimitRange;
}

export interface PerformanceLimits {
  asic_freq_req: LimitRange;
  vcore_req: LimitRange;
  vcore_measure: LimitRange;
}

export interface IGaugeLimits {
  power: PowerLimits;
  heat: HeatLimits;
  performance: PerformanceLimits;
}
