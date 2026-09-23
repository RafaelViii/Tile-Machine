import { createContext, useContext, useEffect, useState, type ReactNode } from 'react';
import { onAuthStateChanged, signOut, type User } from 'firebase/auth';
import { onValue, ref } from 'firebase/database';
import { auth, db } from '../../lib/firebase';

export type Role = 'admin' | 'hub' | null;

interface AuthState {
  user: User | null;
  role: Role;
  loading: boolean;
  logout: () => Promise<void>;
}

const AuthContext = createContext<AuthState | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<User | null>(null);
  const [userLoading, setUserLoading] = useState(true);
  // Role is tagged with the uid it was loaded for, so a fresh login never shows a stale role.
  const [roleOf, setRoleOf] = useState<{ uid: string; role: Role } | null>(null);

  useEffect(
    () =>
      onAuthStateChanged(auth, (u) => {
        setUser(u);
        setUserLoading(false);
      }),
    [],
  );

  useEffect(() => {
    if (!user) return;
    const uid = user.uid;
    return onValue(
      ref(db, `roles/${uid}`),
      (snap) => {
        const v = snap.val();
        setRoleOf({ uid, role: v === 'admin' || v === 'hub' ? v : null });
      },
      () => setRoleOf({ uid, role: null }),
    );
  }, [user]);

  const roleReady = !!user && roleOf?.uid === user.uid;
  const value: AuthState = {
    user,
    role: roleReady ? roleOf!.role : null,
    loading: userLoading || (!!user && !roleReady),
    logout: () => signOut(auth),
  };
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used inside <AuthProvider>');
  return ctx;
}
