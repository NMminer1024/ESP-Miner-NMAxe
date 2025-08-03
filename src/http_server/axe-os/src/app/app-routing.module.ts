import {NgModule} from '@angular/core';
import {RouterModule, Routes} from '@angular/router';

import {HomeComponent} from './components/home/home.component';
import {LogsComponent} from './components/logs/logs.component';
import {MonitorComponent} from './components/monitor/monitor.component';
import {SettingsComponent} from './components/settings/settings.component';
import {SwarmComponent} from './components/swarm/swarm.component';
import {UpdateComponent} from './components/update/update.component';
import {AppLayoutComponent} from './layout/app.layout.component';

const routes: Routes = [
  {
    path: '',
    component: AppLayoutComponent,
    children: [
      {
        path: '',
        component: HomeComponent
      },
      {
        path: 'logs',
        component: LogsComponent
      },
      {
        path: 'update',
        component: UpdateComponent
      },
      {
        path: 'settings',
        component: SettingsComponent
      },
      {
        path: 'swarm',
        component: SwarmComponent
      }
    ]
  },

];

@NgModule({
  imports: [RouterModule.forRoot(routes)],
  exports: [RouterModule]
})
export class AppRoutingModule {
}
