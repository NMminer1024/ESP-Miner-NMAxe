import {Component, OnInit} from '@angular/core';
import {ToastrService} from 'ngx-toastr';

import {SystemService} from '../services/system.service';
import {LayoutService} from './service/app.layout.service';

@Component({
  selector: 'app-menu',
  templateUrl: './app.menu.component.html'
})
export class AppMenuComponent implements OnInit {

  model: any[] = [];

  constructor(public layoutService: LayoutService,
    private systemService: SystemService,
    private toastr: ToastrService
  ) {
  }

  ngOnInit() {
    this.model = [
      {
        items: [
          {label: 'Dashboard', icon: 'pi pi-fw pi-home', routerLink: ['/']},
          {label: 'Monitor', icon: 'pi pi-fw pi-chart-line', routerLink: ['monitor']},
          {label: 'Swarm', icon: 'pi pi-fw pi-share-alt', routerLink: ['swarm']},
          {label: 'Settings', icon: 'pi pi-fw pi-cog', routerLink: ['settings']},
          {label: 'Logs', icon: 'pi pi-fw pi-list', routerLink: ['logs']},
          {label: 'Update', icon: 'pi pi-fw pi-upload', routerLink: ['update']},
        ]
      }
    ];
  }
}
