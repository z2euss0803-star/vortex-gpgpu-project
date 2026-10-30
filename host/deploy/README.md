# R13部署

本版build-id：`VXU-S8-JOINT-NN-IMAGE-P8-R13-20261029T152945Z`。启动包见assets.json和同仓库Release。服务默认UNIFIED_SERVICE_ENABLED=0，手动核对后按MANUAL.md启用唯一服务。

下载：`python host/deploy/fetch_release.py --dest <不存在或空的新目录>`。私有仓库可设置环境变量GITHUB_TOKEN用于GitHub API授权下载，脚本不打印或保存token。也可用已登录浏览器手动下载Release资产，再用`python host/deploy/fetch_release.py --archive <已下载ZIP> --dest <新目录>`离线校验并解压。仅下载、校验与解压，不写SD、不连接板卡、不启动设备。

依赖、重建边界见docs/reproduce.md。部署和启动步骤见MANUAL.md。R13尚无新boot、GPU或DP通过记录。
