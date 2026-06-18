import {Injectable} from '@angular/core';
import {Subject, Observable} from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class WebsocketService {

  private _ws: WebSocket | null = null;
  private _messages$ = new Subject<string>();

  /**
   * Open a new WebSocket connection and return an Observable of incoming messages.
   * Closes any existing connection first.
   */
  connect(): Observable<string> {
    // always close any existing socket before opening a new one
    this._close();

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${protocol}//${window.location.host}/ws`);
    this._ws = ws;

    ws.onmessage = (e: MessageEvent) => {
      this._messages$.next(e.data as string);
    };

    ws.onerror = (e) => {
      console.warn('[WS] error', e);
    };

    ws.onclose = () => {
      console.log('[WS] connection closed');
    };

    return this._messages$.asObservable();
  }

  /** Close the WebSocket — triggers WS_EVT_DISCONNECT on the device. */
  disconnect(): void {
    this._close();
  }

  private _close(): void {
    if (this._ws) {
      // only call close() if socket is CONNECTING or OPEN
      if (this._ws.readyState === WebSocket.CONNECTING ||
          this._ws.readyState === WebSocket.OPEN) {
        this._ws.close();
      }
      this._ws = null;
    }
  }
}
