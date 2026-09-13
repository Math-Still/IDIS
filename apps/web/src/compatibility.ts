export type VerificationState='SOURCE_READY'|'STATIC_CHECKED'|'PENDING_REAL_DEVICE'|'VERIFIED';
export interface CompatibilityTarget{key:string;name:string;host:string;webCore:string;shell:string;native:string;verification:VerificationState;requiredEvidence:string[];}
export const compatibilityTargets:CompatibilityTarget[]=[
 {key:'uos',name:'统信 UOS',host:'Chromium / Kiosk',webCore:'同一 Vue Web Core',shell:'不需要 Native Shell',native:'Browser PlatformAdapter',verification:'PENDING_REAL_DEVICE',requiredEvidence:['系统版本与 CPU 信息','Web 界面启动','REST / WebSocket 连接','离线启动','断网恢复','性能记录']},
 {key:'kylin',name:'银河麒麟',host:'Chromium / Kiosk',webCore:'同一 Vue Web Core',shell:'不需要 Native Shell',native:'Browser PlatformAdapter',verification:'PENDING_REAL_DEVICE',requiredEvidence:['系统版本与 CPU 信息','Web 界面启动','REST / WebSocket 连接','离线启动','断网恢复','性能记录']},
 {key:'hongzos',name:'HongZOS / OpenHarmony',host:'ArkUI / ArkTS + Web Component',webCore:'同一 Vue Web Core',shell:'OpenHarmony Native Shell',native:'Native Bridge v1 + Distributed Device Provider',verification:'PENDING_REAL_DEVICE',requiredEvidence:['系统版本与板卡信息','应用启动','原生桥接连接','分布式设备状态','断网与重启恢复','性能记录']}
];
