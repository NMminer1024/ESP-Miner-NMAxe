import {Pipe, PipeTransform} from '@angular/core';

@Pipe({
  name: 'diffSuffix',
  standalone: false
})
export class DiffSuffixPipe implements PipeTransform {

  private static _this = new DiffSuffixPipe();

  public static transform(value: number): string {
    return this._this.transform(value);
  }

  public static revert(value: string): number {
    return this._this.revert(value);
  }

  public revert(value: string): number {
    const suffixes = ['', 'K', 'M', 'G', 'T', 'P', 'E'].reverse();
    const power = suffixes.length - suffixes.findIndex(suffix => value.endsWith(suffix)) - 1;
    if (power === suffixes.length) {
      return parseFloat(value);
    }
    const scaledValue = parseFloat(value.replace(suffixes[power], ''));
    return scaledValue * Math.pow(1000, power);
  }

  public transform(value: number): string {

    if (value == null || value < 0) {
      return '0';
    }

    const suffixes = ['', 'K', 'M', 'G', 'T', 'P', 'E'];

    let power = Math.floor(Math.log10(value) / 3);
    if (power < 0) {
      power = 0;
    }
    const scaledValue = value / Math.pow(1000, power);
    const suffix = suffixes[power];

    let floatLength = 4;
    if (scaledValue > 1) {
      floatLength = 3;
    }
    if (scaledValue > 10) {
      floatLength = 2;
    }
    if (scaledValue > 100) {
      floatLength = 1;
    }

    return scaledValue.toFixed(floatLength) + suffix;
  }
}
