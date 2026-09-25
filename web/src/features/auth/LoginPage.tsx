import { useState, type FormEvent } from 'react';
import { LogoMark } from '../../components/icons';
import { Navigate } from 'react-router';
import { sendPasswordResetEmail, signInWithEmailAndPassword } from 'firebase/auth';
import { auth } from '../../lib/firebase';
import { Button, Spinner } from '../../components/ui';
import { logAudit } from '../../shared/audit';
import { deviceName } from '../../shared/presence';
import { useAuth } from './auth';

function friendly(code: string): string {
  if (code.includes('invalid-credential') || code.includes('wrong-password') || code.includes('user-not-found'))
    return 'Wrong email or password.';
  if (code.includes('too-many-requests')) return 'Too many attempts. Wait a minute and try again.';
  if (code.includes('network')) return 'No internet connection.';
  return code;
}

export function LoginPage() {
  const { user, loading } = useAuth();
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ tone: 'error' | 'ok'; text: string } | null>(null);

  if (loading) return <Spinner />;
  if (user) return <Navigate to="/" replace />;

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setMsg(null);
    try {
      await signInWithEmailAndPassword(auth, email.trim(), password);
      // Activity log (refused by the rules for accounts without staff access: nothing to log then).
      await logAudit('SIGN_IN', { summary: `Signed in on ${deviceName()}` }).catch(() => {});
    } catch (err) {
      setMsg({ tone: 'error', text: friendly(err instanceof Error ? err.message : String(err)) });
    } finally {
      setBusy(false);
    }
  };

  const reset = async () => {
    if (!email.trim()) {
      setMsg({ tone: 'error', text: 'Type your email first, then click "Forgot password".' });
      return;
    }
    try {
      await sendPasswordResetEmail(auth, email.trim());
      setMsg({ tone: 'ok', text: 'Password reset email sent. Check your inbox (and spam).' });
    } catch (err) {
      setMsg({ tone: 'error', text: friendly(err instanceof Error ? err.message : String(err)) });
    }
  };

  return (
    <div className="flex min-h-screen items-center justify-center px-4">
      <form onSubmit={submit} className="surface w-full max-w-sm rounded-2xl border p-7">
        <div className="mb-6 flex items-center gap-3">
          <LogoMark raised className="h-12 w-12" />
          <div>
            <h1 className="text-xl font-bold">Tile Console</h1>
            <p className="text-xs text-zinc-400">Control panel for the Tile Machine</p>
          </div>
        </div>

        <label className="block text-xs font-medium text-zinc-400">
          Email
          <input
            type="email"
            autoComplete="username"
            required
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            className="mt-1 w-full rounded-lg bg-zinc-950 px-3 py-2.5 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500"
          />
        </label>
        <label className="mt-4 block text-xs font-medium text-zinc-400">
          Password
          <input
            type="password"
            autoComplete="current-password"
            required
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            className="mt-1 w-full rounded-lg bg-zinc-950 px-3 py-2.5 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500"
          />
        </label>

        {msg && (
          <p className={msg.tone === 'error' ? 'mt-4 text-sm text-red-400' : 'mt-4 text-sm text-emerald-400'}>
            {msg.text}
          </p>
        )}

        <Button type="submit" variant="primary" disabled={busy} className="mt-6 w-full py-2.5">
          {busy ? 'Signing in…' : 'Sign in'}
        </Button>
        <button type="button" onClick={reset} className="mt-3 w-full text-center text-xs text-zinc-500 hover:text-zinc-300">
          Forgot password?
        </button>
      </form>
    </div>
  );
}
