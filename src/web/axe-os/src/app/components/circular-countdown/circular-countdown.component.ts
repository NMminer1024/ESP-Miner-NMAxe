import { Component, Input, OnChanges, SimpleChanges } from '@angular/core';

@Component({
  selector: 'app-circular-countdown',
  template: `
    <div class="circular-countdown" [title]="countdown + 's'">
      <svg class="countdown-svg" width="40" height="40" viewBox="0 0 40 40">
        <!-- Background circle -->
        <circle
          cx="20"
          cy="20"
          r="16"
          class="countdown-bg"
          fill="none"
          stroke="var(--surface-border)"
          stroke-width="3">
        </circle>
        <!-- Progress circle -->
        <circle
          cx="20"
          cy="20"
          r="16"
          class="countdown-progress"
          fill="none"
          [attr.stroke]="getStrokeColor()"
          stroke-width="3"
          stroke-linecap="round"
          [attr.stroke-dasharray]="circumference"
          [attr.stroke-dashoffset]="strokeDashoffset"
          transform="rotate(-90 20 20)">
        </circle>
        <!-- Center text -->
        <text
          x="20"
          y="20"
          text-anchor="middle"
          dominant-baseline="central"
          class="countdown-text"
          [attr.fill]="getTextColor()">
          {{ countdown }}
        </text>
      </svg>
    </div>
  `,
  styleUrls: ['./circular-countdown.component.scss']
})
export class CircularCountdownComponent implements OnChanges {
  @Input() countdown: number = 0;
  @Input() maxValue: number = 30;
  @Input() size: number = 40;

  circumference: number = 0;
  strokeDashoffset: number = 0;

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['maxValue'] || changes['countdown']) {
      this.updateProgress();
    }
  }

  private updateProgress(): void {
    const radius = 16; // Circle radius
    this.circumference = 2 * Math.PI * radius;
    
    // Calculate progress (0 to 1)
    const progress = this.maxValue > 0 ? (this.maxValue - this.countdown) / this.maxValue : 0;
    
    // Calculate stroke-dashoffset
    this.strokeDashoffset = this.circumference * (1 - progress);
  }

  getStrokeColor(): string {
    const progress = this.maxValue > 0 ? this.countdown / this.maxValue : 0;
    
    if (progress > 0.6) {
      return 'var(--green-500)'; // Green for plenty of time
    } else if (progress > 0.3) {
      return 'var(--orange-500)'; // Orange for medium time
    } else {
      return 'var(--red-500)'; // Red for low time
    }
  }

  getTextColor(): string {
    const progress = this.maxValue > 0 ? this.countdown / this.maxValue : 0;
    
    if (progress > 0.6) {
      return 'var(--green-500)';
    } else if (progress > 0.3) {
      return 'var(--orange-500)';
    } else {
      return 'var(--red-500)';
    }
  }
}
