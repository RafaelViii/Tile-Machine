import { Fragment, type ComponentType, type SVGProps } from 'react';
import { Link } from 'react-router';
import { ArrowRight, ContainerIcon, CuringIcon, DesignIcon, HotpressIcon, HubIcon, ShredderIcon } from '../../components/icons';
import { Badge, Card, CardTitle, PageHeader, Stat, StatusDot, cx } from '../../components/ui';
import { ago, humanize, kg, uptime } from '../../shared/format';
import { useMachine, type MachineStatus } from '../../shared/machine';
import type { ModuleId } from '../../shared/types/rtdb';

interface Process {
  name: string;
  device: ModuleId;
  esp: string;
  to: string;
  Icon: ComponentType<SVGProps<SVGSVGElement>>;
  summary: (m: MachineStatus) => string | null;
}

const RAW = ['R-HDPE', 'R-PP', 'M-HDPE', 'M-PP'];

const processes: Process[] = [
  {
    name: 'Shredder',
    device: 'shredder',
    esp: 'esp1',
    to: '/shredder',
    Icon: ShredderIcon,
    summary: (m) => {
      const s = m.modules.shredder.node?.state;
      return s ? `${s.mode} · ${humanize(s.state)}` : null;
    },
  },
  {
    name: 'Containing',
    device: 'containing',
    esp: 'esp2',
    to: '/containing',
    Icon: ContainerIcon,
    summary: (m) => {
      const cs = m.modules.containing.node?.state?.containers;
      if (!cs) return null;
      const busy = cs.findIndex((c) => c.state === 'DISPENSING');
      return busy >= 0 ? `${RAW[busy]} dispensing ${cs[busy].progressPct}%` : `Total ${kg(cs.reduce((a, c) => a + (c.weightG || 0), 0))}`;
    },
  },
  {
    name: 'Hot Press',
    device: 'hotpress',
    esp: 'esp3',
    to: '/hotpress',
    Icon: HotpressIcon,
    summary: (m) => {
      const s = m.modules.hotpress.node?.state;
      if (!s) return null;
      if (s.selector === 'RIGHT') return 'AUTO';
      return s.relayHotpress ? 'Heating' : 'Cooling';
    },
  },
  {
    name: 'Designing',
    device: 'hotpress',
    esp: 'esp3',
    to: '/hotpress',
    Icon: DesignIcon,
    summary: (m) => {
      const s = m.modules.hotpress.node?.state;
      return s ? (s.relayDesignCure ? 'On' : 'Off') : null;
    },
  },
  {
    name: 'Curing',
    device: 'hotpress',
    esp: 'esp3',
    to: '/hotpress',
    Icon: CuringIcon,
    summary: (m) => {
      const s = m.modules.hotpress.node?.state;
      return s ? (s.relayDesignCure ? 'On' : 'Off') : null;
    },
  },
];

function ProcessTile({ p, m }: { p: Process; m: MachineStatus }) {
  const connected = m.modules[p.device].connected;
  const summary = connected ? p.summary(m) : null;
  return (
    <Link
      to={p.to}
      className={cx(
        'group relative flex min-h-40 flex-1 basis-40 flex-col items-center justify-center gap-2 rounded-2xl border p-4 text-center transition',
        connected
          ? 'tile-connected border-emerald-500/50 bg-emerald-500/[0.07]'
          : 'border-zinc-800 bg-zinc-900/40 opacity-50 grayscale hover:opacity-70',
      )}
    >
      <span className="absolute top-3 left-3 font-mono text-[10px] text-zinc-500">{p.esp}</span>
      <span className="absolute top-3 right-3">
        <StatusDot on={connected} />
      </span>
      <p.Icon className={cx('h-10 w-10', connected ? 'text-emerald-300' : 'text-zinc-500')} />
      <div className="font-semibold">{p.name}</div>
      <div className={cx('text-xs font-medium', connected ? 'text-emerald-300' : 'text-zinc-500')}>
        {connected ? 'Connected' : 'Not connected'}
      </div>
      {summary && <div className="font-mono text-[11px] text-zinc-400">{summary}</div>}
    </Link>
  );
}

export function DashboardPage() {
  const m = useMachine();
  const { hub, hubOnline, now } = m;
  const connectedCount = (['shredder', 'containing', 'hotpress'] as const).filter((id) => m.modules[id].connected).length;

  return (
    <>
      <PageHeader
        title="Dashboard"
        subtitle="Modules light up automatically as soon as they pair with the hub."
        right={<Badge tone={connectedCount === 3 ? 'green' : 'zinc'}>{connectedCount} / 3 modules connected</Badge>}
      />

      <Card className="mb-6">
        <CardTitle>Process line</CardTitle>
        <div className="flex flex-wrap items-stretch gap-3">
          {processes.map((p, i) => (
            <Fragment key={p.name}>
              {i > 0 && <ArrowRight className="hidden h-5 w-5 shrink-0 self-center text-zinc-600 lg:block" />}
              <ProcessTile p={p} m={m} />
            </Fragment>
          ))}
        </div>
      </Card>

      <Card>
        <CardTitle
          right={
            <Badge tone={hubOnline ? 'green' : 'red'}>
              <StatusDot on={hubOnline} /> {hubOnline ? 'Online' : 'Offline'}
            </Badge>
          }
        >
          <span className="flex items-center gap-2">
            <HubIcon className="h-4 w-4" /> Main hub · esp0
          </span>
        </CardTitle>
        {!hub ? (
          <p className="text-sm text-zinc-400">
            The hub hasn't connected to Firebase yet. Once its firmware is flashed and on WiFi, it shows up here and the
            modules start lighting up.
          </p>
        ) : (
          <div className="grid grid-cols-2 gap-3 sm:grid-cols-4 lg:grid-cols-7">
            <Stat label="Last seen" value={ago(hub.lastSeen, now)} tone={hubOnline ? 'green' : 'red'} />
            <Stat label="Up since" value={hub.bootAt ? uptime(Math.round((now - hub.bootAt) / 1000)) : '—'} />
            <Stat label="IP" value={hub.ip ?? '—'} />
            <Stat label="WiFi channel" value={hub.wifiChannel ?? '—'} />
            <Stat label="WiFi signal" value={hub.wifiRssi !== undefined ? `${hub.wifiRssi} dBm` : '—'} />
            <Stat
              label="Clock (DS3231)"
              value={
                hub.rtc === 'ok'
                  ? `RTC ok · ${hub.timeSource === 'ntp' ? 'internet' : hub.timeSource === 'rtc' ? 'RTC only' : '—'}`
                  : hub.rtc === 'lost-power'
                    ? 'RTC not set'
                    : hub.rtc === 'missing'
                      ? 'RTC missing'
                      : '—'
              }
              tone={hub.rtc === 'ok' ? 'green' : hub.rtc ? 'amber' : undefined}
            />
            <Stat label="Firmware" value={hub.fw ?? '—'} />
          </div>
        )}
      </Card>
    </>
  );
}
