import { Component, ElementRef, ViewChild } from '@angular/core';
import { MenuItem } from 'primeng/api';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from '../services/loading.service';
import { SystemService } from '../services/system.service';
import { LayoutService } from './service/app.layout.service';
import { HttpErrorResponse } from '@angular/common/http';

@Component({
    selector: 'app-topbar',
    templateUrl: './app.topbar.component.html'
})
export class AppTopBarComponent {

    items!: MenuItem[];

    @ViewChild('menubutton') menuButton!: ElementRef;

    @ViewChild('topbarmenubutton') topbarMenuButton!: ElementRef;

    @ViewChild('topbarmenu') menu!: ElementRef;

    constructor(public layoutService: LayoutService,
                private systemService: SystemService,
                private toastr: ToastrService,
                private loadingService: LoadingService
    ) { }

    public restart() {
        this.systemService.restart()
            .pipe(this.loadingService.lockUIUntilComplete())
            .subscribe({
                next: () => {
                    this.toastr.success('Success!', 'Miner restarted');
                },
                error: (err: HttpErrorResponse) => {
                    this.toastr.error('Error', `Could not restart. ${err.message}`);
                }
            });
    }


}
