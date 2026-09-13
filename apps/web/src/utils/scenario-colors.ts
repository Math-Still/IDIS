export const scenarioColors = {
  temperature_humidity: ['#126F8C','#688B9B','#99ADB6'],
  pir_lighting: ['#688B9B','#879EA8','#A3B4BC'],
  hazardous_gas: ['#496C7D','#688B9B','#99ADB6'],
  agv_obstacle: ['#708F9D','#8EA4AD','#A8B8BF'],
  goods_counting: ['#258BA8','#688B9B','#99ADB6']
} as const;

const fallbackColor = '#688B9B';

function paletteFor(scenario?: string): readonly string[] | undefined {
  return scenario && Object.prototype.hasOwnProperty.call(scenarioColors, scenario)
    ? scenarioColors[scenario as keyof typeof scenarioColors]
    : undefined;
}

export function scenarioColor(scenario?: string): string {
  return paletteFor(scenario)?.[0] ?? fallbackColor;
}

export function trendColor(scenario?: string, pointId?: string): string {
  const colors = paletteFor(scenario);
  if (!colors) return fallbackColor;
  if (!pointId) return colors[0];
  let hash = 2166136261;
  for (let index = 0; index < pointId.length; index++) {
    hash = Math.imul(hash ^ pointId.charCodeAt(index), 16777619) >>> 0;
  }
  return colors[hash % colors.length];
}
