export function ago(ts: number | undefined, now: number): string {
  if (!ts) return 'never';
  const s = Math.max(0, Math.round((now - ts) / 1000));
  if (s < 5) return 'just now';
  if (s < 60) return `${s}s ago`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m} min ago`;
  const h = Math.floor(m / 60);
  if (h < 48) return `${h} h ago`;
  return new Date(ts).toLocaleDateString();
}

export function uptime(sec: number | undefined): string {
  if (sec === undefined) return '—';
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  return h ? `${h}h ${m}m` : m ? `${m}m ${s}s` : `${s}s`;
}

export function kg(grams: number | undefined): string {
  if (grams === undefined) return '—';
  return `${(grams / 1000).toFixed(2)} kg`;
}

export function secs(ms: number | undefined): string {
  if (ms === undefined) return '—';
  return `${(ms / 1000).toFixed(ms % 1000 ? 1 : 0)} s`;
}

/** "AUTO_RUNNING" → "Auto running" */
export function humanize(s: string | undefined): string {
  if (!s) return '—';
  const t = s.replace(/_/g, ' ').toLowerCase();
  return t.charAt(0).toUpperCase() + t.slice(1);
}

export function dateTime(ts: number): string {
  return new Date(ts).toLocaleString();
}
