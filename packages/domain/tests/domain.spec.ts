import { describe, expect, it } from 'vitest';
import { scenarioLabels } from '../src';
describe('domain labels',()=>{it('contains exactly the five required scenarios',()=>{expect(Object.keys(scenarioLabels)).toHaveLength(5);expect(scenarioLabels.hazardous_gas).toContain('危气');});});
