import { useEffect, useState } from 'react';
import { onValue, ref } from 'firebase/database';
import { db } from '../../lib/firebase';

export interface ValueResult<T> {
  /** undefined while the first snapshot is loading, null when the path is empty. */
  data: T | null | undefined;
  error: Error | null;
}

/** Live-subscribes to one RTDB path. */
export function useValue<T>(path: string | null): ValueResult<T> {
  const [result, setResult] = useState<ValueResult<T>>({ data: undefined, error: null });

  useEffect(() => {
    if (!path) return;
    setResult({ data: undefined, error: null });
    return onValue(
      ref(db, path),
      (snap) => setResult({ data: snap.exists() ? (snap.val() as T) : null, error: null }),
      (error) => setResult({ data: null, error }),
    );
  }, [path]);

  return result;
}
