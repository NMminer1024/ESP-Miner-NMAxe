import {eASICModel} from './enum/eASICModel';

export interface IFan {
  id: number;
  speed: number;
  rpm: number;
}

export interface IStratumPool {
  url?: string;
  user?: string;
  pwd?: string;
}

export interface IStratum {
  // Flat fields from GET /api/system/info (active pool, read-only)
  url?: string;
  user?: string;
  pwd?: string;
  // Nested fields from GET /api/setting/mining
  primary?: IStratumPool;
  fallback?: IStratumPool;
}

// ── /api/system/info nested sub-objects ──────────────────────────────────────

export interface IInfoPower {
  power: number;   // W
  vbus:  number;   // mV
  ibus:  number;   // mA
}

export interface IInfoTemp {
  vcore: number;    // °C
  mcu:   number;    // °C
  asic:  number;    // °C
}

export interface IInfoAsic {
  count:       number;
  model:       eASICModel;
  vcoreReq:    number;   // mV
  vcoreReal:   number;   // mV
  freqReq:     number;   // MHz
  smallCoreCnt: number;
}

export interface IInfoMiner {
  hashRate:        number;
  bestDiffEver:    string;
  bestDiffSession: string;
  freeHeap:        number;
  sAccepted:       number;
  sRejected:       number;
  uptimeSeconds:   number;
}

export interface IInfoIdentity {
  fwVersion: string;
  hwModel:   string;
  hostName:  string;
  ssid:      string;
  rssi:      number;
}

// ── /api/system/info ─────────────────────────────────────────────────────────
export interface ISystemInfo {

  // Performance preferences
  screenFlip: number;
  screenAutoRoll: number;
  Brightness: number;
  asicTargetTemp?: string;
  fanAutoSpeed: number;
  ledIndicator: number;

  // Nested structured fields (new API)
  power?:    IInfoPower;
  temps?:    IInfoTemp;
  asic?:     IInfoAsic;
  miner?:    IInfoMiner;
  identity?: IInfoIdentity;

  // Fan info
  fans: IFan[],

  // Pool info (nested structure)
  stratum?: IStratum,
  
  // Pool info (legacy flat structure for backward compatibility)
  usedUrl: string,
  usedUser: string,
  primaryUrl: string,
  primaryUser: string,
  primaryPassword: string,
  fallBackUrl: string,
  fallBackUser: string,
  fallBackPassword: string,

  coinPriceDisplay: string, // Price display currency (BTC, BCH, DGB, XEC)
  timeZone: string,
  timeFormat?: number,
  dateFormat?: string,

  // Legacy flat field names (populated by home.component.ts for backward compat)
  power_w?: number,
  voltage?: number,
  current?: number,
  asicTemp?: number,
  vcoreTemp?: number,
  mcuTemp?: number,
  asicCount?: number,
  vcoreReq?: number,
  vcoreActual?: number,
  freqReq?: number,
  smallCoreCnt?: number,
  ASICModel?: eASICModel,
  hashRate?: number,
  bestDiffEver?: string,
  bestDiffSession?: string,
  freeHeap?: number,
  sharesAccepted?: number,
  sharesRejected?: number,
  uptimeSeconds?: number,
  fwVersion?: string,
  hwModel?: string,
  hostName?: string,
  wifiSSID?: string,
  wifiRSSI?: number,
  // older legacy names
  temp?: number,
  vrTemp?: number,
  coreVoltage?: number,
  coreVoltageActual?: number,
  frequency?: number,
  hostname?: string,
  ssid?: string,
  stratumURLUSED?: string,
  stratumURL1?: string,
  stratumURL2?: string,
  stratumUserUSED?: string,
  stratumUser1?: string,
  stratumPassword1?: string,
  stratumUser2?: string,
  stratumPassword2?: string,
  version?: string,
  boardVersion?: string,
  coin?: string,
  brightness?: number,
  bestDiff?: string,
  bestSessionDiff?: string,
  smallCoreCount?: number,
  flipscreen?: number,
  invertscreen?: number,
  ledindicator?: number,
  autofanspeed?: number,
  targetAsicTemp?: string,
  autoscreen?: number,
  fanspeed?: number,
  fanrpm?: number,
  boardtemp2?: number,
}

// ── /api/setting/network ─────────────────────────────────────────────────────
export interface ISettingNetwork {
  hostName: string;
  ssid:     string;
  status:   string;
  ip:       string;
}

// ── /api/setting/time ────────────────────────────────────────────────────────
export interface ISettingTime {
  timeZone: string;
  timeFormat: number;
  dateFormat: string;
}

// ── /api/setting/mining ──────────────────────────────────────────────────────
export interface ISettingMining {
  vcoreReq: number;
  freqReq: number;
  asic: eASICModel;
  stratum: IStratum;
  overclock: { options: Array<{ name: string; value: number }> };
  vcore:     { options: Array<{ name: string; value: number }> };
}

// ── /api/setting/market ──────────────────────────────────────────────────────
export interface ISettingMarket {
  mainprice: string;
  coinWatchlist: string;
}

// ── /api/setting/preference ──────────────────────────────────────────────────
export interface ISettingPreference {
  screenFlip: number;
  ledIndicator: number;
  fanAutoSpeed: number;
  screenAutoRoll: number;
  asicTargetTemp: string;
  Brightness: number;
  fans: IFan[];
}
