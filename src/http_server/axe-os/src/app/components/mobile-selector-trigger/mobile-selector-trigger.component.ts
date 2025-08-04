import { Component, Input, Output, EventEmitter, HostListener } from '@angular/core';
import { SelectorConfig, SelectorOption } from '../bottom-sheet-selector/bottom-sheet-selector.component';

@Component({
  selector: 'app-mobile-selector-trigger',
  templateUrl: './mobile-selector-trigger.component.html',
  styleUrls: ['./mobile-selector-trigger.component.scss']
})
export class MobileSelectorTriggerComponent {
  @Input() config: SelectorConfig = {
    title: 'Select Options',
    type: 'single',
    options: []
  };
  @Input() selectedValue: any = null;
  @Input() selectedValues: any[] = [];
  @Input() placeholder = 'Select...';
  @Input() icon = 'pi pi-angle-down';
  
  @Output() triggerClick = new EventEmitter<void>();
  @Output() selectionChange = new EventEmitter<any>();
  @Output() multiSelectionChange = new EventEmitter<string[]>();

  showBottomSheet = false;

  get displayText(): string {
    if (this.config.type === 'single') {
      const selected = this.config.options.find(opt => opt.value === this.selectedValue);
      return selected ? selected.label : this.placeholder;
    } else {
      const selectedCount = this.selectedValues.length;
      if (selectedCount === 0) {
        return this.placeholder;
      } else if (selectedCount === 1) {
        const selected = this.config.options.find(opt => opt.value === this.selectedValues[0]);
        return selected ? selected.label : `${selectedCount} selected`;
      } else {
        return `${selectedCount} selected`;
      }
    }
  }

  get hasSelection(): boolean {
    return this.config.type === 'single' ? 
      this.selectedValue !== null && this.selectedValue !== undefined :
      this.selectedValues.length > 0;
  }

  onTriggerClick() {
    this.showBottomSheet = true;
    this.triggerClick.emit();
  }

  onSelectionChange(value: any) {
    this.selectedValue = value;
    this.showBottomSheet = false;
    this.selectionChange.emit(value);
  }

  onMultiSelectionChange(values: any[]) {
    this.selectedValues = values;
    this.multiSelectionChange.emit(values);
  }

  onBottomSheetClose() {
    this.showBottomSheet = false;
  }

  // 检测是否为移动设备
  get isMobile(): boolean {
    return window.innerWidth <= 768;
  }
}
