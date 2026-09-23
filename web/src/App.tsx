import { BrowserRouter, Navigate, Route, Routes } from 'react-router';
import { Layout } from './components/Layout';
import { AuthProvider } from './features/auth/auth';
import { LoginPage } from './features/auth/LoginPage';
import { RequireAdmin } from './features/auth/RequireAdmin';
import { ContainingPage } from './features/containing/ContainingPage';
import { DashboardPage } from './features/dashboard/DashboardPage';
import { EventsPage } from './features/events/EventsPage';
import { HotpressPage } from './features/hotpress/HotpressPage';
import { ShredderPage } from './features/shredder/ShredderPage';
import { MachineProvider } from './shared/machine';

export function App() {
  return (
    <AuthProvider>
      <BrowserRouter>
        <Routes>
          <Route path="/login" element={<LoginPage />} />
          <Route
            element={
              <RequireAdmin>
                <MachineProvider>
                  <Layout />
                </MachineProvider>
              </RequireAdmin>
            }
          >
            <Route index element={<DashboardPage />} />
            <Route path="shredder" element={<ShredderPage />} />
            <Route path="containing" element={<ContainingPage />} />
            <Route path="hotpress" element={<HotpressPage />} />
            <Route path="events" element={<EventsPage />} />
          </Route>
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </BrowserRouter>
    </AuthProvider>
  );
}
