import { Component, ElementRef, ViewChild } from '@angular/core';
import { interval, map, Observable, shareReplay, startWith, switchMap } from 'rxjs';
import { MenuItem } from 'primeng/api';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from '../services/loading.service';
import { SystemService } from '../services/system.service';
import { LayoutService } from './service/app.layout.service';
import { HttpErrorResponse } from '@angular/common/http';
import { ISystemInfo } from 'src/models/ISystemInfo';

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
    private infoPoll$: Observable<ISystemInfo>;
    poolDisplay$: Observable<{ url: string; wallet: string; rssi: number; uptime: number }>;
    miningControl$: Observable<{ state: string; paused: boolean }>;

    @ViewChild('menubutton') menuButton!: ElementRef;

    @ViewChild('topbarmenubutton') topbarMenuButton!: ElementRef;

    @ViewChild('topbarmenu') menu!: ElementRef;

    constructor(public layoutService: LayoutService,
                private systemService: SystemService,
                private toastr: ToastrService,
                private loadingService: LoadingService
    ) {
        this.infoPoll$ = interval(5000).pipe(
            startWith(0),
            switchMap(() => this.systemService.getInfo()),
            shareReplay({ refCount: true, bufferSize: 1 })
        );

        this.poolDisplay$ = this.infoPoll$.pipe(
            map(info => {
                const url  = info.stratum?.url  || info.stratumURLUSED || info.usedUrl  || '';
                const user = info.stratum?.user || info.stratumUserUSED || info.usedUser || '';
                const rssi = info.identity?.rssi ?? info.wifiRSSI ?? 0;
                const uptime = info.miner?.uptimeSeconds ?? info.uptimeSeconds ?? 0;
                return {
                    url:    this.stripStratumPrefix(url),
                    wallet: this.abbreviateAddress(user),
                    rssi:   rssi as number,
                    uptime: uptime as number
                };
            }),
            shareReplay({ refCount: true, bufferSize: 1 })
        );

        this.miningControl$ = this.infoPoll$.pipe(
            map(info => {
                const state = info.miner?.state || 'running';
                const paused = !!info.miner?.paused || state === 'pausing' || state === 'paused' || state === 'resuming' || state === 'error';
                return { state, paused };
            }),
            shareReplay({ refCount: true, bufferSize: 1 })
        );
    }

    private stripStratumPrefix(url: string): string {
        return url.replace(/^stratum\+(tcp|ssl):\/\//i, '');
    }

    private abbreviateAddress(user: string): string {
        const address = user.split('.')[0];
        if (address.length <= 16) return address;
        return address.substring(0, 8) + '......' + address.substring(address.length - 8);
    }

    public formatUptime(seconds: number): string {
        const d = Math.floor(seconds / 86400);
        const h = Math.floor((seconds % 86400) / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        const s = seconds % 60;
        if (d > 0) return `${d}d ${h}h ${m}m`;
        if (h > 0) return `${h}h ${m}m`;
        if (m > 0) return `${m}m ${s}s`;
        return `${s}s`;
    }


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

    public isMiningControlBusy(state: string): boolean {
        return state === 'pausing' || state === 'resuming';
    }

    public miningControlLabel(control: { state: string; paused: boolean }): string {
        if (control.state === 'pausing') return 'Pausing';
        if (control.state === 'resuming') return 'Starting';
        if (control.paused) return 'Start Mining';
        return 'Pause Mining';
    }

    public toggleMiningPause(control: { state: string; paused: boolean }) {
        const pause = !control.paused;
        this.systemService.setMiningPaused(pause)
            .pipe(this.loadingService.lockUIUntilComplete())
            .subscribe({
                next: () => {
                    this.toastr.success('Success!', pause ? 'Mining paused' : 'Mining started');
                },
                error: (err: HttpErrorResponse) => {
                    this.toastr.error('Error', `Could not ${pause ? 'pause' : 'start'} mining. ${err.message}`);
                }
            });
    }

    public showClearBlockHitsDialog() {
        this.clearBlockHitsDialogVisible = true;
    }

    public clearBlockHits() {
        this.clearBlockHitsDialogVisible = false;
        
        this.systemService.resetMinerStats('')
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
