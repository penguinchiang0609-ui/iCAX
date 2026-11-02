// Included by IssuerCoreTests.cpp. All files and keys belong to OwnedTemp fixtures.
namespace {
std::string ExportHistoryPathText(const std::filesystem::path& path) {
    const auto value = path.u8string(); return {value.begin(), value.end()};
}
IssuanceHistory ExpectDuplicateIssuance(const std::function<void()>& action) {
    try { action(); }
    catch (const DuplicateTrialError& error) {
        Check(error.History().licenseId.size() > 0, "Duplicate exposes the signed issuance identity");
        if (!error.History().outputPath.empty())
            Check(std::string(error.what()).find(ExportHistoryPathText(error.History().outputPath)) != std::string::npos,
                "Duplicate diagnostic names the exact previously exported authorization path");
        return error.History();
    }
    throw std::runtime_error("Expected a structured duplicate issuance error");
}
void RemoveFixtureExportReceipts(const Fixture& fixture) {
    const auto folder = fixture.store / L"native-exports";
    Require(folder.parent_path() == fixture.store && fixture.store.parent_path() == fixture.directory
        && fixture.directory.filename().wstring().find(L"history-") == 0,
        "Only this test's temporary fixture export receipts may be removed");
    std::filesystem::remove_all(folder);
}
void SignedExportHistoryTests(const std::filesystem::path& directory) {
    Fixture fixture(directory / L"history-signed-export");
    const auto requestPath = fixture.directory / L"首份试用申请.tdreq";
    WriteNewFile(requestPath, fixture.Request(true));
    Policy policy; policy.customer = "签发记录测试客户"; policy.kind = Kind::Trial;
    policy.days = 2; policy.minMajor = 1; policy.maxMajor = 2;
    const auto output = fixture.directory / L"客户甲-首次试用授权.tdact";
    const auto issued = Generate(fixture.product, fixture.store, requestPath, Fixture::Password, policy, output);
    Check(issued.warning.empty() && !issued.reexported, "Successful export stores its signed receipt without warning");
    const auto original = ReadFileBounded(output, 65536);
    const auto certificate = VerifyCertificateForProduct(fixture.DecryptPackage(output), fixture.product,
        Fixture::IssuerId, fixture.trustedPublic);
    std::filesystem::path receiptPath;
    for (const auto& entry : std::filesystem::directory_iterator(fixture.store / L"native-exports")) {
        Check(receiptPath.empty(), "First export writes exactly one immutable receipt"); receiptPath = entry.path();
    }
    Check(!receiptPath.empty(), "Export receipt exists");
    const auto receipt = ReadFileBounded(receiptPath, 65536);
    VerifySignature(fixture.trustedPublic, std::span(receipt).first(receipt.size() - 64), std::span(receipt).last(64));
    Reader reader(std::span(receipt).first(receipt.size() - 64));
    Check(Copy(reader.Take(8)) == Text("TDEXP001") && reader.Text(128) == Fixture::IssuerId,
        "Export filename is protected by the trusted issuer signature");
    Check(reader.Text(64) == certificate.requestId && reader.Text(64) == HexText(Hash(original)),
        "Receipt binds the exact request and delivered artifact bytes");
    const auto recordedPath = reader.Field(32768);
    Check(std::string(recordedPath.begin(), recordedPath.end()) == ExportHistoryPathText(std::filesystem::absolute(output))
        && reader.Text(64) == HexText(certificate.bodyDigest) && reader.U64() > 0 && reader.End(),
        "Receipt preserves the full Chinese output filename, exact certificate version and export time");
    const auto fresh = fixture.directory / L"另一次申请.tdreq";
    WriteNewFile(fresh, fixture.Request(true, 50, 0x01501235));
    const auto inspected = InspectRequest(fixture.product, fresh, fixture.store);
    Check(inspected.previousIssuance.has_value(), "New same-device request displays original authorization before signing");
    const auto history = *inspected.previousIssuance;
    Check(history.outputPath == std::filesystem::absolute(output) && history.outputExists
        && history.recoveryPath == history.outputPath && history.licenseId == certificate.licenseId
        && history.requestId == certificate.requestId && history.customer == policy.customer
        && history.issuedAt == certificate.issuedAt, "Inspection exposes exact signed authorization and filename history");
    const auto blocked = fixture.directory / L"未确认的重复授权.tdact";
    const auto duplicate = ExpectDuplicateIssuance([&] {
        Generate(fixture.product, fixture.store, fresh, Fixture::Password, policy, blocked);
    });
    Check(duplicate.outputPath == history.outputPath && !std::filesystem::exists(blocked)
        && ReadFileBounded(output, 65536) == original, "Duplicate leaves the original file intact and produces no second authorization");
    const auto moved = fixture.directory / L"搬移后仍可找到的授权.tdact";
    std::filesystem::rename(output, moved);
    const auto missing = *InspectRequest(fixture.product, fresh, fixture.store).previousIssuance;
    Check(missing.outputPath == history.outputPath && !missing.outputExists && missing.recoveryPath == moved,
        "A missing original retains its known filename and separately reports a verified recovery file");
    RemoveFixtureExportReceipts(fixture);
    const auto legacy = *InspectRequest(fixture.product, fresh, fixture.store).previousIssuance;
    Check(legacy.outputPath == moved && legacy.outputExists && legacy.recoveryPath == moved,
        "Legacy signed ledger can recover a nearby exact authorization without inventing a filename");
    std::filesystem::create_directory(fixture.store / L"native-exports");
    auto unsignedReceipt = receipt;
    std::fill(unsignedReceipt.end() - 64, unsignedReceipt.end(), static_cast<unsigned char>(0));
    WriteNewFile(fixture.store / L"native-exports" / L"unsigned.tdexport", unsignedReceipt);
    WriteNewFile(fixture.store / L"native-exports" / L"malformed.tdexport", Text("not a signed export receipt"));
    const auto corrupt = ExpectDuplicateIssuance([&] {
        Generate(fixture.product, fixture.store, fresh, Fixture::Password, policy, blocked);
    });
    Check(corrupt.outputPath == moved && corrupt.outputExists && !std::filesystem::exists(blocked),
        "Malformed or unsigned receipts neither bypass duplicate policy nor replace verified history");

    Fixture warning(directory / L"history-receipt-warning");
    WriteNewFile(warning.store / L"native-exports", Text("fixture obstruction"));
    const auto warningOutput = warning.directory / L"已生成但未能保存文件名.tdact";
    Policy warningPolicy; warningPolicy.customer = "导出记录告警客户";
    const auto warningResult = Generate(warning.product, warning.store, warning.requestPath, Fixture::Password, warningPolicy, warningOutput);
    Check(!warningResult.warning.empty() && std::filesystem::exists(warningOutput),
        "Receipt failure reports a warning while preserving the successfully signed exported file");
}
void FailedExportHistoryTests(const std::filesystem::path& directory) {
    Fixture fixture(directory / L"history-failed-export");
    const auto trial = fixture.directory / L"试用申请.tdreq";
    WriteNewFile(trial, fixture.Request(true));
    Policy policy; policy.customer = "失败导出测试客户"; policy.kind = Kind::Trial; policy.days = 1;
    std::array<BundleInput, 1> inputs{{{&fixture.product, fixture.store, Text(Fixture::Password), trial, policy}}};
    Reject([&] { GenerateBundle(inputs, std::nullopt, fixture.directory / L"不存在" / L"未导出的授权.tdact"); },
        "A user-file export failure is surfaced after durable issuance");
    Check(!std::filesystem::exists(fixture.store / L"native-exports"),
        "Failed user-file export never creates a successful export receipt");
    const auto fresh = fixture.directory / L"新申请.tdreq";
    WriteNewFile(fresh, fixture.Request(true, 50, 0x01501235));
    const auto history = *InspectRequest(fixture.product, fresh, fixture.store).previousIssuance;
    Check(history.outputPath.empty() && !history.outputExists && !history.recoveryPath.empty()
        && history.recoveryPath.parent_path() == fixture.store / L"native-bundles",
        "Internal signed cache is recovery evidence and is never presented as a previously exported customer filename");
    const auto recovered = fixture.directory / L"重试后实际导出的授权.tdact";
    Check(GenerateBundle(inputs, std::nullopt, recovered).reexported,
        "Retry recovers the exact committed cached authorization after output failure");
    const auto exported = *InspectRequest(fixture.product, fresh, fixture.store).previousIssuance;
    Check(exported.outputPath == recovered && exported.outputExists && exported.outputPath.parent_path() != fixture.store / L"native-bundles",
        "The first successful user export becomes the historical filename");
    RemoveFixtureExportReceipts(fixture);
    const auto legacy = *InspectRequest(fixture.product, fresh, fixture.store).previousIssuance;
    Check(legacy.outputPath == recovered && legacy.outputExists,
        "A legacy bundle export is discovered by exact signed contents rather than an internal cache basename");
}
void NativeReissueChainTests(const std::filesystem::path& directory) {
    Fixture fixture(directory / L"history-reissue-chain");
    const auto firstRequest = fixture.directory / L"首次申请.tdreq";
    WriteNewFile(firstRequest, fixture.Request(true));
    Policy originalPolicy; originalPolicy.customer = "再次授权测试客户"; originalPolicy.kind = Kind::Trial;
    originalPolicy.days = 1; originalPolicy.features = KnownFeatures; originalPolicy.minMajor = 1; originalPolicy.maxMajor = 3;
    std::array<BundleInput, 1> inputs{{{&fixture.product, fixture.store, Text(Fixture::Password), firstRequest, originalPolicy}}};
    std::array<BundleStoreRef, 1> stores{{{&fixture.product, fixture.store}}};
    const auto originalPath = fixture.directory / L"首次授权.tdact";
    GenerateBundle(inputs, std::nullopt, originalPath);
    const auto originalBytes = ReadFileBounded(originalPath, static_cast<DWORD>(MaxAuthorizationFileBytes));
    const auto originalBundle = ParseAuthorizationBundle(originalBytes);
    const auto originalSlot = FindBundleSlot(originalBundle, fixture.product.id);
    const auto rootPath = fixture.directory / L"test-only-root-package.tdact";
    WriteNewFile(rootPath, originalSlot.chain.front());
    const auto rootCertificate = fixture.DecryptPackage(rootPath);
    const auto root = VerifyCertificateForProduct(rootCertificate, fixture.product, Fixture::IssuerId, fixture.trustedPublic);
    const auto fresh = fixture.directory / L"重新导出的申请.tdreq";
    WriteNewFile(fresh, fixture.Request(true, 50, 0x01501235));
    auto renewedPolicy = originalPolicy; renewedPolicy.days = 3;
    renewedPolicy.features = RequiredFeatures(Feature::ProductDesign); renewedPolicy.maxMajor = 2;
    inputs[0].sourcePath = fresh; inputs[0].policy = renewedPolicy;
    ExpectDuplicateIssuance([&] { GenerateBundle(inputs, std::nullopt, fixture.directory / L"未选择再次授权.tdact"); });
    inputs[0].allowReissue = true;
    const auto renewalPath = fixture.directory / L"按本次设置重新签发.tdact";
    const auto before = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto generated = GenerateBundle(inputs, std::nullopt, renewalPath);
    const auto after = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto renewedBytes = ReadFileBounded(renewalPath, static_cast<DWORD>(MaxAuthorizationFileBytes));
    const auto renewedBundle = VerifyAuthorizationBundleForProduct(renewedBytes, fixture.product, Fixture::IssuerId, fixture.trustedPublic);
    const auto renewedSlot = FindBundleSlot(renewedBundle, fixture.product.id);
    Check(!generated.reexported && generated.warning.empty() && renewedSlot.chain.size() == 2
        && renewedSlot.chain.front() == originalSlot.chain.front() && IsReissuePackage(renewedSlot.chain.back()),
        "Explicit renewal appends TDREI001 to the original signed activation root");
    const auto renewal = VerifyReissuePackage(renewedSlot.chain.back(), Fixture::IssuerId, fixture.trustedPublic, fixture.product);
    const auto next = renewal.parsed;
    Check(renewal.baseDigest == root.bodyDigest && next.licenseId != root.licenseId && next.requestId != root.requestId
        && next.requestId == InspectRequest(fixture.product, fresh, fixture.store).digest
        && next.customerId == root.customerId && next.devicePublicKey == root.devicePublicKey
        && next.issuedAt >= static_cast<std::uint64_t>(before) && next.issuedAt <= static_cast<std::uint64_t>(after)
        && next.notBefore == next.issuedAt && next.expiresAt - next.notBefore == 3 * 86400
        && next.features == renewedPolicy.features && next.maxMajor == renewedPolicy.maxMajor
        && next.trialNvPublic != root.trialNvPublic, "Reissue uses current time, chosen policy, fresh request/NV and preserved customer/device");
    Check(std::distance(std::filesystem::directory_iterator(fixture.store / L"native-ledger"), std::filesystem::directory_iterator{}) == 1,
        "Reissue preserves one immutable activation root instead of creating a second initial issuance");
    const auto retryPath = fixture.directory / L"相同申请再次导出.tdact";
    Check(GenerateBundle(inputs, std::nullopt, retryPath).reexported
        && ReadFileBounded(retryPath, static_cast<DWORD>(MaxAuthorizationFileBytes)) == renewedBytes,
        "Exact fresh-request retry reexports byte-identical committed reissue bundle");
    auto invalid = inputs; invalid[0].sourcePath = firstRequest;
    Reject([&] { GenerateBundle(invalid, std::nullopt, fixture.directory / L"旧申请不能续期.tdact"); },
        "Original request cannot start another trial period");
    const auto reused = fixture.directory / L"复用旧计数但随机数不同.tdreq";
    WriteNewFile(reused, fixture.Request(true, 150, 0x01501234)); invalid[0].sourcePath = reused;
    Reject([&] { GenerateBundle(invalid, std::nullopt, fixture.directory / L"旧计数不能续期.tdact"); },
        "A new nonce or counter value cannot renew a previously used NV public binding");
    const auto renewedInfo = InspectBundle(renewalPath, stores);
    Check(renewedInfo.products.size() == 1 && renewedInfo.products.front().licenseId == next.licenseId
        && renewedInfo.products.front().policy.days == 3, "Trusted issuer inspection resolves its newly signed reissue history");
    inputs[0].sourcePath = renewalPath; inputs[0].allowReissue = false;
    const auto reexportPath = fixture.directory / L"读取原授权后重新导出.tdact";
    Check(GenerateBundle(inputs, renewalPath, reexportPath).reexported
        && ReadFileBounded(reexportPath, static_cast<DWORD>(MaxAuthorizationFileBytes)) == renewedBytes,
        "Reading the renewed complete authorization can reexport the same signed file");
    auto expandedPolicy = renewedPolicy; expandedPolicy.features |= RequiredFeatures(Feature::ProductBreakdown);
    inputs[0].policy = expandedPolicy;
    const auto upgradePath = fixture.directory / L"重新签发后扩展权限.tdact";
    GenerateBundle(inputs, renewalPath, upgradePath);
    const auto upgradeBytes = ReadFileBounded(upgradePath, static_cast<DWORD>(MaxAuthorizationFileBytes));
    const auto upgradedBundle = ParseAuthorizationBundle(upgradeBytes);
    const auto upgradedSlot = FindBundleSlot(upgradedBundle, fixture.product.id);
    Check(upgradedSlot.chain.size() == 3 && std::equal(renewedSlot.chain.begin(), renewedSlot.chain.end(), upgradedSlot.chain.begin())
        && IsUpgradePackage(upgradedSlot.chain.back()), "Renewed authorization can append an ordinary permission upgrade without changing its predecessors");
    const auto upgraded = VerifyUpgradePackage(upgradedSlot.chain.back(), Fixture::IssuerId, fixture.trustedPublic, fixture.product);
    const auto upgradedHistory = *InspectRequest(fixture.product, fresh, fixture.store).previousIssuance;
    Check(upgradedHistory.outputPath == upgradePath && upgradedHistory.outputExists,
        "Ordinary upgrade history names its current exported file instead of marking an older version missing");
    const auto secondFresh = fixture.directory / L"第二次重新导出的申请.tdreq";
    WriteNewFile(secondFresh, fixture.Request(true, 50, 0x01501236));
    inputs[0].sourcePath = secondFresh; inputs[0].allowReissue = true; inputs[0].policy.days = 2;
    const auto secondPath = fixture.directory / L"第二次重新签发.tdact";
    GenerateBundle(inputs, upgradePath, secondPath);
    const auto secondBytes = ReadFileBounded(secondPath, static_cast<DWORD>(MaxAuthorizationFileBytes));
    const auto secondBundle = VerifyAuthorizationBundleForProduct(secondBytes, fixture.product, Fixture::IssuerId, fixture.trustedPublic);
    const auto secondSlot = FindBundleSlot(secondBundle, fixture.product.id);
    Check(secondSlot.chain.size() == 4 && std::equal(upgradedSlot.chain.begin(), upgradedSlot.chain.end(), secondSlot.chain.begin())
        && IsReissuePackage(secondSlot.chain.back()), "A second fresh reissue extends root, reissue and upgrade with every prefix byte preserved");
    const auto final = VerifyReissuePackage(secondSlot.chain.back(), Fixture::IssuerId, fixture.trustedPublic, fixture.product);
    const auto decryptRoot = [&](std::span<const unsigned char> bytes) {
        const auto path = fixture.directory / L"test-only-client-root-decrypt.tdact";
        WriteNewFile(path, bytes); return fixture.DecryptPackage(path);
    };
    Check(ResolveAuthorizationChain(secondSlot, {}, Fixture::IssuerId, fixture.trustedPublic, fixture.product, decryptRoot) == final.certificate,
        "Actual native signed fixture packages are consumed by the client mixed-chain resolver from first activation");
    const auto noDecrypt = [](std::span<const unsigned char>) -> Bytes { throw std::runtime_error("Unexpected fixture root decryption"); };
    for (const auto* installed : {&rootCertificate, &renewal.certificate, &upgraded.certificate, &final.certificate})
        Check(ResolveAuthorizationChain(secondSlot, *installed, Fixture::IssuerId, fixture.trustedPublic, fixture.product, noDecrypt) == final.certificate,
            "Client accepts the actual native chain from each installed predecessor and persisted successor");
    const auto latestHistory = *InspectRequest(fixture.product, secondFresh, fixture.store).previousIssuance;
    Check(latestHistory.licenseId == final.parsed.licenseId && latestHistory.outputPath == secondPath && latestHistory.outputExists,
        "Same-device history reports the newest reissued identity and its real exported filename");
    Check(ReadFileBounded(originalPath, static_cast<DWORD>(MaxAuthorizationFileBytes)) == originalBytes
        && ReadFileBounded(renewalPath, static_cast<DWORD>(MaxAuthorizationFileBytes)) == renewedBytes,
        "Repeated signing never alters the original delivered authorizations");
}
void IssuerExportHistoryTests(const std::filesystem::path& directory) {
    SignedExportHistoryTests(directory); FailedExportHistoryTests(directory); NativeReissueChainTests(directory);
}
}
