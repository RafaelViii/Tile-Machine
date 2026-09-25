import { initializeApp } from 'firebase/app';
import {
  createUserWithEmailAndPassword,
  getAuth,
  inMemoryPersistence,
  initializeAuth,
  signInWithEmailAndPassword,
  signOut,
  updatePassword,
} from 'firebase/auth';
import { getDatabase } from 'firebase/database';

const env = import.meta.env;

const firebaseConfig = {
  apiKey: env.VITE_FIREBASE_API_KEY,
  authDomain: env.VITE_FIREBASE_AUTH_DOMAIN,
  databaseURL: env.VITE_FIREBASE_DATABASE_URL,
  projectId: env.VITE_FIREBASE_PROJECT_ID,
  storageBucket: env.VITE_FIREBASE_STORAGE_BUCKET,
  messagingSenderId: env.VITE_FIREBASE_MESSAGING_SENDER_ID,
  appId: env.VITE_FIREBASE_APP_ID,
};

const missing = Object.entries(firebaseConfig)
  .filter(([, v]) => !v)
  .map(([k]) => k);
if (missing.length) {
  throw new Error(`Firebase config missing (${missing.join(', ')}). Copy web/.env.example to web/.env.local.`);
}

export const app = initializeApp(firebaseConfig);
export const auth = getAuth(app);
export const db = getDatabase(app);

/**
 * Create an email/password account WITHOUT signing the current user out. createUserWithEmailAndPassword
 * signs in as the new account on the auth instance it runs on, so it runs on a second, in-memory-only
 * app instance: the superadmin's own session (and its saved login) is never touched.
 */
let helperAuth: ReturnType<typeof initializeAuth> | null = null;
export async function createAccountWithoutSwitching(email: string, password: string): Promise<string> {
  if (!helperAuth) {
    const helperApp = initializeApp(firebaseConfig, 'account-helper');
    helperAuth = initializeAuth(helperApp, { persistence: inMemoryPersistence });
  }
  const cred = await createUserWithEmailAndPassword(helperAuth, email, password);
  const uid = cred.user.uid;
  await signOut(helperAuth);
  return uid;
}

/**
 * The superadmin sets an operator's password: signs in AS the operator on the in-memory helper instance
 * (with the password the superadmin gave them), sets the new one and signs out again. The superadmin's own
 * session is never touched. Firebase lets only a server change someone else's password without the
 * current one, so the current password is needed (forgotten = add a new account, turn the old one off).
 */
export async function setAccountPasswordWithoutSwitching(email: string, current: string, next: string): Promise<void> {
  if (!helperAuth) {
    const helperApp = initializeApp(firebaseConfig, 'account-helper');
    helperAuth = initializeAuth(helperApp, { persistence: inMemoryPersistence });
  }
  const cred = await signInWithEmailAndPassword(helperAuth, email, current);
  try {
    await updatePassword(cred.user, next);
  } finally {
    await signOut(helperAuth);
  }
}
