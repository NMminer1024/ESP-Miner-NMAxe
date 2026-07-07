import {ComponentFixture, TestBed} from '@angular/core/testing';

import {PreferenceComponent} from './preference.component';

describe('EditComponent', () => {
  let component: PreferenceComponent;
  let fixture: ComponentFixture<PreferenceComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [PreferenceComponent]
    });
    fixture = TestBed.createComponent(PreferenceComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
