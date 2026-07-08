import { ComponentFixture, TestBed } from '@angular/core/testing';

import { HrDistChartComponent } from './hr-dist-chart.component';

describe('HrDistChartComponent', () => {
  let component: HrDistChartComponent;
  let fixture: ComponentFixture<HrDistChartComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ HrDistChartComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(HrDistChartComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
