import { Component, HostListener, OnDestroy, OnInit } from '@angular/core';
import { Subscription } from 'rxjs';
import { HelpEntry } from '../../help/help-content';
import { HelpService } from '../../services/help.service';

@Component({
  selector: 'app-help-overlay',
  templateUrl: './help-overlay.component.html',
  styleUrls: ['./help-overlay.component.scss']
})
export class HelpOverlayComponent implements OnInit, OnDestroy {
  visible = false;
  content: HelpEntry | null = null;

  private subs = new Subscription();

  constructor(private helpService: HelpService) {}

  ngOnInit(): void {
    this.subs.add(
      this.helpService.visible$.subscribe(v => (this.visible = v))
    );
    this.subs.add(
      this.helpService.content$.subscribe(c => (this.content = c))
    );
  }

  ngOnDestroy(): void {
    this.subs.unsubscribe();
  }

  close(): void {
    this.helpService.close();
  }

  @HostListener('document:keydown.escape')
  onEsc(): void {
    if (this.visible) this.close();
  }
}
