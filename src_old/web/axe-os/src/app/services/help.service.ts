import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';
import { HELP_CONTENT, HelpEntry } from '../help/help-content';

@Injectable({ providedIn: 'root' })
export class HelpService {
  private _visible = new BehaviorSubject<boolean>(false);
  private _content = new BehaviorSubject<HelpEntry | null>(null);

  visible$ = this._visible.asObservable();
  content$ = this._content.asObservable();

  open(key: string): void {
    const entry = HELP_CONTENT[key];
    if (!entry) {
      console.warn(`HelpService: no content found for key "${key}"`);
      return;
    }
    this._content.next(entry);
    this._visible.next(true);
  }

  close(): void {
    this._visible.next(false);
  }
}
