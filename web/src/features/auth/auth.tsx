import { createContext, useCallback, useContext, useEffect, useState, type ReactNode } from 'react';
import {
  EmailAuthProvider,
  onAuthStateChanged,
  reauthenticateWithCredential,
  signOut,
  updatePassword,
  type User,
} from 'firebase/auth';
import { onValue, ref } from 'firebase/database';
import { auth, db } from '../../lib/firebase';
import { logAudit } from '../../shared/audit';
import { endPresence } from '../../shared/presence';
import type { Role } from '../../shared/types/rtdb';

interface AuthState {
  user: User | null;
  role: Role | null;
  /** Display name from /users/{uid} (falls back to the email). */
  name: string;
  isSuper: boolean;
  /** superadmin or operator: may use the web app. */
  isStaff: boolean;
  loading: boolean;
  logout: () => Promise<void>;
  changePassword: (current: string, next: string) => Promise<void>;
}

const AuthContext = createContext<AuthState | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<User | null>(null);
  const [userLoading, setUserLoading] = useState(true);
  // Role and name are tagged with the uid they were loaded for, so after an account switch the
  // previous account's role is never shown or used, not even for one render.
  const [roleOf, setRoleOf] = useState<{ uid: string; role: Role | null } | null>(null);
  const [nameOf, setNameOf] = useState<{ uid: string; name: string } | null>(null);

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
    // Live: if the superadmin turns this account's access off, the app locks within a second.
    const offRole = onValue(
      ref(db, `roles/${uid}`),
      (snap) => {
        const v = snap.val();
        setRoleOf({ uid, role: v === 'superadmin' || v === 'operator' || v === 'hub' ? v : null });
      },
      () => setRoleOf({ uid, role: null }),
    );
    const offName = onValue(
      ref(db, `users/${uid}/name`),
      (snap) => setNameOf({ uid, name: typeof snap.val() === 'string' ? snap.val() : '' }),
      () => setNameOf({ uid, name: '' }),
    );
    return () => {
      offRole();
      offName();
    };
  }, [user]);

  const roleReady = !!user && roleOf?.uid === user.uid;
  const role = roleReady ? roleOf!.role : null;
  const isStaff = role === 'superadmin' || role === 'operator';

  const logout = useCallback(async () => {
    // Log + go offline while still signed in: after signOut the rules refuse both writes.
    if (isStaff) await logAudit('SIGN_OUT', { summary: 'Signed out' }).catch(() => {});
    await endPresence();
    await signOut(auth);
  }, [isStaff]);

  const changePassword = useCallback(async (current: string, next: string) => {
    const u = auth.currentUser;
    if (!u?.email) throw new Error('Not signed in');
    if (role !== 'superadmin') throw new Error('Only the superadmin can change passwords');
    await reauthenticateWithCredential(u, EmailAuthProvider.credential(u.email, current));
    await updatePassword(u, next);
    await logAudit('PASSWORD_CHANGE', { summary: 'Changed own password' }).catch(() => {});
  }, [role]);

  const value: AuthState = {
    user,
    role,
    name: (user && nameOf?.uid === user.uid && nameOf.name) || user?.email || '',
    isSuper: role === 'superadmin',
    isStaff,
    loading: userLoading || (!!user && !roleReady),
    logout,
    changePassword,
  };
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used inside <AuthProvider>');
  return ctx;
}
