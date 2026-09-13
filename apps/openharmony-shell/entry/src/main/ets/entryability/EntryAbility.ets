import UIAbility from '@ohos.app.ability.UIAbility';
import window from '@ohos.window';
import abilityAccessCtrl, { Permissions } from '@ohos.abilityAccessCtrl';

const DISTRIBUTED_PERMISSION: Permissions = 'ohos.permission.DISTRIBUTED_DATASYNC';

export default class EntryAbility extends UIAbility {
  onWindowStageCreate(windowStage: window.WindowStage): void {
    // The distributed provider is created by the page/BridgeService. Load the page only
    // after the user-grant request settles so DeviceManager is not initialized before
    // DISTRIBUTED_DATASYNC has been granted or denied.
    const atManager: abilityAccessCtrl.AtManager = abilityAccessCtrl.createAtManager();
    atManager.requestPermissionsFromUser(this.context, [DISTRIBUTED_PERMISSION])
      .then((result) => {
        const granted: boolean = result.authResults.length > 0 && result.authResults[0] === 0;
        console.info(`SmartFactoryShell: DISTRIBUTED_DATASYNC granted=${granted}`);
      })
      .catch((error) => {
        console.error(`SmartFactoryShell: permission request failed: ${String(error)}`);
      })
      .then(() => {
        this.loadMainPage(windowStage);
      });
  }

  private loadMainPage(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        console.error(`Failed to load Index. Code=${err.code}, message=${err.message}`);
        return;
      }
      console.info('SmartFactoryShell: Index loaded.');
    });
  }
}
