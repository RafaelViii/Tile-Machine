import type { ReactNode } from 'react';
import { ago, uptime } from '../shared/format';
import { useMachine } from '../shared/machine';
import type { ModuleId } from '../shared/types/rtdb';
import { CommandButton } from './CommandButton';
import { Card, CardTitle, ConnectionBadge, PageHeader, Stat } from './ui';

/** Page header + device card (connection, info, STOP / IDENTIFY / REBOOT) shared by every module page. */
export function ModulePanel({
  id,
  title,
  subtitle,
  esp,
  children,
}: {
  id: ModuleId;
  title: string;
  subtitle: string;
  esp: string;
  children: ReactNode;
}) {
  const { hubOnline, now, modules } = useMachine();
  const { node, connected } = modules[id];
  const offlineReason = hubOnline ? 'Module not connected' : 'Hub offline';
  const state = node?.state;

  return (
    <>
      <PageHeader title={title} subtitle={subtitle} right={<ConnectionBadge connected={connected} hubOnline={hubOnline} />} />

      <Card className="mb-6">
        <CardTitle right={<span className="text-xs text-zinc-500">{esp}</span>}>Device</CardTitle>
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
          <Stat label="Last seen" value={ago(node?.presence?.lastSeen, now)} tone={connected ? 'green' : undefined} />
          <Stat label="Uptime" value={connected ? uptime(state?.uptimeS) : '—'} />
          <Stat label="MAC" value={node?.info?.mac ?? '—'} />
          <Stat label="Firmware" value={node?.info?.fw ?? '—'} />
        </div>
        {connected && state?.interlock && (
          <p className="mt-4 rounded-xl bg-amber-500/10 px-3 py-2 text-sm text-amber-300 ring-1 ring-amber-500/30">
            Power-up interlock: move the switch to OFF / the middle position once to start using this module.
          </p>
        )}
        <div className="mt-4 flex flex-wrap gap-3">
          <CommandButton moduleId={id} type="STOP" variant="danger" disabled={!connected} disabledReason={offlineReason}>
            STOP {title}
          </CommandButton>
          <CommandButton moduleId={id} type="IDENTIFY" disabled={!connected} disabledReason={offlineReason}>
            Identify
          </CommandButton>
          <CommandButton moduleId={id} type="REBOOT" variant="ghost" disabled={!connected} disabledReason={offlineReason}>
            Reboot
          </CommandButton>
        </div>
        <p className="mt-3 text-xs text-zinc-500">
          For safety the website can only stop things. Starting always needs a person at the machine.
        </p>
      </Card>

      {children}
    </>
  );
}
