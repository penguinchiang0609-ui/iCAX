namespace {
void EnrollmentRepairTests(const std::filesystem::path& directory) {
    Fixture fixture(directory / L"repair"), other(directory / L"repair-other");
    const auto cache = fixture.directory / L"cache";
    unsigned fetched{};
    auto fetch = [&](std::span<const unsigned char>) { ++fetched; return fixture.leaf; };
    auto result = RepairEnrollmentMaterial(fixture.ekArea, {fixture.leaf}, 0x494e5443, cache, fetch);
    Check(result.requestReady && result.code == "ready" && !fetched && !std::filesystem::exists(cache), "Existing certificate needs no network or write");
    result = RepairEnrollmentMaterial(fixture.ekArea, {}, 0x494e5443, cache, fetch);
    Check(result.requestReady && result.code == "repaired" && fetched == 1, "Missing Intel certificate repaired");
    std::vector<Bytes> cached; AppendCachedEnrollmentCertificate(cached, fixture.ekArea, cache);
    Check(cached.size() == 1 && cached[0] == fixture.leaf, "Cached material revalidated and reloaded");
    AppendCachedEnrollmentCertificate(cached, fixture.ekArea, cache);
    Check(cached.size() == 1, "Cached certificate not duplicated");
    result = RepairEnrollmentMaterial(fixture.ekArea, cached, 0x494e5443, cache, fetch);
    Check(result.requestReady && fetched == 1, "Retry uses valid cached material");
    result = RepairEnrollmentMaterial(fixture.ekArea, {}, 0x414d4400, cache, fetch);
    Check(!result.requestReady && result.code == "manufacturer_not_supported" && fetched == 1, "Unsupported vendor not sent to Intel");
    const auto badCache = fixture.directory / L"rejected-cache";
    result = RepairEnrollmentMaterial(fixture.ekArea, {}, 0x494e5443, badCache, [&](auto) { return other.leaf; });
    Check(!result.requestReady && !std::filesystem::exists(badCache), "Other-device certificate rejected before write");
    auto trailing = fixture.leaf; trailing.push_back(0);
    Reject([&] { ValidateEnrollmentCertificate(trailing, fixture.ekArea); }, "Trailing DER rejected");
    auto truncated = fixture.leaf; truncated.pop_back();
    Reject([&] { ValidateEnrollmentCertificate(truncated, fixture.ekArea); }, "Truncated DER rejected");
    Check(!EnrollmentCertificateMatches(other.leaf, fixture.ekArea), "Certificate key binding checked");
    Check(DecodeIntelEnrollmentCertificate("-_8%3D") == Bytes({0xfb, 0xff}), "Intel escaped base64url decoded");
    Check(DecodeIntelEnrollmentCertificate("-_8") == Bytes({0xfb, 0xff}), "Unpadded base64url decoded");
    for (const auto invalid : {"", "%zz", "A", "AAAA====", "A A=", "AB==", "AA==AA=="})
        Reject([&] { DecodeIntelEnrollmentCertificate(invalid); }, "Malformed base64 rejected");
    auto vector = tpm::RsaEkTemplate(); std::fill(vector.end() - 256, vector.end(), static_cast<unsigned char>(0x80));
    Check(IntelEnrollmentPath(vector) == L"/ekcertservice/n9M71pg0QdpcLlb7YIp57U01w_3RHqdbi9ObmRZsx0A%3D", "Intel URL matches independent SHA256 vector");
    for (const DWORD http : {204UL, 404UL, 503UL}) {
        result = RepairEnrollmentMaterial(fixture.ekArea, {}, 0x494e5443, badCache, [&](auto) -> Bytes { CheckEnrollmentHttpStatus(http); return {}; });
        Check(!result.requestReady && result.code == (http == 503 ? "manufacturer_service_failed" : "certificate_unavailable"), "HTTP failure classified");
    }
    result = RepairEnrollmentMaterial(fixture.ekArea, {}, 0x494e5443, badCache, [&](auto) -> Bytes { SetLastError(ERROR_WINHTTP_TIMEOUT); EnrollmentHttpCheck(FALSE); return {}; });
    Check(!result.requestReady && result.code == "network_timeout", "Network timeout classified");
    // A cached public certificate is request material, never a trust anchor.
    auto tampered = fixture.leaf; tampered.back() ^= 1;
    SaveEnrollmentCertificate(tampered, fixture.ekArea, cache);
    cached.clear(); AppendCachedEnrollmentCertificate(cached, fixture.ekArea, cache);
    Check(cached.size() == 1, "Cache only establishes key binding, not issuer trust");
    fixture.leaf = cached[0]; const auto rejectedRequest = fixture.directory / L"tampered.tdreq";
    WriteNewFile(rejectedRequest, fixture.Request(false));
    Reject([&] { InspectRequest(fixture.product, rejectedRequest, fixture.store); }, "Issuer still rejects tampered certificate signature from cache");
    const auto corruptPath = EnrollmentCachePath(cache, fixture.ekArea);
    std::filesystem::remove(corruptPath); WriteNewFile(corruptPath, Bytes{1, 2, 3});
    cached.clear(); AppendCachedEnrollmentCertificate(cached, fixture.ekArea, cache);
    Check(cached.empty(), "Corrupt cache ignored");
}
}
