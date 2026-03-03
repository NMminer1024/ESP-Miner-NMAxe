import { Component, ElementRef, ViewChild } from '@angular/core';
import { MenuItem } from 'primeng/api';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from '../services/loading.service';
import { SystemService } from '../services/system.service';
import { LayoutService } from './service/app.layout.service';
import { HttpErrorResponse } from '@angular/common/http';

@Component({
    selector: 'app-topbar',
    templateUrl: './app.topbar.component.html',
    styles: [`
        ::ng-deep .p-dialog .p-dialog-footer {
            padding: 1rem 1.5rem;
            border-top: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        ::ng-deep .p-dialog .p-dialog-footer .p-button-secondary.p-button-outlined {
            background: transparent !important;
            border: 1px solid rgba(255, 255, 255, 0.3) !important;
            color: #ffffff !important;
        }
        
        ::ng-deep .p-dialog .p-dialog-footer .p-button-secondary.p-button-outlined:hover {
            background: rgba(255, 255, 255, 0.1) !important;
            border: 1px solid rgba(255, 255, 255, 0.5) !important;
            color: #ffffff !important;
        }
        
        ::ng-deep .p-dialog .p-dialog-footer .p-button-secondary.p-button-outlined:focus {
            background: transparent !important;
            border: 1px solid rgba(255, 255, 255, 0.3) !important;
            color: #ffffff !important;
            outline: none !important;
            box-shadow: none !important;
        }
        
        ::ng-deep .p-dialog .p-dialog-footer .p-button-danger {
            background: #dc3545 !important;
            border: 1px solid #dc3545 !important;
            color: #ffffff !important;
        }
        
        ::ng-deep .p-dialog .p-dialog-footer .p-button-danger:hover {
            background: #c82333 !important;
            border: 1px solid #c82333 !important;
            color: #ffffff !important;
        }
        
        ::ng-deep .p-dialog .p-dialog-footer .p-button-danger:focus {
            background: #dc3545 !important;
            border: 1px solid #dc3545 !important;
            color: #ffffff !important;
            outline: none !important;
            box-shadow: none !important;
        }
    `]
})
export class AppTopBarComponent {

    items!: MenuItem[];
    clearBlockHitsDialogVisible: boolean = false;

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

    public showClearBlockHitsDialog() {
        this.clearBlockHitsDialogVisible = true;
    }

    public clearBlockHits() {
        this.clearBlockHitsDialogVisible = false;
        
        const clearData = { blockhits: 0 };
        
        this.systemService.updateSystem('', clearData)
            .pipe(this.loadingService.lockUIUntilComplete())
            .subscribe({
                next: () => {
                    this.toastr.success('Success!', 'Block hits cleared');
                },
                error: (err: HttpErrorResponse) => {
                    this.toastr.error('Error', `Could not clear block hits. ${err.message}`);
                }
            });
    }


}
