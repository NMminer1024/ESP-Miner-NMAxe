import {Injectable} from '@angular/core';
import {BehaviorSubject, Observable, finalize} from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class LoadingService {

  public loading$: BehaviorSubject<boolean> = new BehaviorSubject(false);

  constructor() {
  }

  // Keeps the full-screen spinner up for the duration of the request.
  //
  // The previous implementation re-implemented the forwarding by hand and got
  // three things wrong, all of which showed up together as "the page is stuck on
  // Waiting...":
  //   - an ERROR was forwarded as `next(err)`, so callers' `error:` handlers
  //     never ran and the SUCCESS path processed the error object as if it were
  //     the response (every field undefined, so the UI filled with fallbacks);
  //   - loading$ was only reset on `complete`, which an errored request never
  //     reaches, so the overlay stayed up forever;
  //   - the inner subscription's teardown was dropped, so a component destroyed
  //     mid-request kept the request alive and could still flip loading$ later.
  // Reporting on Nexus with v3.2.02k: one unparseable /api/benchmark response
  // left the Benchmark page on "Waiting..." in every browser, cache cleared.
  public lockUIUntilComplete() {
    return <T>(source: Observable<T>): Observable<T> => {
      return new Observable<T>(subscriber => {
        this.loading$.next(true);
        const sub = source
          .pipe(finalize(() => this.loading$.next(false)))
          .subscribe(subscriber);
        return () => sub.unsubscribe();
      });
    }
  }
}

