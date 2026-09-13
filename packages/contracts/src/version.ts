export const WEB_APP_VERSION = '1.6.0' as const;
export const BRIDGE_PROTOCOL_VERSION = '1.0.0' as const;
export const DEFAULT_BACKEND_API_VERSION = 'v1' as const;
export interface VersionInfo { webVersion:string; shellVersion:string|null; bridgeVersion:string; backendApiVersion:string; }
