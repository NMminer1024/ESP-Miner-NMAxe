import { CommonModule } from '@angular/common';
import { NgModule } from '@angular/core';
import { RouterModule } from '@angular/router';
import { RippleModule } from 'primeng/ripple';
import { TooltipModule } from 'primeng/tooltip';

import { PrimeNGModule } from '../prime-ng.module';
import { HelpBtnComponent } from '../components/help-btn/help-btn.component';
import { HelpOverlayComponent } from '../components/help-overlay/help-overlay.component';
import { SafeHtmlPipe } from '../pipes/safe-html.pipe';
import { AppFooterComponent } from './app.footer.component';
import { AppLayoutComponent } from './app.layout.component';
import { AppMenuComponent } from './app.menu.component';
import { AppMenuitemComponent } from './app.menuitem.component';
import { AppSidebarComponent } from './app.sidebar.component';
import { AppTopBarComponent } from './app.topbar.component';

@NgModule({
    declarations: [
        AppMenuitemComponent,
        AppTopBarComponent,
        AppFooterComponent,
        AppMenuComponent,
        AppSidebarComponent,
        AppLayoutComponent,
        HelpBtnComponent,
        HelpOverlayComponent,
        SafeHtmlPipe,
    ],
    imports: [
        CommonModule,
        RouterModule,
        RippleModule,
        TooltipModule,
        PrimeNGModule,

    ],
    exports: [AppLayoutComponent, HelpBtnComponent, HelpOverlayComponent, SafeHtmlPipe]
})
export class AppLayoutModule { }
