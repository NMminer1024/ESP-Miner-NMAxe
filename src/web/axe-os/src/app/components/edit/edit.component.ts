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
    // Single call to /api/setting/mining returns OC/VC options + stratum config + current freq/vcore
    this.systemService.getSettingMining(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        // Populate dropdown options
        if (info.overclock?.options) this.DropdownFrequency = info.overclock.options;
        if (info.vcore?.options)     this.CoreVoltage       = info.vcore.options;

        // Map ASIC model
        this.ASICModel = info.asic || info.ASICModel;

        // Parse stratum URLs
        const stratumURL1 = info.stratum?.primary?.url  || '';
        const stratumURL2 = info.stratum?.fallback?.url || '';
        const stratumUser1 = info.stratum?.primary?.user  || '';
        const stratumUser2 = info.stratum?.fallback?.user || '';
        const stratumPwd1  = info.stratum?.primary?.pwd   || '';
        const stratumPwd2  = info.stratum?.fallback?.pwd  || '';

        this.form = this.fb.group({
          stratumURL1: [stratumURL1 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],
          stratumURL2: [stratumURL2 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl|tls):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],
          stratumUser1: [stratumUser1, [Validators.required]],
          stratumUser2: [stratumUser2, [Validators.required]],
          stratumPassword1: [stratumPwd1],
          stratumPassword2: [stratumPwd2],
          coreVoltage: [info.vcoreReq, [Validators.required]],
          frequency:   [info.freqReq,  [Validators.required]],
        });
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

    this.systemService.patchSettingMining(this.uri, form)
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
