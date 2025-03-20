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

  public BM1366DropdownFrequency = [
    {name: '400', value: 400},
    {name: '425', value: 425},
    // {name: '450', value: 450},
    {name: '475', value: 475},
    {name: '485 (default)', value: 485},
    {name: '500', value: 500},
    {name: '525', value: 525},
    {name: '550', value: 550},
    {name: '575', value: 575},
  ];
  public BM1366CoreVoltage = [
    {name: '1100', value: 1100},
    {name: '1150', value: 1150},
    {name: '1200 (default)', value: 1200},
    {name: '1250', value: 1250},
    {name: '1300', value: 1300},
  ];


  public BM1370DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '440', value: 440 },
    { name: '490', value: 490 },
    { name: '550 (default)', value: 550 },
    { name: '575', value: 575 },
    { name: '600', value: 600 },
    { name: '625', value: 625 },
  ];
  public BM1370CoreVoltage = [
    { name: '1000', value: 1000 },
    { name: '1060', value: 1060 },
    { name: '1100', value: 1100 },
    { name: '1150 (default)', value: 1150 },
    { name: '1200', value: 1200 },
    { name: '1250', value: 1250 },
  ];


  public Coin = [
    {name: 'BTC', value: 'BTC'},
    {name: 'BCH', value: 'BCH'},
    {name: 'BSV', value: 'BSV'},
    {name: 'DGB', value: 'DGB'},
    {name: 'FB', value: 'FB'},
    {name: 'SPACE', value: 'SPACE'},
    {name: 'XEC', value: 'XEC'},
  ];



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
    this.systemService.getInfo(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.ASICModel = info.ASICModel;
        this.form = this.fb.group({
          stratumURL1: [info.stratumURL1 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],

          stratumURL2: [info.stratumURL2 || 'stratum+tcp://', [
            Validators.required,
            Validators.pattern(/^(stratum\+(tcp|ssl):\/\/[a-zA-Z0-9.-]+:(6553[0-5]|655[0-2][0-9]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[0-9]{1,4}))$/)
          ]],
          
          stratumUser1: [info.stratumUser1, [Validators.required]],
          stratumUser2: [info.stratumUser2, [Validators.required]],
          stratumPassword1: [info.stratumPassword1],
          stratumPassword2: [info.stratumPassword2],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          coin: [info.coin, [Validators.required]],
          overheat_mode: [info.overheat_mode, [Validators.required]]
        });

        this.form.controls['autofanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autofanspeed'].value)
        ).subscribe(autofanspeed => {
          if (autofanspeed) {
            this.form.controls['fanspeed'].disable();
          } else {
            this.form.controls['fanspeed'].enable();
          }
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

    const form = this.form.getRawValue();

    // if (form.stratumPassword1 === 'password') {
    //   delete form.stratumPassword1;
    // }
    // if (form.stratumPassword2 === 'password') {
    //   delete form.stratumPassword2;
    // }

    form.overheat_mode = form.overheat_mode ? 1 : 0;

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
