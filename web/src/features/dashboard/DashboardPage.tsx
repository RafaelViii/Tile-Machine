import { Fragment, type ComponentType, type SVGProps } from 'react';
import { Link } from 'react-router';
import { ArrowRight, ContainerIcon, CuringIcon, DesignIcon, HotpressIcon, ShredderIcon } from '../../components/icons';
import { Badge, Card, CardTitle, PageHeader, cx } from '../../components/ui';
import { humanize, kg } from '../../shared/format';
import { useMachine, type MachineStatus } from '../../shared/machine';
import type { ModuleId } from '../../shared/types/rtdb';
import { DeviceCards } from './DeviceCards';
import { PresetsCard } from './PresetsCard';

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
        'group relative flex min-h-36 flex-col lg:min-h-40 lg:flex-1 lg:basis-40 items-center justify-center gap-2 rounded-xl border p-4 text-center transition',
        connected
          ? 'tile-connected border-emerald-500/40 bg-emerald-500/[0.04]'
          : 'tile-off border-zinc-800 bg-zinc-900/40',
      )}
    >
      <span className="absolute top-3 left-3 text-xs text-zinc-500">{p.esp}</span>
      <p.Icon className={cx('h-10 w-10', connected ? 'text-emerald-300' : 'text-zinc-500')} />
      <div className="font-semibold">{p.name}</div>
      <div className={cx('text-xs font-medium', connected ? 'text-emerald-300' : 'text-zinc-500')}>
        {connected ? 'Connected' : 'Not connected'}
      </div>
      {summary && <div className="text-xs text-zinc-400 tabular-nums">{summary}</div>}
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

      <Card className="mb-6">
        <CardTitle>Process line</CardTitle>
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 lg:flex lg:flex-wrap lg:items-stretch">
          {processes.map((p, i) => (
            <Fragment key={p.name}>
              {i > 0 && <ArrowRight className="hidden h-5 w-5 shrink-0 self-center text-zinc-600 lg:block" />}
              <ProcessTile p={p} m={m} />
            </Fragment>
          ))}
        </div>
      </Card>

      <DeviceCards />

      <PresetsCard />
    </>
  );
}
