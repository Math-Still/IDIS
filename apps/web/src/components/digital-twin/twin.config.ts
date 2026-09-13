import type { Device } from '@smart-factory/domain';
import type { PlantZoneDefinition, ProcessRouteDefinition } from './twin.types';

export const TWIN_ZONES: PlantZoneDefinition[] = [
  {
    id: 'raw-material', name: '原料与配料', shortName: '原料配料', description: '精矿仓储、配料、输送及厂内物流', shape: 'warehouse',
    position: [82, 186], size: [148, 84, 68], labelPosition: [150, 337], labelPriority: 'secondary',
    deviceIds: ['env-01', 'env-02', 'motion-01'], locationKeywords: ['精矿', '原料', '配料']
  },
  {
    id: 'smelting', name: '熔炼系统', shortName: '熔炼', description: '熔炼主厂房、炉体、上升烟道与加料系统', shape: 'smelter',
    position: [276, 164], size: [180, 104, 126], labelPosition: [360, 358], labelPriority: 'primary', locationKeywords: ['熔炼']
  },
  {
    id: 'converting', name: '吹炼 / PS 转炉', shortName: 'PS 转炉', description: '卧式转炉、倾转支撑、排烟罩及转炉烟气系统', shape: 'converter',
    position: [472, 220], size: [156, 86, 98], labelPosition: [542, 398], labelPriority: 'primary',
    deviceIds: ['safety-01'], locationKeywords: ['转炉', '吹炼']
  },
  {
    id: 'refining', name: '火法精炼 / 阳极炉', shortName: '阳极炉', description: '卧式阳极炉、燃烧端、流槽及浇铸方向', shape: 'refining',
    position: [635, 308], size: [138, 76, 78], labelPosition: [694, 474], labelPriority: 'primary',
    deviceIds: ['safety-02'], locationKeywords: ['精炼', '阳极炉']
  },
  {
    id: 'acid', name: '烟气净化与制酸', shortName: '烟气制酸', description: '烟气主管、除尘、洗涤、转化吸收与高烟囱', shape: 'acid',
    position: [748, 112], size: [164, 90, 138], labelPosition: [824, 323], labelPriority: 'primary',
    deviceIds: ['safety-01', 'safety-02'], locationKeywords: ['烟气', '制酸', '除尘', '环保']
  },
  {
    id: 'casting', name: '阳极浇铸与成品', shortName: '阳极浇铸', description: '铜液流槽、浇铸轮、阳极板及成品转运', shape: 'casting',
    position: [540, 430], size: [196, 74, 58], labelPosition: [625, 548], labelPriority: 'secondary',
    deviceIds: ['count-01', 'count-02'], locationKeywords: ['成品', '浇铸']
  },
  {
    id: 'utilities', name: '能源动力与公辅', shortName: '能源动力', description: '风机、泵组、配电、水系统及公辅设施', shape: 'utilities',
    position: [790, 414], size: [138, 76, 66], labelPosition: [852, 548], labelPriority: 'secondary',
    deviceIds: ['light-01', 'light-02'], locationKeywords: ['动力', '配电', '公辅']
  }
];

export const TWIN_PROCESS_ROUTES: ProcessRouteDefinition[] = [
  { id:'material-1', from:'raw-material', to:'smelting', kind:'material', path:'M 210 274 C 246 266, 278 258, 326 254' },
  { id:'material-2', from:'smelting', to:'converting', kind:'material', path:'M 435 282 C 468 290, 492 305, 520 318' },
  { id:'material-3', from:'converting', to:'refining', kind:'material', path:'M 604 345 C 632 359, 656 378, 684 395' },
  { id:'product-1', from:'refining', to:'casting', kind:'product', path:'M 697 418 C 680 438, 653 456, 630 470' },
  { id:'gas-1', from:'smelting', to:'acid', kind:'gas', path:'M 398 194 C 500 108, 636 92, 790 151' },
  { id:'gas-2', from:'converting', to:'acid', kind:'gas', path:'M 570 245 C 634 186, 702 155, 790 163' },
  { id:'utility-1', from:'utilities', to:'converting', kind:'utility', path:'M 826 431 C 754 402, 680 364, 594 332' },
  { id:'utility-2', from:'utilities', to:'smelting', kind:'utility', path:'M 816 446 C 710 420, 574 360, 421 288' }
];

export function matchesTwinZoneDevice(zone: PlantZoneDefinition, device: Device): boolean {
  if (zone.deviceIds?.includes(device.id)) return true;
  const location = device.location ?? '';
  return Boolean(zone.locationKeywords?.some((keyword) => location.includes(keyword)));
}
