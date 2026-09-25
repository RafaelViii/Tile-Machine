import type { ReactNode } from 'react';
import { Navigate } from 'react-router';
import { Button, Spinner } from '../../components/ui';
import { useAuth } from './auth';

/** Superadmin and operators only. Reacts live: turning access off locks an open session at once. */
export function RequireStaff({ children }: { children: ReactNode }) {
  const { user, isStaff, loading, logout } = useAuth();

  if (loading) return <Spinner />;
  if (!user) return <Navigate to="/login" replace />;
  if (!isStaff)
    return (
      <div className="flex min-h-screen flex-col items-center justify-center gap-4 px-4 text-center">
        <h1 className="text-xl font-bold">No access</h1>
        <p className="max-w-md text-sm text-zinc-400">
          {user.email} is signed in but has no access to Tile Console, or access was turned off. Ask the owner to
          turn it on in Users.
        </p>
        <Button onClick={logout}>Sign out</Button>
      </div>
    );
  return <>{children}</>;
}

/** Only the superadmin (Users, Activity). Operators who open the URL go back to the Dashboard. */
export function RequireSuper({ children }: { children: ReactNode }) {
  const { isSuper } = useAuth();
  if (!isSuper) return <Navigate to="/" replace />;
  return <>{children}</>;
}
