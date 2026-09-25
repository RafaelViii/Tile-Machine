import { useEffect, useState, type ComponentType, type ReactNode, type SVGProps } from 'react';
import { limitToLast, onValue, orderByChild, query, ref } from 'firebase/database';
import { db } from '../../lib/firebase';
import { Link } from 'react-router';
import { ContainerIcon, HotpressIcon, HubIcon, ShredderIcon } from '../../components/icons';
import { Badge, Card, CardTitle, Stat, cx } from '../../components/ui';
import { ago, dateTime, faultList, uptime } from '../../shared/format';
import { useMachine, type MachineStatus } from '../../shared/machine';
import type { HubDiag, HubLogNode, ModuleId } from '../../shared/types/rtdb';

type Tone = 'green' | 'amber' | 'red' | 'zinc' | 'sky';

interface RowProps {
  Icon: ComponentType<SVGProps<SVGSVGElement>>;
  name: string;
  esp: string;
  online: boolean;
  status: { text: string; tone: Tone };
  lastSeen: string | null;
  issue: string | null; // shown collapsed only when something needs attention
  children: ReactNode; // details, shown when expanded
}

function Chevron({ open }: { open: boolean }) {
  return (
    <svg
      viewBox="0 0 20 20"
      className={cx('h-4 w-4 shrink-0 text-zinc-500 transition-transform', open && 'rotate-180')}
      fill="none"
      stroke="currentColor"
      strokeWidth={2}
      aria-hidden
    >
      <path d="M5 8l5 5 5-5" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

function DeviceRow({ Icon, name, esp, online, status, lastSeen, issue, children }: RowProps) {
  const [open, setOpen] = useState(false);
  return (
    <li>
      <button
        type="button"
        aria-expanded={open}
        onClick={() => setOpen(!open)}
        className="flex w-full flex-wrap items-center gap-x-3 gap-y-1.5 rounded-lg px-2 py-3 text-left transition hover:bg-zinc-800/40"
      >
        <Icon className={cx('h-5 w-5 shrink-0', online ? 'text-emerald-400' : 'text-zinc-500')} />
        <span className="font-medium">{name}</span>
        <span className="text-xs text-zinc-500">{esp}</span>
        <span className="ml-auto flex items-center gap-2">
          {issue && <Badge tone="amber">⚠ {issue}</Badge>}
          <Badge tone={status.tone}>{status.text}</Badge>
          {lastSeen && <span className="hidden w-20 text-right text-xs text-zinc-500 sm:inline">{lastSeen}</span>}
          <Chevron open={open} />
        </span>
      </button>
      {open && <div className="px-2 pt-1 pb-4">{children}</div>}
    </li>
  );
}

function hubRow(m: MachineStatus) {
  const { hub, hubOnline, now } = m;
  const weak = hubOnline && typeof hub?.wifiRssi === 'number' && hub.wifiRssi < -80;
  const rtcIssue = hub?.rtc === 'lost-power' ? 'Clock battery / not set' : hub?.rtc === 'missing' ? 'Clock (RTC) missing' : null;
  const clock =
    hub?.rtc === 'ok'
      ? `RTC ok · ${hub.timeSource === 'ntp' ? 'internet' : hub.timeSource === 'rtc' ? 'RTC only' : '—'}`
      : hub?.rtc === 'lost-power'
        ? 'RTC not set'
        : hub?.rtc === 'missing'
          ? 'RTC missing'
          : '—';

  return (
    <DeviceRow
      key="hub"
      Icon={HubIcon}
      name="Main hub"
      esp="esp0"
      online={hubOnline}
      status={hubOnline ? { text: 'Online', tone: 'green' } : hub ? { text: 'Offline', tone: 'red' } : { text: 'Never seen', tone: 'zinc' }}
      lastSeen={hub?.lastSeen ? ago(hub.lastSeen, now) : null}
      issue={rtcIssue ?? (weak ? 'Weak WiFi' : null)}
    >
      {!hub ? (
        <p className="text-sm text-zinc-400">The hub hasn't reported yet. Once it's on WiFi it appears here.</p>
      ) : (
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
          <Stat label="Network" value={hub.wifiSsid || '—'} />
          <Stat label="Setup hotspot" value={hub.portal ? 'Open' : 'Off'} tone={hub.portal ? 'sky' : undefined} />
          <Stat label="Up for" value={hub.diag ? uptime(hub.diag.uptimeS) : hub.bootAt ? uptime(Math.round((now - hub.bootAt) / 1000)) : "—"} />
          <Stat label="IP" value={hub.ip ?? '—'} />
          <Stat
            label="WiFi"
            value={hub.wifiRssi !== undefined ? `ch ${hub.wifiChannel ?? '—'} · ${hub.wifiRssi} dBm` : '—'}
            tone={weak ? 'amber' : undefined}
          />
          <Stat label="Clock" value={clock} tone={hub.rtc === 'ok' ? 'green' : hub.rtc ? 'amber' : undefined} />
          <Stat label="Firmware" value={hub.fw ?? '—'} />
          <Stat label="Protocol" value={hub.protocolVersion !== undefined ? `v${hub.protocolVersion}` : '—'} />
        </div>
      )}
      {hub?.diag && <HubHealth d={hub.diag} />}
      {hub && <HubLog />}
    </DeviceRow>
  );
}

function configSync(m: MachineStatus, id: ModuleId): { text: string; tone?: Tone; problem?: string } {
  const node = m.modules[id].node;
  const cfg = node?.config;
  const applied = node?.configApplied;
  if (!cfg) {
    const v = node?.state?.configVersion;
    return { text: v ? `Board v${v}` : 'Defaults' };
  }
  if (applied?.version === cfg.version) {
    if (applied.result === 'rejected') return { text: `Rejected v${cfg.version}`, tone: 'red', problem: 'Settings rejected' };
    if (applied.result === 'queued') return { text: `Queued v${cfg.version}`, tone: 'amber' };
    return { text: `Synced v${cfg.version}`, tone: 'green' };
  }
  return { text: `Pending v${cfg.version}`, tone: 'amber' };
}

function moduleRow(
  m: MachineStatus,
  id: ModuleId,
  name: string,
  esp: string,
  Icon: ComponentType<SVGProps<SVGSVGElement>>,
  to: string,
) {
  const { node, connected } = m.modules[id];
  const everPaired = !!node?.info;
  const faults = connected ? faultList(id, node?.state?.faults) : [];
  const interlock = connected && !!node?.state?.interlock;
  const sync = configSync(m, id);
  const issue = faults.length
    ? faults.length === 1
      ? faults[0]
      : `${faults.length} faults`
    : interlock
      ? 'Set switch to OFF'
      : (sync.problem ?? null);

  return (
    <DeviceRow
      key={id}
      Icon={Icon}
      name={name}
      esp={esp}
      online={connected}
      status={
        connected
          ? { text: 'Online', tone: 'green' }
          : everPaired
            ? { text: 'Offline', tone: 'red' }
            : { text: 'Not paired yet', tone: 'zinc' }
      }
      lastSeen={everPaired ? ago(node?.presence?.lastSeen, m.now) : null}
      issue={issue}
    >
      {!everPaired ? (
        <p className="text-sm text-zinc-400">
          Flash this ESP32 and power it on near the hub. It pairs by itself and appears here. No buttons needed.
        </p>
      ) : (
        <>
          <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
            <Stat label="Uptime" value={connected ? uptime(node?.state?.uptimeS) : '—'} />
            <Stat label="Firmware" value={node?.info?.fw ?? '—'} />
            <Stat label="Settings" value={sync.text} tone={sync.tone} />
            <Stat
              label="Faults"
              value={connected ? (faults.length ? faults.join(', ') : 'None') : '—'}
              tone={connected ? (faults.length ? 'amber' : 'green') : undefined}
            />
            <Stat label="MAC" value={node?.info?.mac ?? '—'} />
            <Stat label="Linked" value={ago(node?.info?.pairedAt, m.now)} />
          </div>
          <Link to={to} className="mt-3 inline-block text-sm font-medium text-emerald-400 hover:underline">
            Open {name.split(' ·')[0]} page →
          </Link>
        </>
      )}
    </DeviceRow>
  );
}

/** Every ESP32 in the system: one compact line each, details on click. */
export function DeviceCards() {
  const m = useMachine();
  const online =
    (m.hubOnline ? 1 : 0) + (['shredder', 'containing', 'hotpress'] as const).filter((id) => m.modules[id].connected).length;

  return (
    <Card>
      <CardTitle right={<span className="text-xs text-zinc-500">{online} / 4 online · click a device for details</span>}>
        Devices
      </CardTitle>
      <ul className="-mx-2 divide-y divide-zinc-800">
        {hubRow(m)}
        {moduleRow(m, 'shredder', 'Shredder', 'esp1', ShredderIcon, '/shredder')}
        {moduleRow(m, 'containing', 'Containing', 'esp2', ContainerIcon, '/containing')}
        {moduleRow(m, 'hotpress', 'Hot Press · Designing · Curing', 'esp3', HotpressIcon, '/hotpress')}
      </ul>
    </Card>
  );
}

const kb = (b: number) => `${Math.round(b / 1024)} KB`;

/** Hub memory + loop health (fw 0.3.2+). "Largest block" is what a new cloud (TLS) connection needs: ~40 KB. */
function HubHealth({ d }: { d: HubDiag }) {
  const low = d.block < 42 * 1024;
  return (
    <div className="mt-3 grid grid-cols-2 gap-3 sm:grid-cols-4">
      <Stat label="Memory free" value={kb(d.heap)} />
      <Stat label="Lowest since start" value={kb(d.minHeap)} tone={d.minHeap < 40 * 1024 ? 'amber' : undefined} />
      <Stat label="Largest block" value={kb(d.block)} tone={low ? 'amber' : undefined} />
      <Stat label="Slowest loop (10 s)" value={`${d.loopMaxMs} ms`} tone={d.loopMaxMs > 500 ? 'amber' : undefined} />
    </div>
  );
}

const LVL = { E: { text: 'Error', tone: 'red' }, I: { text: 'Info', tone: 'zinc' }, C: { text: 'Crash', tone: 'red' } } as const;

/** The hub's own messages (/hubLog, 7 days): errors, WiFi/cloud/hotspot changes, crash reports. */
function HubLog() {
  const [rows, setRows] = useState<(HubLogNode & { id: string })[] | undefined>(undefined);
  const [all, setAll] = useState(false);
  useEffect(
    () =>
      onValue(
        query(ref(db, 'hubLog'), orderByChild('ts'), limitToLast(all ? 200 : 15)),
        (snap) => {
          const list: (HubLogNode & { id: string })[] = [];
          snap.forEach((c) => {
            list.push({ ...(c.val() as HubLogNode), id: c.key! });
          });
          setRows(list.reverse());
        },
        () => setRows([]),
      ),
    [all],
  );
  return (
    <div className="mt-4">
      <div className="mb-2 flex items-center justify-between">
        <span className="text-sm font-semibold">Hub log</span>
        <button type="button" onClick={() => setAll(!all)} className="rounded-md px-2 py-0.5 text-xs text-zinc-400 hover:bg-zinc-800 hover:text-zinc-200">
          {all ? 'Show less' : 'Show more'}
        </button>
      </div>
      {rows === undefined ? (
        <p className="text-xs text-zinc-500">Loading…</p>
      ) : rows.length === 0 ? (
        <p className="text-xs text-zinc-500">No messages yet (hub fw 0.3.2 or newer writes them).</p>
      ) : (
        <ul className={cx('divide-y divide-zinc-800 rounded-xl bg-zinc-950/60 px-3 ring-1 ring-zinc-800', all && 'max-h-96 overflow-y-auto')}>
          {rows.map((r) => (
            <li key={r.id} className="flex flex-wrap items-baseline gap-x-3 gap-y-0.5 py-2 text-xs">
              <span className="w-36 shrink-0 text-zinc-500 tabular-nums">{typeof r.ts === 'number' ? dateTime(r.ts) : ''}</span>
              <Badge tone={LVL[r.lvl]?.tone ?? 'zinc'}>{LVL[r.lvl]?.text ?? r.lvl}</Badge>
              <span className="min-w-0 flex-1 break-words text-zinc-300">{r.msg}</span>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
