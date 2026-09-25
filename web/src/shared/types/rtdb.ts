// TypeScript mirror of docs/DATA_MODEL.md. Change both together.

export type ModuleId = 'shredder' | 'containing' | 'hotpress';
export const MODULE_IDS: ModuleId[] = ['shredder', 'containing', 'hotpress'];

export type Selector = 'NEUTRAL' | 'LEFT' | 'RIGHT';

// ---------- /roles/{uid}, /users/{uid}, /presence/{uid}, /audit/{pushId} ----------
/** superadmin = owner (Users + Activity pages), operator = runs the machine, hub = the hub ESP32. */
export type Role = 'superadmin' | 'operator' | 'hub';

export interface UserNode {
  email: string;
  name: string;
  createdAt: number;
  createdBy?: string;
}

export interface PresenceConnection {
  device: string;
  page: string;
  since: number;
}

export interface PresenceNode {
  /** One entry per open tab/device; removed by the server when that tab disconnects. */
  connections?: Record<string, PresenceConnection>;
  /** Last activity (refreshed every minute while online, and on disconnect). */
  lastSeen?: number;
}

export type AuditAction =
  | 'SIGN_IN'
  | 'SIGN_OUT'
  | 'CONFIG_SAVE'
  | 'COMMAND'
  | 'PRESET_CREATE'
  | 'PRESET_UPDATE'
  | 'PRESET_RENAME'
  | 'PRESET_DELETE'
  | 'USER_ADD'
  | 'USER_RENAME'
  | 'USER_ACCESS'
  | 'PASSWORD_CHANGE'
  | 'USER_PASSWORD';

export interface AuditChange {
  label: string;
  from: string;
  to: string;
}

export interface AuditNode {
  ts: number;
  uid: string;
  email: string;
  action: AuditAction;
  summary: string;
  module?: string;
  changes?: AuditChange[];
  /** COMMAND: the command's push id, and its final status once known. */
  cmdId?: string;
  result?: 'done' | 'failed' | 'expired';
}

// ---------- /hub ----------
export interface HubNode {
  online?: boolean;
  lastSeen?: number;
  bootAt?: number;
  fw?: string;
  ip?: string;
  wifiRssi?: number;
  wifiChannel?: number;
  /** Network the hub is on (hub fw >= 0.3.0). */
  wifiSsid?: string;
  /** Setup hotspot "TileHub-XXXX" open right now (hub fw >= 0.3.0). */
  portal?: boolean;
  /** Health, refreshed every 10 s (hub fw >= 0.3.2). */
  diag?: HubDiag;
  protocolVersion?: number;
  /** Where the hub's clock comes from right now. */
  timeSource?: 'ntp' | 'rtc' | 'none';
  /** DS3231 state: ok, lost-power (battery/time lost, waiting for internet time) or missing. */
  rtc?: 'ok' | 'lost-power' | 'missing';
}

export interface HubDiag {
  heap: number; // free bytes now
  minHeap: number; // lowest free since boot
  block: number; // largest free block (TLS needs a big one)
  uptimeS: number;
  loopMaxMs: number; // longest main-loop pass in the last 10 s
  logSuppressed: number; // log lines not sent (rate limit)
}

// ---------- /hubLog/{key} ----------
export interface HubLogNode {
  ts: number;
  lvl: 'E' | 'I' | 'C'; // error, info, crash report
  msg: string;
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

/** Written with every config save; the rules require editedBy = the saver and editedAt = server time. */
export interface EditStamp {
  editedBy?: string;
  editedAt?: number;
}

export interface ShredderConfig extends EditStamp {
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

export interface ContainingConfig extends EditStamp {
  version: number;
  raw: RawContainerConfig[]; // 0 Raw HDPE, 1 Raw PP
  mixed: MixedContainerConfig[]; // 0 Mixed HDPE, 1 Mixed PP
  servo: ServoConfig[]; // 8 entries, PCA9685 ch 0..7
}

export interface HotpressConfig extends EditStamp {
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
  updatedBy?: string;
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
