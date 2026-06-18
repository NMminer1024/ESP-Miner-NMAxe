import {NgModule} from '@angular/core';
import {RadioButtonModule} from 'primeng/radiobutton';
import {ButtonModule} from 'primeng/button';
import {InputSwitchModule} from 'primeng/inputswitch';
import {ChartModule} from 'primeng/chart';
import {CheckboxModule} from 'primeng/checkbox';
import {DialogModule} from 'primeng/dialog';
import {DropdownModule} from 'primeng/dropdown';
import {FileUploadModule} from 'primeng/fileupload';
import {InputTextModule} from 'primeng/inputtext';
import {MultiSelectModule} from 'primeng/multiselect';
import {SliderModule} from 'primeng/slider';

const primeNgModules = [
  InputSwitchModule,
  InputTextModule,
  CheckboxModule,
  DialogModule,
  DropdownModule,
  MultiSelectModule,
  SliderModule,
  ButtonModule,
  FileUploadModule,
  ChartModule,
  RadioButtonModule
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
