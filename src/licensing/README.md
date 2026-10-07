# TubeDesigner 离线 TPM 授权

已接入申请、激活、状态、页面访问及原生操作检查点。features.json 是客户端、签发工具共用的唯一权限目录；覆盖产品、下料、加工、仿真四页和各页独立功能。

当前签发端为纯 C++ / Win32 单 EXE：运行 output/tools/iCAX-License-Issuer.exe，选择产品和申请文件后点击“生成授权文件”，自动输出同目录的 .tdact。既有正式签发库和当前 Windows 用户保存的口令自动加载，页面与功能权限在折叠选项中独立设置。构建及使用说明见 issuer_native/README.md。当前程序不依赖 Python 或验证器子程序；issuer 目录保留此前原型和协议测试代码。

一个 .tdact 可以包含多个产品，各产品分别声明页面与功能权限并用自己的固定公钥验签。签发器逐产品选择申请和权限后一次生成；追加功能时选择客户导出的最新完整文件，已有产品和权限全部保留，切换要升级的产品再生成。文件包含完整首次激活与升级链，原电脑可直接导入最新文件，不必逐次导入中间版本。试用转正式及原试用期限/NV绑定规则不变。产品由 include/ProductCatalog.h 编译注册，目前仅注册 TubeDesigner；未来增加真实产品声明，每份内部证书仍绑定一个产品。

## 发布前必需步骤

仅 Debug 构建（_DEBUG 且未定义 NDEBUG）在编译期提供开发免授权；Release 和其他宏组合均强制授权。没有运行时免授权开关。未配置正式公钥、未激活、权限不符或设备证明不符均拒绝操作。状态页的 capabilities 同时校验父页面和子操作，开发免授权明确标识且不伪装成证书激活。

2026-10-07 的正式签发库位于仓库外 D:/TubeDesigner-Signing/20261007/primary。私钥为口令加密的 PKCS#8 P-256，口令单独由当前 Windows 用户保护保存；上级目录的“查看签发口令.ps1”提供本机取回入口。客户端只编译该签发库的公钥。另一个 standby 目录是因错误理解“两对密钥”而额外生成的签发密钥，保留为未使用材料，不作为设备密钥或客户端信任根。正式发行打包拒绝未配置公钥或免授权 DLL。历史临时测试公钥不作为正式签发材料。

两类密钥的职责固定：设备密钥由每台客户电脑在永久授权申请中通过 TPM 创建 P-256 受限 AK，私钥不导出；申请含设备公钥和随机 Quote，签发证书绑定同一设备公钥，程序的每个业务检查点重新取得 TPM Quote 来证明设备私钥。授权签发密钥由签发端保管，签署授权证书及激活包，程序用内置签发公钥验证签名。设备密钥不会被统一的客户端安装包或签发库私钥替代。当前授权端通过离线签发工具处理申请，未增加在线授权服务器接口。

在隔离电脑创建签发库 → 导出公钥头文件到 include → 重建 Application、TubeDesigner 及辅助客户端 → 目标设备完成签发激活验收。公钥不从客户文件或环境变量动态加载。独立 CMake 工程用于工具和测试，运行时头文件直接接入解决方案的 TubeDesigner 项目。

Application 清单要求管理员；启用授权检查的构建在每个业务检查点使用新随机挑战及 TPM 交互，没有跨操作授权布尔缓存。模板不能保证机器码不被编译器合并。尚需最终发行二进制审计、独立绕过测试；未提供加壳、代码签名或加密狗 SDK 适配。

## 协议与状态

- TDREQ002 永久申请：受限 AK、RSA EK、厂商链、随机 Quote；TDREQ003 另含 NV 签名绑定。
- 签发只信任独立导入的厂商根，Windows 原生离线验证，无系统根回退或在线吊销查询。Intel PTT 多段厂商 NV 链只读拼接。
- TDACT001 激活包：签发者签名，MakeCredential/ActivateCredential 保护 AES-256-GCM 密钥，只有对应硬件解开内部证书。
- ICAXB001 多产品文件：最多 32 个产品、每产品 64 个链包、文件 1 MiB。各产品签名覆盖同一个规范清单及所有包的长度和摘要。签发端校验同一客户及 TPM；客户端使用自己的内置公钥验完整清单，保留完整源文件用于升级回传，不把其他产品的公钥当作信任来源。
- TDLIC003 是当前唯一证书格式：ECDSA P-256/SHA-256，绑定设备、签发者、产品、功能、主版本范围；kind=2 的试用追加 NV 公共区、初值、3600 秒档位。旧 TDLIC001/002 一律拒绝，不迁移。
- TDUPG001 升级包：签发者签名覆盖基础证书摘要与独立签名的新证书。签发端验证完整签名台账及唯一后继；客户端在独占安装锁内验证基础摘要、身份、权限和版本，再做新随机 TPM Quote 并原子替换。错误文件保留原证书，重复同一升级可重导入。试用保留原期限与 NV 绑定。当前已安装证书上的降级导入被拒绝；恢复整个磁盘旧快照没有硬件或服务器版本水位保护。
- 页面 bit 仅允许进入；子操作同时要求父页面 bit。孤立子操作、未知位、零权限即使签名有效仍拒绝。目录修改后运行 generate_feature_catalog.py，--check 校验生成目录一致。
- 产品持久化生成/编辑归 product.design，拆单归 product.breakdown；下料标准件/冲孔/绘制归 nesting.edit，排样归 nesting.calculate。导出根据原生数据库中的零件来源选择 product.export 或 nesting.export；请求中的 nestingOnly 不改变产品写权限。
- 共用管型/模具编辑与参数预览允许 product.design 或 nesting.edit。当前 CAM 注册接口基于宿主 ProductContext 声明和场景资源归属校验，TubeDesigner 加工读取要求 page.machining，写入要求 machining.toolpath；其他产品的许可策略不受影响。仿真仅预留目录与访问检查，未新增业务。
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

build-issuer.ps1 使用 CMake/MSVC 构建原生签发器并执行全部七组软件回归，输出 output/tools/iCAX-License-Issuer.exe 一个文件，不生成私钥。程序 --self-test 可隐藏启动自检，--check-settings 可只读检查本机签发配置。

## 当前协议与策略验证（2026-10-07）

独立 CMake 的 Release 与 Debug 测试通过；签发器和原生 TDLIC003 互通覆盖 13 个单页/页面加操作组合、全权限及完整 NV 试用证书，并拒绝签名有效的旧格式、孤立操作和未知位。新测试没有配置客户端正式公钥，没有执行 TPM/NV 写入。完整发行 SDO 拒绝和发行构建信息由本轮集成报告单独记录。

以下记录是旧版本的历史验收，包含当时 Release 免授权和 TDLIC001/002，不能作为当前发行授权已完成的证明。

## 历史授权链路验收（2026-10-05）

本轮重新构建原生 Release 工具，并直接调用当前交付 `TubeDesigner.dll` 的真实 `TubeDesignerLicensing` SDO。发行 DLL SHA256 为 `DBFA5FADD84A603A3907A6E1241495DB338DBA26E64B8BCD47249DEB215B7B3F`，没有替换发行 DLL 或配置正式签发公钥。

| 环节 | 当前交付 Release | 隔离永久授权硬件测试 |
| --- | --- | --- |
| 申请 | `Request` 成功生成 TDREQ002；AK Quote、厂商 EK 证书链及本机绑定通过 | 通过 |
| 激活 | `Activate` 明确拒绝“尚未配置正式签发公钥” | 临时测试签发、TPM Credential Activation、激活包解密和原子安装通过 |
| 验证 | `Status` 返回 `releaseBypass=true`、`configured=false`、`activated=false`；业务授权仍被跳过 | 设计、拆单、STEP 导出、排样四类直接 TPM 验证通过，共 12 次逐调用证明 |

隔离测试还通过篡改申请、缺失厂商信任根、重复签发、篡改激活包、篡改已装证书、删除证书、错误签发密钥和错误设备绑定的拒绝检查。失败激活保留原证书，测试结束恢复测试证书且没有残留原子安装临时文件。申请自带的证书链缺少上级中间 CA，导入中间 CA 后重新验证通过；信任根从 [Intel 官方说明](https://software.intel.com/sites/manageability/AMT_Implementation_and_Reference_Guide/WordDocuments/ODCA.htm)指向的地址独立下载并核对 SHA256。

原生 CTest 2/2 通过（核心 881 项检查）；签发 Python 测试 20/20、无跳过；授权 UI 单元测试 31 条断言通过。UI 单元使用模拟宿主，真实 Release SDO 使用发行 DLL；本轮没有桌面 GUI 点击验收。没有执行真实试用申请、试用激活或试用 NV 写入；本轮永久授权硬件结果不代替试用硬件验收。

专用 `td-license-hardware-tests` 只在 `BUILD_TESTING` 下构建。`request <新申请文件绝对路径>` 调用真实永久申请实现；`activate-verify <测试公钥blob> <测试issuer-id> <测试tdact> <全新测试目录>` 调用真实解密、证书校验、原子安装和逐功能 TPM 验证。激活测试需 Windows 管理员权限；目录必须全新且不得与正式 ProgramData 授权目录重叠。生产 `InstallCertificate` 仍固定使用 `LicenseDirectory()`，目录参数只用于独立测试入口。

Python 互操作测试可通过 `TD_LICENSE_TEST_BINARY_DIR` 指向本轮重新构建的 Release 工具目录。测试材料使用临时签发库，临时私钥已删除，正式 ProgramData 证书与正式公钥配置首尾保持不变。证据位于 `output/tests/licensing-acceptance-20261005/`：`hardware-elevated.json`、`release-sdo-acceptance.json`、`software/report-final.json`，汇总见 `acceptance.json`。

## 试用硬件与桌面界面补充验收（2026-10-05）

补充验收尚未全部完成，不能记为试用硬件通过。独立 `td-license-trial-hardware-tests` Release 工具构建、核心 CTest 2/2、Python 与 PowerShell 脚本检查和独立代码审查通过；唯一一次管理员启动被 Windows 取消，测试进程没有启动。因此本次没有创建试用申请、临时私钥或 NV 索引，没有写试用计数器。启动前后只读观察到相同 14 个已有 NV handles。

已准备的固定流程覆盖真实 TDREQ003 申请、临时签发、TPM 激活、激活前试用校验、隔离安装、四类权限及同小时无写入；临时签名日期样本调用真实 `VerifyTpmAtSite` / `EnforceTrial`，验证两小时推进、时间回拨拒绝和 24 档预算持久到期，不修改 Windows 时间。还覆盖证据重放与篡改、删除和重新定义 NV 后拒绝原试用。流程只清理本次明确拥有的计数器；不确定的跨磁盘/TPM状态拒绝盲目删除。执行成功后也不能宣称 TPM 内部全局单调计数水位恢复原值。

单次管理员执行入口为 `output/tests/licensing-trial-acceptance-20261005/run-elevated.ps1 -Stage request`，它在一个管理员进程中完成申请、临时签发、激活、验证和最终清理。当前状态、编译与只读检查证据见该目录 `acceptance.json` 和 `README.md`；实际硬件流程启动后才生成 `hardware-trial-lifecycle.json`。

实际交付 `TubeDesigner.exe` 已通过 Computer Use 启动并观察到正常设计界面。截图读取成功，但点击“关于”“添加”和按 Tab 没有可见响应；源码清单要求管理员，管理员窗口输入限制是当前推测原因。授权按钮、保存/打开对话框以及桌面申请、激活、状态操作仍待人工协助，不计为通过。记录见 `output/tests/licensing-gui-acceptance-20261005/desktop-gui-acceptance.json`。没有修改发行程序、公钥配置或 Release 免授权策略。

## 实测范围（2026-09-07）

当前 Intel PTT 实机通过 EK 厂商链验证、受限 AK Quote、Credential Activation、激活包解密、证书验证、逐调用 TPM 证明及篡改拒绝；试用计数推进、时间回拨拒绝、同小时不额外写入通过。测试仅用临时测试签发密钥，测试 NV 已清理，未创建正式私钥。

不代表全部 TPM 型号兼容或获得安全认证。无 TPM/策略不支持设备拒绝；加密狗未选型接入。离线永久授权不能远程撤销。签发库须受控备份，厂商证书须人工维护。
