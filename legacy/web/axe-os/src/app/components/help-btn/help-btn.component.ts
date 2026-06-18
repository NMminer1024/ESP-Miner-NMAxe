import { Component, Input } from '@angular/core';
import { HelpService } from '../../services/help.service';

@Component({
  selector: 'app-help-btn',
  templateUrl: './help-btn.component.html',
  styleUrls: ['./help-btn.component.scss']
})
export class HelpBtnComponent {
  @Input() key = '';

  constructor(private helpService: HelpService) {}

  open(event: MouseEvent): void {
    event.stopPropagation();
    this.helpService.open(this.key);
  }
}
