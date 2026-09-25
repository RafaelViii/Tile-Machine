// Online presence at /presence/{uid} (read only by the superadmin).
//  - connections/{id}: one per open tab, removed by the SERVER when that tab disconnects (onDisconnect),
//    so a closed laptop lid or lost WiFi shows as offline without the page doing anything.
//  - lastSeen: refreshed every minute while online and on disconnect. A connection whose lastSeen is
//    older than PRESENCE_STALE_MS is treated as gone (covers the rare case the server missed it).
// Sign-out must call endPresence() BEFORE signing out: afterwards the rules refuse the cleanup write.
import {
  onDisconnect,
  onValue,
  push,
  ref,
  remove,
  serverTimestamp,
  set,
  update,
  type DatabaseReference,
} from 'firebase/database';
import { db } from '../lib/firebase';

export const PRESENCE_HEARTBEAT_MS = 60_000;
export const PRESENCE_STALE_MS = 150_000;

let active: {
  uid: string;
  conn: DatabaseReference | null;
  page: string;
  unsubConnected: () => void;
  timer: ReturnType<typeof setInterval>;
} | null = null;

/** "Edge on Windows", "Safari on iPhone"… good enough to tell devices apart. */
export function deviceName(ua = navigator.userAgent): string {
  const browser = /Edg\//.test(ua)
    ? 'Edge'
    : /OPR\//.test(ua)
      ? 'Opera'
      : /Chrome\//.test(ua)
        ? 'Chrome'
        : /Firefox\//.test(ua)
          ? 'Firefox'
          : /Safari\//.test(ua)
            ? 'Safari'
            : 'Browser';
  const os = /iPhone/.test(ua)
    ? 'iPhone'
    : /iPad/.test(ua)
      ? 'iPad'
      : /Android/.test(ua)
        ? 'Android'
        : /Windows/.test(ua)
          ? 'Windows'
          : /Mac OS X/.test(ua)
            ? 'Mac'
            : /Linux/.test(ua)
              ? 'Linux'
              : 'unknown device';
  return `${browser} on ${os}`;
}

export function startPresence(uid: string, page: string): void {
  if (active?.uid === uid) return setPresencePage(page);
  if (active) void endPresence();
  const base = `presence/${uid}`;
  const state = {
    uid,
    conn: null as DatabaseReference | null,
    page,
    unsubConnected: () => {},
    timer: setInterval(() => {
      if (state.conn) set(ref(db, `${base}/lastSeen`), serverTimestamp()).catch(() => {});
    }, PRESENCE_HEARTBEAT_MS),
  };
  active = state;
  // Fires on every (re)connection of this tab: register a fresh connection entry each time.
  state.unsubConnected = onValue(ref(db, '.info/connected'), async (snap) => {
    if (snap.val() !== true || active !== state) return;
    try {
      const conn = push(ref(db, `${base}/connections`));
      await onDisconnect(conn).remove();
      await onDisconnect(ref(db, `${base}/lastSeen`)).set(serverTimestamp());
      await set(conn, { device: deviceName(), page: state.page, since: serverTimestamp() });
      await set(ref(db, `${base}/lastSeen`), serverTimestamp());
      state.conn = conn;
    } catch {
      /* access removed meanwhile: nothing to show */
    }
  });
}

export function setPresencePage(page: string): void {
  if (!active) return;
  active.page = page;
  if (active.conn) update(active.conn, { page }).catch(() => {});
}

/** Mark this tab offline now (call before signOut, while the write is still allowed). */
export async function endPresence(): Promise<void> {
  const s = active;
  if (!s) return;
  active = null;
  clearInterval(s.timer);
  s.unsubConnected();
  const base = `presence/${s.uid}`;
  try {
    if (s.conn) {
      await onDisconnect(s.conn).cancel();
      await remove(s.conn);
    }
    await onDisconnect(ref(db, `${base}/lastSeen`)).cancel();
    await set(ref(db, `${base}/lastSeen`), serverTimestamp());
  } catch {
    /* already signed out or access removed */
  }
}
