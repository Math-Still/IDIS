import { describe, expect, it } from 'vitest';
import * as palette from './scenario-colors';

const expected = {
  temperature_humidity: ['#4DD9F5','#8CE9F9','#66BFF3'],
  pir_lighting: ['#C89CFF','#E0BFFF','#B8A5FF'],
  hazardous_gas: ['#70ADFF','#A0CCFF','#80C6F0'],
  agv_obstacle: ['#48DCC8','#8BE8D9','#64CADA'],
  goods_counting: ['#A6AAFF','#CECFFF','#97BFFF']
};

describe('scenario palette', () => {
  it('provides the five approved, distinct three-tone palettes', () => {
    expect(palette?.scenarioColors).toEqual(expected);
    expect(new Set(Object.values(palette.scenarioColors).flat()).size).toBe(15);
  });

  it('uses the primary tone for scenario identity and a missing point', () => {
    expect(palette?.scenarioColor).toBeTypeOf('function');
    expect(palette?.trendColor).toBeTypeOf('function');
    for (const [scenario, colors] of Object.entries(expected)) {
      expect(palette.scenarioColor(scenario)).toBe(colors[0]);
      expect(palette.trendColor(scenario)).toBe(colors[0]);
      expect(palette.trendColor(scenario, '')).toBe(colors[0]);
    }
  });

  it('falls back safely for absent, unknown and inherited object-key names', () => {
    expect(palette?.scenarioColor).toBeTypeOf('function');
    expect(palette?.trendColor).toBeTypeOf('function');
    for (const scenario of [undefined, '', 'future_scenario', 'toString', '__proto__']) {
      expect(palette.scenarioColor(scenario)).toBe('#B4C1D1');
      expect(palette.trendColor(scenario, 'co')).toBe('#B4C1D1');
    }
  });

  it('keeps point colors stable when points are reordered, filtered or added', () => {
    expect(palette?.trendColor).toBeTypeOf('function');
    const ids = ['co','so2','future-point'];
    const initial = Object.fromEntries(ids.map(id => [id, palette.trendColor('hazardous_gas', id)]));
    for (const order of [[...ids].reverse(), ['so2'], ['new-point', ...ids]]) {
      for (const id of order.filter(id => id in initial)) {
        expect(palette.trendColor('hazardous_gas', id)).toBe(initial[id]);
      }
    }
    expect(initial.co).not.toBe(initial.so2);
  });

  it('keeps all point colors within their own scenario palette', () => {
    expect(palette?.trendColor).toBeTypeOf('function');
    for (const [scenario, colors] of Object.entries(expected)) {
      for (const id of ['temperature','humidity','co','so2','frontDistance','rate','totalCount','新测点']) {
        expect(colors).toContain(palette.trendColor(scenario, id));
      }
    }
  });

  it('reserves warm red, amber and orange hues for alarms', () => {
    expect(palette?.scenarioColors).toEqual(expected);
    for (const hex of Object.values(palette.scenarioColors).flat()) {
      const red = parseInt(hex.slice(1,3), 16);
      const blue = parseInt(hex.slice(5,7), 16);
      expect(blue).toBeGreaterThanOrEqual(red);
    }
  });
});
