// TypeScript mirror of docs/DATA_MODEL.md. Change both together.

export type ModuleId = 'shredder' | 'containing' | 'hotpress';
export const MODULE_IDS: ModuleId[] = ['shredder', 'containing', 'hotpress'];

export type Selector = 'NEUTRAL' | 'LEFT' | 'RIGHT';

// ---------- /hub ----------
export interface HubNode {
  online?: boolean;
  lastSeen?: number;
  bootAt?: number;
  fw?: string;
  ip?: string;
  wifiRssi?: number;
  wifiChannel?: number;
  protocolVersion?: number;
  /** Where the hub's clock comes from right now. */
  timeSource?: 'ntp' | 'rtc' | 'none';
  /** DS3231 state: ok, lost-power (battery/time lost, waiting for internet time) or missing. */
  rtc?: 'ok' | 'lost-power' | 'missing';
}

// ---------- /modules/{id} ----------
export interface ModuleInfo {
  mac: string;
  fw: string;
  pairedAt: number;
}

export interface Presence {
  online: boolean;
  lastSeen: number;
}

export interface ConfigApplied {
  version: number;
  result: 'ok' | 'queued' | 'rejected';
  at: number;
}

interface StateCommon {
  interlock: boolean;
  faults: number;
  uptimeS: number;
  configVersion: number;
}

export interface ShredderState extends StateCommon {
  mode: 'OFF' | 'MANUAL' | 'AUTO';
  state: string;
  relayOn: boolean;
  irDetected: boolean;
  estopLatched: boolean;
  countdownMs: number;
}

export interface ContainerState {
  weightG: number;
  state: string;
  mode: string;
  selectedKg: number;
  progressPct: number;
  remainingMs: number;
  dispensedG: number;
}

export interface ContainingState extends StateCommon {
  selector: Selector;
  hxOkMask: number;
  pcaOk: boolean;
  containers: ContainerState[];
  /** Load-cell calibration, owned by the module (set via CALIBRATE). Not part of config. */
  calFactor: number[];
}

export interface HotpressState extends StateCommon {
  onButton: boolean;
  selector: Selector;
  relayDesignCure: boolean;
  relayHotpress: boolean;
  stopLatched: boolean;
}

export interface ShredderConfig {
  version: number;
  autoStartDelayMs: number;
  autoEmptyStopDelayMs: number;
  manualConfirmTimeoutMs: number;
  irDebounceMs: number;
  buzzerVolumePct: number;
  switchDebounceMs: number; // 3-way switch contacts
  buttonDebounceMs: number; // START / STOP
}

export type RawMode = 'LOADCELL' | 'TIME';
export type MixedMode = 'MANUAL' | 'TIME';

export interface RawContainerConfig {
  mode: RawMode;
  timeTableMs: number[]; // 5 entries: 1..5 kg
  maxDispenseMs: number;
  jamTimeoutMs: number;
  toleranceG: number;
}

export interface MixedContainerConfig {
  mode: MixedMode;
  runTimeMs: number;
}

export interface ServoConfig {
  stopUs: number;
  runUs: number;
}

export interface ContainingConfig {
  version: number;
  raw: RawContainerConfig[]; // 0 Raw HDPE, 1 Raw PP
  mixed: MixedContainerConfig[]; // 0 Mixed HDPE, 1 Mixed PP
  servo: ServoConfig[]; // 8 entries, PCA9685 ch 0..7
}

export interface HotpressConfig {
  version: number;
  autoModeBehaviour: number;
  buttonDebounceMs: number; // ON (latching) button must be stable this long
  selectorDebounceMs: number; // 3-way selector must be stable this long
}

export interface ConfigByModule {
  shredder: ShredderConfig;
  containing: ContainingConfig;
  hotpress: HotpressConfig;
}

export interface StateByModule {
  shredder: ShredderState;
  containing: ContainingState;
  hotpress: HotpressState;
}

export interface ModuleNode<M extends ModuleId> {
  info?: ModuleInfo;
  presence?: Presence;
  state?: StateByModule[M];
  config?: ConfigByModule[M];
  configApplied?: ConfigApplied;
}

// ---------- /commands/{moduleId|all}/{pushId} ----------
export type CommandType = 'STOP' | 'IDENTIFY' | 'TARE' | 'CALIBRATE' | 'REBOOT';
export type CommandStatus = 'pending' | 'sent' | 'done' | 'failed' | 'expired';

export interface CommandNode {
  type: CommandType;
  target?: number;
  arg?: number;
  createdAt: number;
  by: string;
  status: CommandStatus;
  updatedAt?: number;
}

// ---------- /presets/{pushId} ----------
/** A named whole-machine setup. Module configs are stored without `version`. */
export interface PresetNode {
  name: string;
  createdAt: number;
  updatedAt: number;
  by: string;
  shredder?: Omit<ShredderConfig, 'version'>;
  containing?: Omit<ContainingConfig, 'version'>;
  hotpress?: Omit<HotpressConfig, 'version'>;
}

// ---------- /events/{pushId} ----------
export interface EventNode {
  ts: number;
  module: ModuleId | 'hub';
  code: string;
  args?: number[];
}
