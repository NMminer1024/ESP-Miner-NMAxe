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
  used?: IStratumPool;
  primary?: IStratumPool;
  fallback?: IStratumPool;
}

export interface ISystemInfo {

  // Performance preferences
  screenFlip: number;
  screenAutoRoll: number;
  Brightness: number;
  asicTargetTemp?: string;
  fanAutoSpeed: number;
  ledIndicator: number;

  // Power info
  power: number,
  voltage: number,
  current: number,

  // Temperature info
  asicTemp: number,
  vcoreTemp: number,
  mcuTemp?: number,

  // ASIC info
  asicCount: number,
  vcoreReq: number,
  vcoreActual: number,
  freqReq: number,
  smallCoreCnt: number,
  asic: eASICModel,

  // Fan info
  fanCount: number,
  fans: IFan[],

  // Miner status
  hashRate: number,
  bestDiffEver: string,
  bestDiffSession: string,
  freeHeap: number,
  sharesAccepted: number,
  sharesRejected: number,
  uptimeSeconds: number,

  // Miner info
  fwVersion: string,
  hwModel: string,
  hostName: string,
  timeZone: string,
  timeFormat?: number,
  dateFormat?: string,
  wifiSSID: string,
  wifiStatus: string,

  // Pool info (new nested structure)
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

  // Legacy field names for backward compatibility (can be removed after full migration)
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
  ASICModel?: eASICModel,
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
  // overheat_mode?: number
}
