import {NgModule} from '@angular/core';
import {RadioButtonModule} from 'primeng/radiobutton';
import {ButtonModule} from 'primeng/button';
import {InputSwitchModule} from 'primeng/inputswitch';
import {ChartModule} from 'primeng/chart';
import {CheckboxModule} from 'primeng/checkbox';
import {DialogModule} from 'primeng/dialog';
import {DropdownModule} from 'primeng/dropdown';
import {FileUploadModule} from 'primeng/fileupload';
import {InputGroupModule} from 'primeng/inputgroup';
import {InputGroupAddonModule} from 'primeng/inputgroupaddon';
import {InputTextModule} from 'primeng/inputtext';
import {KnobModule} from 'primeng/knob';
import {MultiSelectModule} from 'primeng/multiselect';
import {ProgressBarModule} from 'primeng/progressbar';
import {SidebarModule} from 'primeng/sidebar';
import {SliderModule} from 'primeng/slider';
import {TableModule} from 'primeng/table';

const primeNgModules = [
  InputSwitchModule,
  SidebarModule,
  InputTextModule,
  CheckboxModule,
  DialogModule,
  DropdownModule,
  MultiSelectModule,
  SliderModule,
  ButtonModule,
  FileUploadModule,
  KnobModule,
  ChartModule,
  InputGroupModule,
  InputGroupAddonModule,
  RadioButtonModule,
  ProgressBarModule,
  TableModule
];

@NgModule({
  imports: [
    ...primeNgModules
  ],
  exports: [
    ...primeNgModules
  ],
})
export class PrimeNGModule {
}
