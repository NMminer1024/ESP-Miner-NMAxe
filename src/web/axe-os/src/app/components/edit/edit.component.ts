import {HttpErrorResponse} from '@angular/common/http';
import {Component, Input, OnInit} from '@angular/core';
import {FormBuilder, FormGroup, Validators} from '@angular/forms';
import {ToastrService} from 'ngx-toastr';
import {startWith} from 'rxjs';
import {LoadingService} from 'src/app/services/loading.service';
import {SystemService} from 'src/app/services/system.service';
import {eASICModel} from 'src/models/enum/eASICModel';

@Component({
  selector: 'app-edit',
  templateUrl: './edit.component.html',
  styleUrls: ['./edit.component.scss']
})
export class EditComponent implements OnInit {

  public form!: FormGroup;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;


  public devToolsOpen: boolean = false;
  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;

  @Input() uri = '';

  public DropdownFrequency: Array<{name: string, value: number}> = [];
  public CoreVoltage: Array<{name: string, value: number}> = [];

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService
  ) {

    window.addEventListener('resize', this.checkDevTools);
    this.checkDevTools();

  }

  ngOnInit(): void {
    // First, get mining settings to populate dropdown options
    this.systemService.getMiningSettings(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(settings => {
        // Populate dropdown options from API
        this.DropdownFrequency = settings.overclock.options;
        this.CoreVoltage = settings.vcore.options;
      });

    // Then get system info to populate form values
    this.systemService.getInfo(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        // Map new field names to legacy names for backward compatibility
        info.ASICModel = info.asic || info.ASICModel;
        
        // Parse new stratum nested structure
        if (info.stratum) {
          info.stratumURL1 = info.stratum.primary?.url || info.primaryUrl || info.stratumURL1 || '';
          info.stratumURL2 = info.stratum.fallback?.url || info.fallBackUrl || info.stratumURL2 || '';
          info.stratumUser1 = info.stratum.primary?.user || info.primaryUser || info.stratumUser1 || '';
          info.stratumUser2 = info.stratum.fallback?.user || info.fallBackUser || info.stratumUser2 || '';
          info.stratumPassword1 = info.stratum.primary?.pwd || info.primaryPassword || info.stratumPassword1 || '';
          info.stratumPassword2 = info.stratum.fallback?.pwd || info.fallBackPassword || info.stratumPassword2 || '';
        } else {
          // Fallback to old flat structure
          info.stratumURL1 = info.primaryUrl || info.stratumURL1 || '';
          info.stratumURL2 = info.fallBackUrl || info.stratumURL2 || '';
          info.stratumUser1 = info.primaryUser || info.stratumUser1 || '';
          info.stratumUser2 = info.fallBackUser || info.stratumUser2 || '';
          info.stratumPassword1 = info.primaryPassword || info.stratumPassword1 || '';
          info.stratumPassword2 = info.fallBackPassword || info.stratumPassword2 || '';
        }
        
        info.coreVoltage = info.vcoreReq || info.coreVoltage;
        info.frequency = info.freqReq || info.frequency;
        
        this.ASICModel = info.ASICModel;
        this.form = this.fb.group({
          stratumURL1: [info.stratumURL1 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],

          stratumURL2: [info.stratumURL2 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],
          
          stratumUser1: [info.stratumUser1, [Validators.required]],
          stratumUser2: [info.stratumUser2, [Validators.required]],
          stratumPassword1: [info.stratumPassword1],
          stratumPassword2: [info.stratumPassword2],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          // overheat_mode: [info.overheat_mode, [Validators.required]]
        });

        // Remove the autofanspeed/fanspeed logic as these fields don't exist in this form
        // This logic belongs to the preference component, not the mining settings
      });
  }


  private checkDevTools = () => {
    if (
      window.outerWidth - window.innerWidth > 160 ||
      window.outerHeight - window.innerHeight > 160
    ) {
      this.devToolsOpen = true;
    } else {
      this.devToolsOpen = false;
    }
  };

  public updateSystem() {

    const formValue = this.form.getRawValue();

    // Transform flat form data to nested stratum structure
    const form = {
      stratum: {
        primary: {
          url: formValue.stratumURL1,
          user: formValue.stratumUser1,
          pwd: formValue.stratumPassword1 || 'x'
        },
        fallback: {
          url: formValue.stratumURL2,
          user: formValue.stratumUser2,
          pwd: formValue.stratumPassword2 || 'x'
        }
      },
      asicVcoreReq: formValue.coreVoltage,
      asicFreqReq: formValue.frequency
    };

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }

}
