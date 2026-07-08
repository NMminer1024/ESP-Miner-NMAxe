import { Component, Input, Output, EventEmitter, OnInit, HostListener, ElementRef, Renderer2 } from '@angular/core';

export interface SelectorOption {
  value: any;
  label: string;
  selected?: boolean;
}

export interface SelectorConfig {
  title: string;
  type: 'single' | 'multi';
  options: SelectorOption[];
  maxSelected?: number;
}

@Component({
  selector: 'app-bottom-sheet-selector',
  templateUrl: './bottom-sheet-selector.component.html',
  styleUrls: ['./bottom-sheet-selector.component.scss'],
  host: {
    'style': 'position: fixed; top: 0; left: 0; width: 100%; height: 100%; pointer-events: none; z-index: 99999;'
  }
})
export class BottomSheetSelectorComponent implements OnInit {
  @Input() isVisible = false;
  @Input() config: SelectorConfig = {
    title: 'Select Options',
    type: 'single',
    options: []
  };
  @Input() selectedValue: any = null;
  @Input() selectedValues: any[] = [];
  
  @Output() isVisibleChange = new EventEmitter<boolean>();
  @Output() selectionChange = new EventEmitter<any>();
  @Output() multiSelectionChange = new EventEmitter<any[]>();
  @Output() close = new EventEmitter<void>();

  isAnimating = false;
  localOptions: SelectorOption[] = [];

  constructor(
    private elementRef: ElementRef,
    private renderer: Renderer2
  ) {}

  ngOnInit() {
    this.initializeOptions();
  }

  ngOnChanges() {
    this.initializeOptions();
  }

  private initializeOptions() {
    this.localOptions = this.config.options.map(option => ({
      ...option,
      selected: this.config.type === 'single' 
        ? option.value === this.selectedValue
        : this.selectedValues.includes(option.value)
    }));
  }

  show() {
    this.isVisible = true;
    this.isAnimating = true;
    this.isVisibleChange.emit(this.isVisible);
    
    // 更新host元素类
    this.renderer.addClass(this.elementRef.nativeElement, 'visible');
    
    // 防止背景滚动
    document.body.style.overflow = 'hidden';
    document.body.style.position = 'fixed';
    document.body.style.width = '100%';
    
    // 添加触觉反馈 (如果支持)
    if ('vibrate' in navigator) {
      navigator.vibrate(10);
    }
    
    setTimeout(() => {
      this.isAnimating = false;
    }, 400);
  }

  hide() {
    this.isAnimating = true;
    
    // 添加触觉反馈
    if ('vibrate' in navigator) {
      navigator.vibrate(5);
    }
    
    setTimeout(() => {
      this.isVisible = false;
      this.isAnimating = false;
      this.isVisibleChange.emit(this.isVisible);
      this.close.emit();
      
      // 移除host元素类
      this.renderer.removeClass(this.elementRef.nativeElement, 'visible');
      
      // 恢复背景滚动
      document.body.style.overflow = '';
      document.body.style.position = '';
      document.body.style.width = '';
    }, 350);
  }

  onBackdropClick() {
    this.hide();
  }

  onOptionClick(option: SelectorOption) {
    if (this.config.type === 'single') {
      // 单选模式
      this.localOptions.forEach(opt => opt.selected = false);
      option.selected = true;
      this.selectedValue = option.value;
      this.selectionChange.emit(option.value);
      
      // 单选模式选择后自动关闭
      setTimeout(() => this.hide(), 150);
    } else {
      // 多选模式
      const currentSelectedCount = this.localOptions.filter(opt => opt.selected).length;
      
      if (option.selected) {
        // 取消选择
        option.selected = false;
      } else {
        // 选择项目
        if (this.config.maxSelected && currentSelectedCount >= this.config.maxSelected) {
          // 如果超过最大选择数量，不允许选择
          return;
        }
        option.selected = true;
      }
      
      this.selectedValues = this.localOptions
        .filter(opt => opt.selected)
        .map(opt => opt.value);
      
      this.multiSelectionChange.emit(this.selectedValues);
    }
  }

  onConfirm() {
    if (this.config.type === 'multi') {
      this.multiSelectionChange.emit(this.selectedValues);
    }
    this.hide();
  }

  get selectedCount(): number {
    return this.localOptions.filter(opt => opt.selected).length;
  }

  get canSelectMore(): boolean {
    if (!this.config.maxSelected) return true;
    return this.selectedCount < this.config.maxSelected;
  }

  // 优化性能的 trackBy 函数
  trackByValue(index: number, option: SelectorOption): any {
    return option.value;
  }

  // 防止页面滚动穿透
  @HostListener('touchmove', ['$event'])
  onTouchMove(event: TouchEvent) {
    if (this.isVisible) {
      event.preventDefault();
    }
  }

  // 添加手势支持
  private touchStartY = 0;
  private touchCurrentY = 0;
  private isDragging = false;

  @HostListener('touchstart', ['$event'])
  onTouchStart(event: TouchEvent) {
    if (this.isVisible && event.target) {
      const target = event.target as HTMLElement;
      // 只在点击把手或内容区域外部时允许拖拽
      if (target.classList.contains('bottom-sheet-handle') || 
          target.classList.contains('bottom-sheet-content')) {
        this.touchStartY = event.touches[0].clientY;
        this.isDragging = true;
      }
    }
  }

  @HostListener('touchmove', ['$event'])
  onTouchMoveGesture(event: TouchEvent) {
    if (this.isVisible && this.isDragging) {
      this.touchCurrentY = event.touches[0].clientY;
      const deltaY = this.touchCurrentY - this.touchStartY;
      
      // 只允许向下拖拽
      if (deltaY > 0) {
        const content = document.querySelector('.bottom-sheet-content') as HTMLElement;
        if (content) {
          content.style.transform = `translateY(${deltaY}px)`;
          
          // 根据拖拽距离调整背景透明度
          const overlay = document.querySelector('.bottom-sheet-overlay') as HTMLElement;
          if (overlay) {
            const maxDrag = 200;
            const opacity = Math.max(0.3, 1 - (deltaY / maxDrag) * 0.7);
            overlay.style.background = `rgba(0, 0, 0, ${opacity * 0.5})`;
          }
        }
        event.preventDefault();
      }
    }
  }

  @HostListener('touchend', ['$event'])
  onTouchEnd(event: TouchEvent) {
    if (this.isVisible && this.isDragging) {
      const deltaY = this.touchCurrentY - this.touchStartY;
      const threshold = 100; // 拖拽超过100px就关闭
      
      const content = document.querySelector('.bottom-sheet-content') as HTMLElement;
      const overlay = document.querySelector('.bottom-sheet-overlay') as HTMLElement;
      
      if (deltaY > threshold) {
        // 关闭面板
        this.hide();
      } else {
        // 回弹到原位
        if (content) {
          content.style.transform = 'translateY(0)';
          content.style.transition = 'transform 0.3s cubic-bezier(0.25, 0.8, 0.25, 1)';
          setTimeout(() => {
            content.style.transition = '';
          }, 300);
        }
        if (overlay) {
          overlay.style.background = 'rgba(0, 0, 0, 0.5)';
        }
      }
      
      this.isDragging = false;
      this.touchStartY = 0;
      this.touchCurrentY = 0;
    }
  }

  // 帮助方法
  getTitleId(): string {
    return 'bottom-sheet-title-' + this.config.title.replace(/\s+/g, '-').toLowerCase();
  }

  getAriaLabel(): string {
    return this.config.title + '选项列表';
  }
}
