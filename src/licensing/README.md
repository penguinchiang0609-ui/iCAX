# TubeDesigner 离线 TPM 授权

已接入申请、激活、状态界面以及原生设计、拆单、导出、排样检查点。签发工具支持加密私钥创建、厂商链审核、永久/试用签发、台账重导出、备份与构建公钥导出。操作说明见 issuer/README.md。

## 发布前必需步骤

Debug 构建（_DEBUG 且未定义 NDEBUG）跳过业务授权检查，不读取授权文件、不访问 TPM、不推进试用计数；状态页显示开发免授权，不伪装已激活。显式申请/激活工具仍保留真实流程供联调。Release 继续严格校验；没有运行时免授权开关，禁止把 Debug 包交付客户。

尚未生成正式私钥或配置正式公钥。缺少 include/IssuerTrust.generated.h 时，受保护操作明确拒绝，不通过测试密钥放行。

在隔离电脑创建签发库 → 导出公钥头文件到 include → 重建 Application、TubeDesigner 及辅助客户端 → 目标设备完成签发激活验收。公钥不从客户文件或环境变量动态加载。独立 CMake 工程用于工具和测试，运行时头文件直接接入解决方案的 TubeDesigner 项目。

Application 清单要求管理员；每个业务检查点有新随机挑战及 TPM 交互，没有跨操作授权布尔缓存。模板不能保证机器码不被编译器合并。尚需最终发行二进制审计、独立绕过测试；未提供加壳、代码签名或加密狗 SDK 适配。

## 协议与状态

- TDREQ002 永久申请：受限 AK、RSA EK、厂商链、随机 Quote；TDREQ003 另含 NV 签名绑定。
- 签发只信任独立导入的厂商根，Windows 原生离线验证，无系统根回退或在线吊销查询。Intel PTT 多段厂商 NV 链只读拼接。
- TDACT001 激活包：签发者签名，MakeCredential/ActivateCredential 保护 AES-256-GCM 密钥，只有对应硬件解开内部证书。
- TDLIC001 永久证书：ECDSA P-256/SHA-256，绑定设备、签发者、产品、功能、主版本范围。TDLIC002 试用增加 NV 公共区、初值、3600 秒档位。
- 证书原子安装至 ProgramData/TubeDesigner/Licensing/license.tdlic。客户端不修改签名证书；篡改、删除、错误设备拒绝。
- 试用由 NV 单调计数器保存小时水位，随机 NV_Certify 验签并串行推进，同小时不写，30 天最多 720 档。没有磁盘时间记录事务。状态丢失重新申请，完全离线仍不能防时钟冻结。
- 确定性 owner 受限 AK 使用临时句柄，TPM 清除会改变绑定。试用申请成功创建的 NV 项需保留；异常只清理本次创建项，不清 TPM 或其他产品状态。

## 构建与验证

```powershell
cmake -S src/licensing -B Temp/licensing-build -G "Visual Studio 17 2022" -A x64
cmake --build Temp/licensing-build --config Release
ctest --test-dir Temp/licensing-build -C Release --output-on-failure
python -B -m unittest discover -s src/licensing/issuer -p 'test_*.py' -v
node src/tests/iCAX-UI/TubeDesignerLicensingTest.mjs
```

build-issuer.ps1 接受带 cryptography 的 Python 路径和 PyInstaller 依赖目录，输出 Temp/licensing-delivery/TubeDesigner-Issuer 完整目录，不生成私钥。打包程序 --self-test 可隐藏启动自检。

## 实测范围（2026-09-07）

当前 Intel PTT 实机通过 EK 厂商链验证、受限 AK Quote、Credential Activation、激活包解密、证书验证、逐调用 TPM 证明及篡改拒绝；试用计数推进、时间回拨拒绝、同小时不额外写入通过。测试仅用临时测试签发密钥，测试 NV 已清理，未创建正式私钥。

不代表全部 TPM 型号兼容或获得安全认证。无 TPM/策略不支持设备拒绝；加密狗未选型接入。离线永久授权不能远程撤销。签发库须受控备份，厂商证书须人工维护。
