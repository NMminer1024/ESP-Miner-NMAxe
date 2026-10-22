import { ComponentFixture, TestBed } from '@angular/core/testing';

import { BestDiffChartComponent } from './best-diff-chart.component';

describe('BestDiffChartComponent', () => {
  let component: BestDiffChartComponent;
  let fixture: ComponentFixture<BestDiffChartComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ BestDiffChartComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(BestDiffChartComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
