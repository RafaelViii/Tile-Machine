import { BrowserRouter, Navigate, Route, Routes } from 'react-router';
import { Layout } from './components/Layout';
import { ActivityPage } from './features/activity/ActivityPage';
import { AuthProvider, useAuth } from './features/auth/auth';
import { LoginPage } from './features/auth/LoginPage';
import { RequireStaff, RequireSuper } from './features/auth/RequireStaff';
import { ContainingPage } from './features/containing/ContainingPage';
import { DashboardPage } from './features/dashboard/DashboardPage';
import { EventsPage } from './features/events/EventsPage';
import { HotpressPage } from './features/hotpress/HotpressPage';
import { ShredderPage } from './features/shredder/ShredderPage';
import { UsersPage } from './features/users/UsersPage';
import { ConfigDraftsProvider } from './shared/configDrafts';
import { MachineProvider } from './shared/machine';

/**
 * Everything below is keyed by the signed-in uid: when the account changes (sign out and in as someone
 * else, or another tab switches accounts) the whole app is rebuilt, so no unsaved edits, picked preset,
 * page state or open listener carries over from the previous person.
 */
function SignedInApp() {
  const { user } = useAuth();
  return (
    <MachineProvider key={user!.uid}>
      <ConfigDraftsProvider>
        <Layout />
      </ConfigDraftsProvider>
    </MachineProvider>
  );
}

export function App() {
  return (
    <AuthProvider>
      <BrowserRouter>
        <Routes>
          <Route path="/login" element={<LoginPage />} />
          <Route
            element={
              <RequireStaff>
                <SignedInApp />
              </RequireStaff>
            }
          >
            <Route index element={<DashboardPage />} />
            <Route path="shredder" element={<ShredderPage />} />
            <Route path="containing" element={<ContainingPage />} />
            <Route path="hotpress" element={<HotpressPage />} />
            <Route path="events" element={<EventsPage />} />
            <Route
              path="users"
              element={
                <RequireSuper>
                  <UsersPage />
                </RequireSuper>
              }
            />
            <Route
              path="activity"
              element={
                <RequireSuper>
                  <ActivityPage />
                </RequireSuper>
              }
            />
          </Route>
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </BrowserRouter>
    </AuthProvider>
  );
}
