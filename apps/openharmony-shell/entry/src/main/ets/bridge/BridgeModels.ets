export class BridgeRequest {
  bridgeVersion: string = '';
  id: string = '';
  method: string = '';
  params: Object = {};
  timestamp: number = 0;
}

export class BridgeErrorPayload {
  code: string = 'UNKNOWN';
  message: string = '';
  recoverable: boolean = true;
}

export class BridgeResponse {
  bridgeVersion: string = '1.0.0';
  id: string = '';
  ok: boolean = false;
  result: Object | null = null;
  error: BridgeErrorPayload | null = null;
}
