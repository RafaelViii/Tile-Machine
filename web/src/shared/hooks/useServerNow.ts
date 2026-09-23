import { useEffect, useState } from 'react';
import { onValue, ref } from 'firebase/database';
import { db } from '../../lib/firebase';

/** Server-corrected "now" in ms, re-rendering every `tickMs`. */
export function useServerNow(tickMs = 1000): number {
  const [offset, setOffset] = useState(0);
  const [now, setNow] = useState(() => Date.now());

  useEffect(
    () => onValue(ref(db, '.info/serverTimeOffset'), (snap) => setOffset(Number(snap.val()) || 0)),
    [],
  );

  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), tickMs);
    return () => clearInterval(id);
  }, [tickMs]);

  return now + offset;
}
