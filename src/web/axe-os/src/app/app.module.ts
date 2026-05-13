import 'chartjs-adapter-moment';

import {CommonModule, HashLocationStrategy, LocationStrategy} from '@angular/common';
import {HttpClientModule} from '@angular/common/http';
import {NgModule, CSP_NONCE} from '@angular/core';
import {FormsModule, ReactiveFormsModule} from '@angular/forms';
import {BrowserModule} from '@angular/platform-browser';
import {BrowserAnimationsModule} from '@angular/platform-browser/animations';
import {ToastrModule} from 'ngx-toastr';

import {AppRoutingModule} from './app-routing.module';
import {AppComponent} from './app.component';
import {EditComponent} from './components/edit/edit.component';
import {HrDistChartComponent} from './components/hr-dist-chart/hr-dist-chart.component';
import {LuckyChartComponent} from './components/lucky-chart/lucky-chart.component';
import {NetworkEditComponent} from './components/network-edit/network.edit.component';
import {TimeEditComponent} from './components/time-edit/time.edit.component';
import {HomeComponent} from './components/home/home.component';
import {LoadingComponent} from './components/loading/loading.component';
import {LogsComponent} from './components/logs/logs.component';
import {MonitorComponent} from './components/monitor/monitor.component';
import {MonitorChartComponent} from './components/monitor-chart/monitor-chart.component';
import {CircularCountdownComponent} from './components/circular-countdown/circular-countdown.component';
import {BottomSheetSelectorComponent} from './components/bottom-sheet-selector/bottom-sheet-selector.component';
import {SettingsComponent} from './components/settings/settings.component';
import {PreferenceComponent} from "./components/preference/preference.component";
import {SwarmComponent} from './components/swarm/swarm.component';
import {ThemeConfigComponent} from './components/settings/theme-config.component';
import {UpdateComponent} from './components/update/update.component';
import {BenchmarkComponent} from './components/benchmark/benchmark.component';
import {AppLayoutModule} from './layout/app.layout.module';
import {ANSIPipe} from './pipes/ansi.pipe';
import {DateAgoPipe} from './pipes/date-ago.pipe';
import {HashSuffixPipe} from './pipes/hash-suffix.pipe';
import {ReplacePipe} from './pipes/replace.pipe';
import {PrimeNGModule} from './prime-ng.module';
import {MessageModule} from 'primeng/message';
import {TooltipModule} from 'primeng/tooltip';

const components = [
  AppComponent,
  EditComponent,
  HomeComponent,
  HrDistChartComponent,
  LuckyChartComponent,
  LoadingComponent,
  SettingsComponent,
  LogsComponent,
  MonitorComponent,
  MonitorChartComponent,
  CircularCountdownComponent,
  BottomSheetSelectorComponent,
  NetworkEditComponent,
  TimeEditComponent,
  PreferenceComponent,
  UpdateComponent,
  BenchmarkComponent
];

@NgModule({
  declarations: [
    ...components,

    ANSIPipe,
    DateAgoPipe,
    SwarmComponent,
    SettingsComponent,
    HashSuffixPipe,
    ReplacePipe,
    ThemeConfigComponent
  ],
  imports: [
    BrowserModule,
    AppRoutingModule,
    HttpClientModule,
    ReactiveFormsModule,
    FormsModule,
    ToastrModule.forRoot({
      positionClass: 'toast-bottom-right'
    }),
    BrowserAnimationsModule,
    CommonModule,
    PrimeNGModule,
    AppLayoutModule,
    MessageModule,
    TooltipModule,
  ],
  providers: [
    {provide: LocationStrategy, useClass: HashLocationStrategy},
    {provide: CSP_NONCE, useValue: ''},  // 添加 CSP nonce 支持
  ],
  bootstrap: [AppComponent]
})
export class AppModule {
}
