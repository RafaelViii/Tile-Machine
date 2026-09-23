import { createContext, useContext, type ReactNode } from 'react';
import type { HubNode, ModuleId, ModuleNode } from './types/rtdb';
import { useServerNow } from './hooks/useServerNow';
import { useValue } from './hooks/useValue';

/** Hub counts as offline if it hasn't refreshed lastSeen in this window (hub writes every 10 s). */
export const HUB_STALE_MS = 25_000;

export interface ModuleStatus<M extends ModuleId> {
  node: ModuleNode<M> | null | undefined;
  /** Connected = hub online AND hub reports the module online. */
  connected: boolean;
}

export interface MachineStatus {
  hub: HubNode | null | undefined;
  hubOnline: boolean;
  loading: boolean;
  now: number;
  modules: { [M in ModuleId]: ModuleStatus<M> };
}

const MachineContext = createContext<MachineStatus | null>(null);

function useModuleNode<M extends ModuleId>(id: M, hubOnline: boolean): ModuleStatus<M> {
  const { data: node } = useValue<ModuleNode<M>>(`modules/${id}`);
  return { node, connected: hubOnline && !!node?.presence?.online };
}

export function MachineProvider({ children }: { children: ReactNode }) {
  const { data: hub } = useValue<HubNode>('hub');
  const now = useServerNow();
  const hubOnline =
    !!hub?.online && typeof hub.lastSeen === 'number' && now - hub.lastSeen < HUB_STALE_MS;

  const shredder = useModuleNode('shredder', hubOnline);
  const containing = useModuleNode('containing', hubOnline);
  const hotpress = useModuleNode('hotpress', hubOnline);

  const value: MachineStatus = {
    hub,
    hubOnline,
    loading: hub === undefined,
    now,
    modules: { shredder, containing, hotpress },
  };
  return <MachineContext.Provider value={value}>{children}</MachineContext.Provider>;
}

export function useMachine(): MachineStatus {
  const ctx = useContext(MachineContext);
  if (!ctx) throw new Error('useMachine must be used inside <MachineProvider>');
  return ctx;
}
