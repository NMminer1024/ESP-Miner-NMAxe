import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-time-edit',
  templateUrl: './time.edit.component.html',
  styleUrls: ['./time.edit.component.scss']
})
export class TimeEditComponent implements OnInit {

  public form!: FormGroup;
  private originalFormValues: any = {};

  @Input() uri = '';

  public timeFormatOptions = [
    { label: '24h', value: 24 },
    { label: '12h', value: 12 },
  ];

  public dateFormatOptions = [
    { label: 'DD-MM-YYYY', value: 'DD-MM-YYYY' },
    { label: 'MM-DD-YYYY', value: 'MM-DD-YYYY' },
    { label: 'YYYY-MM-DD', value: 'YYYY-MM-DD' },
    { label: 'YYYY-DD-MM', value: 'YYYY-DD-MM' },
    { label: 'DD/MM/YYYY', value: 'DD/MM/YYYY' },
    { label: 'MM/DD/YYYY', value: 'MM/DD/YYYY' },
    { label: 'YYYY/MM/DD', value: 'YYYY/MM/DD' },
    { label: 'YYYY/DD/MM', value: 'YYYY/DD/MM' },
  ];

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {}

  ngOnInit(): void {
    this.systemService.getSettingTime(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        const timezone   = info.timeZone   ?? 8;
        const timeFormat = info.timeFormat  ?? 24;
        const dateFormat = info.dateFormat  || 'YYYY/MM/DD';

        this.form = this.fb.group({
          timezone:   [timezone,   [Validators.required, Validators.min(-12), Validators.max(14)]],
          timeFormat: [Number(timeFormat), [Validators.required]],
          dateFormat: [dateFormat, [Validators.required]],
        });

        this.originalFormValues = {
          timezone,
          timeFormat: Number(timeFormat),
          dateFormat,
        };

        this.form.valueChanges.subscribe(() => {
          if (!this.form.dirty) this.form.markAsDirty();
        });
      });
  }

  hasFormChanged(): boolean {
    if (!this.form || !this.originalFormValues) return false;
    const cur = this.form.getRawValue();
    return cur.timezone   !== this.originalFormValues.timezone   ||
           cur.timeFormat !== this.originalFormValues.timeFormat ||
           cur.dateFormat !== this.originalFormValues.dateFormat;
  }

  getButtonTooltip(): string {
    if (!this.form)        return 'Loading...';
    if (this.form.invalid) return 'Please fix form validation errors before saving';
    if (!this.hasFormChanged()) return 'No changes detected. Modify any field to enable saving';
    return 'Click to save changes';
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    this.systemService.patchSettingTime(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Saved.');
          this.originalFormValues = { ...form };
          this.form.markAsPristine();
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }
}
