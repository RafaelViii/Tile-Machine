import type { ReactNode } from 'react';
import { useCommand } from '../shared/hooks/useCommand';
import type { CommandType, ModuleId } from '../shared/types/rtdb';
import { Button } from './ui';

const statusText: Record<string, string> = {
  sending: 'Sending…',
  pending: 'Waiting for hub…',
  sent: 'Delivered to module…',
  done: 'Done ✓',
  failed: 'Failed',
  expired: 'Expired (hub too late)',
  error: 'Error',
};

/** Sends one command and shows its live status underneath. */
export function CommandButton({
  moduleId,
  type,
  target,
  arg,
  variant = 'subtle',
  disabled,
  disabledReason,
  children,
  className,
  floatingStatus = false,
}: {
  moduleId: ModuleId | 'all';
  type: CommandType;
  target?: number;
  arg?: number;
  variant?: 'primary' | 'danger' | 'ghost' | 'subtle';
  disabled?: boolean;
  disabledReason?: string;
  children: ReactNode;
  className?: string;
  /** Show the status as a label floating under the button (for tight spots like the header). */
  floatingStatus?: boolean;
}) {
  const { status, error, send } = useCommand(moduleId);
  return (
    <div className={floatingStatus ? 'relative' : 'flex flex-col items-start gap-1'}>
      <Button
        variant={variant}
        disabled={disabled || status === 'sending'}
        title={disabled ? disabledReason : undefined}
        onClick={() => send(type, { target, arg })}
        className={className}
      >
        {children}
      </Button>
      {status && (
        <span
          className={[
            'font-mono text-[11px]',
            floatingStatus && 'absolute top-full right-0 mt-2 rounded-sm bg-zinc-900 px-1.5 py-0.5 whitespace-nowrap ring-1 ring-zinc-700',
            status === 'done'
              ? 'text-emerald-400'
              : status === 'failed' || status === 'expired' || status === 'error'
                ? 'text-red-400'
                : 'text-zinc-400',
          ]
            .filter(Boolean)
            .join(' ')}
        >
          {statusText[status]}
          {error ? `: ${error}` : ''}
        </span>
      )}
    </div>
  );
}
