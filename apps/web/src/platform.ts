import { createPlatformAdapter } from '@smart-factory/platform';

// Release source always uses capability auto-detection. Mock adapters remain available
// to tests as package-level fixtures, but cannot be enabled from a production URL.
export const platform = createPlatformAdapter('auto');
