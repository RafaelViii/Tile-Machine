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
}) {
  const { status, error, send } = useCommand(moduleId);
  return (
    <div className="flex flex-col items-start gap-1">
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
          className={
            status === 'done'
              ? 'text-[11px] text-emerald-400'
              : status === 'failed' || status === 'expired' || status === 'error'
                ? 'text-[11px] text-red-400'
                : 'text-[11px] text-zinc-400'
          }
        >
          {statusText[status]}
          {error ? `: ${error}` : ''}
        </span>
      )}
    </div>
  );
}
