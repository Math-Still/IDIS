# SSH 远程目标机验证

目标国产操作系统环境提供 SSH 后，可从开发机执行：

```bash
./deploy/ssh/remote-build-test.sh user@target-host /opt/smart-factory 22
```

脚本只完成源码同步、目标机原生编译和 CTest，不会自动开启真实设备紧急控制。

SSH 只是开发/测试通道，不属于最终运行时依赖。
