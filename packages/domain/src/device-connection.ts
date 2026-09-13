import type { Device } from './models';

export function deviceProtocol(device: Device): string {
  return device.connection?.protocol?.trim() || device.protocol || '—';
}

export function deviceInterface(device: Device): string {
  return device.connection?.interfaceType?.trim() || inferLegacyInterface(device.protocol);
}

export function deviceEndpoint(device: Device): string {
  return device.connection?.endpoint?.trim() || '—';
}

export function deviceDriver(device: Device): string {
  return device.connection?.driver?.trim() || '—';
}

function inferLegacyInterface(protocol: string): string {
  const value = protocol || '';
  if (value.includes('RS485') || value.includes('RS-485')) return 'RS-485';
  if (value.includes('UART')) return 'UART';
  if (value.includes('DI')) return 'DI / DO';
  if (value.includes('4-20mA')) return '4-20mA';
  return value.split('/')[0]?.trim() || '—';
}
