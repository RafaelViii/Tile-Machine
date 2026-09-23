import type { ReactNode } from 'react';
import { Navigate } from 'react-router';
import { Button, Spinner } from '../../components/ui';
import { useAuth } from './auth';

export function RequireAdmin({ children }: { children: ReactNode }) {
  const { user, role, loading, logout } = useAuth();

  if (loading) return <Spinner />;
  if (!user) return <Navigate to="/login" replace />;
  if (role !== 'admin')
    return (
      <div className="flex min-h-screen flex-col items-center justify-center gap-4 px-4 text-center">
        <h1 className="text-xl font-bold">Not authorized</h1>
        <p className="max-w-md text-sm text-zinc-400">
          <span className="font-mono">{user.email}</span> is signed in but isn't an admin. Ask the owner to add
          <span className="font-mono"> roles/{user.uid} = "admin"</span> in Firebase.
        </p>
        <Button onClick={logout}>Sign out</Button>
      </div>
    );
  return <>{children}</>;
}
