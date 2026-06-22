import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'replace'
})
export class ReplacePipe implements PipeTransform {
  transform(value: string, searchValue: string, replaceValue: string): string {
    if (!value) return value;
    return value.replace(new RegExp(searchValue, 'g'), replaceValue);
  }
}
