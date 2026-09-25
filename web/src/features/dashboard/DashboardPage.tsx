import { Fragment, type ComponentType, type SVGProps } from 'react';
import { Link } from 'react-router';
import { ArrowRight, ContainerIcon, CuringIcon, DesignIcon, HotpressIcon, ShredderIcon } from '../../components/icons';
import { Badge, Card, CardTitle, Lamp, PageHeader, cx } from '../../components/ui';
import { humanize, kg } from '../../shared/format';
import { useMachine, type MachineStatus } from '../../shared/machine';
import type { ModuleId } from '../../shared/types/rtdb';
import { DeviceCards } from './DeviceCards';

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

function ProcessTile({ p, m, step }: { p: Process; m: MachineStatus; step: number }) {
  const connected = m.modules[p.device].connected;
  const summary = connected ? p.summary(m) : null;
  return (
    <Link
      to={p.to}
      className={cx(
        'group relative flex min-h-40 flex-col lg:min-h-44 lg:flex-1 lg:basis-40 items-center justify-center gap-2 overflow-hidden rounded-lg border p-4 pt-7 text-center transition',
        connected
          ? 'tile-connected border-emerald-500/60 bg-emerald-500/[0.08]'
          : 'border-zinc-800 bg-zinc-900/60 opacity-60 grayscale hover:opacity-80',
      )}
    >
      {/* station plate: step number + ESP, lamp on the right */}
      <div className="absolute inset-x-0 top-0 flex items-center justify-between border-b border-zinc-800 bg-zinc-950/60 px-2.5 py-1">
        <span className="font-display text-[12px] font-semibold tracking-[0.14em] text-zinc-500 uppercase">
          Step {step} · {p.esp}
        </span>
        <Lamp state={connected ? 'on' : 'off'} />
      </div>
      <p.Icon className={cx('mt-1 h-10 w-10', connected ? 'text-emerald-300' : 'text-zinc-500')} />
      <div className="font-display text-lg leading-tight font-bold tracking-wide uppercase">{p.name}</div>
      <div
        className={cx(
          'font-display text-[13px] font-semibold tracking-wider uppercase',
          connected ? 'text-emerald-300' : 'text-zinc-500',
        )}
      >
        {connected ? 'Connected' : 'Not connected'}
      </div>
      {summary && <div className="font-mono text-[11px] text-zinc-400">{summary}</div>}
    </Link>
  );
}

export function DashboardPage() {
  const m = useMachine();
  const connectedCount = (['shredder', 'containing', 'hotpress'] as const).filter((id) => m.modules[id].connected).length;

  return (
    <>
      <PageHeader
        title="Dashboard"
        subtitle="Modules light up automatically as soon as they pair with the hub."
        right={<Badge tone={connectedCount === 3 ? 'green' : 'zinc'}>{connectedCount} / 3 modules connected</Badge>}
      />

      <Card className="mb-8">
        <CardTitle>Process line</CardTitle>
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 lg:flex lg:flex-wrap lg:items-stretch">
          {processes.map((p, i) => (
            <Fragment key={p.name}>
              {i > 0 && <ArrowRight className="hidden h-5 w-5 shrink-0 self-center text-zinc-600 lg:block" />}
              <ProcessTile p={p} m={m} step={i + 1} />
            </Fragment>
          ))}
        </div>
      </Card>

      <DeviceCards />
    </>
  );
}
