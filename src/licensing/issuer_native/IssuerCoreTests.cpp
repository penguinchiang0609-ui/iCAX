// Native software-crypto interoperability tests. No TPM, installed license,
// production issuer directory, or Windows trust store is opened or modified.
#include "IssuerCore.h"
#include "LicenseFiles.h"
#include "TpmEvidence.h"
#include <wincrypt.h>
#include <algorithm>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace tube::license;
using namespace tube::license::issuer_native;
namespace {
unsigned checks{};
void Check(bool value, const char* message) { ++checks; Require(value, message); }
void Reject(const std::function<void()>& action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, message);
}
Bytes Copy(std::span<const unsigned char> input) { return {input.begin(), input.end()}; }
Bytes Text(std::string_view value) { return {value.begin(), value.end()}; }
void Join(Bytes& to, std::span<const unsigned char> from) { to.insert(to.end(), from.begin(), from.end()); }
Bytes Concat(std::initializer_list<Bytes> values) { Bytes result; for (const auto& v : values) Join(result, v); return result; }
Bytes Hex(std::string_view value) {
    auto digit = [](char c) -> unsigned { if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10); throw std::runtime_error("Bad fixture hex"); };
    Require(value.size() % 2 == 0, "Bad fixture hex size"); Bytes result;
    for (std::size_t i = 0; i < value.size(); i += 2) result.push_back(static_cast<unsigned char>((digit(value[i]) << 4) | digit(value[i + 1])));
    return result;
}
std::string HexText(std::span<const unsigned char> value) {
    constexpr char digits[] = "0123456789abcdef"; std::string result;
    for (auto byte : value) { result += digits[byte >> 4]; result += digits[byte & 15]; } return result;
}
void U16(Bytes& out, std::uint16_t value) { out.push_back(static_cast<unsigned char>(value >> 8)); out.push_back(static_cast<unsigned char>(value)); }
void Sized(Bytes& out, std::span<const unsigned char> value) {
    Require(value.size() <= 65535, "Fixture TPM field too large"); U16(out, static_cast<std::uint16_t>(value.size())); Join(out, value);
}
Bytes Der(unsigned char tag, std::span<const unsigned char> value) {
    Bytes result{tag};
    if (value.size() < 128) result.push_back(static_cast<unsigned char>(value.size()));
    else { Bytes length; auto n = value.size(); while (n) { length.insert(length.begin(), static_cast<unsigned char>(n)); n >>= 8; }
        result.push_back(static_cast<unsigned char>(0x80 | length.size())); Join(result, length); }
    Join(result, value); return result;
}
Bytes Seq(std::initializer_list<Bytes> values) { return Der(0x30, Concat(values)); }
Bytes Integer(std::span<const unsigned char> value) {
    auto first = value.begin(); while (first != value.end() && *first == 0) ++first;
    Bytes positive(first, value.end()); if (positive.empty() || (positive[0] & 128)) positive.insert(positive.begin(), 0);
    return Der(2, positive);
}
Bytes Number(std::uint32_t value) { Bytes bytes; Append32(bytes, value); return Integer(bytes); }
Bytes Bits(std::span<const unsigned char> value) { Bytes bytes{0}; Join(bytes, value); return Der(3, bytes); }

// Every test key is generated with the software provider and stays ephemeral.
struct TestKey final {
    Algorithm algorithm;
    PublicKey key;
    explicit TestKey(bool rsa = false) : algorithm(rsa ? BCRYPT_RSA_ALGORITHM : BCRYPT_ECDSA_P256_ALGORITHM) {
        BCryptCheck(BCryptGenerateKeyPair(algorithm.value, &key.value, rsa ? 2048UL : 256UL, 0), "Generate test key");
        BCryptCheck(BCryptFinalizeKeyPair(key.value, 0), "Finalize test key");
    }
    Bytes Export(LPCWSTR format) const {
        ULONG length{}; BCryptCheck(BCryptExportKey(key.value, nullptr, format, nullptr, 0, &length, 0), "Size test key blob");
        Bytes out(length); BCryptCheck(BCryptExportKey(key.value, nullptr, format, out.data(), length, &length, 0), "Export test key blob");
        out.resize(length); return out;
    }
    Bytes Public() const { return Export(BCRYPT_ECCPUBLIC_BLOB); }
    Bytes Sign(std::span<const unsigned char> message) const {
        const auto digest = Hash(message); ULONG size{}; Bytes signature(64);
        BCryptCheck(BCryptSignHash(key.value, nullptr, const_cast<PUCHAR>(digest.data()), 32, signature.data(), 64, &size, 0), "Sign fixture");
        Require(size == 64, "Unexpected test ECDSA size"); return signature;
    }
};
Bytes HashBytes(std::span<const unsigned char> value) { const auto h = Hash(value); return {h.begin(), h.end()}; }
Bytes Hmac(std::span<const unsigned char> key, std::span<const unsigned char> message) {
    BCRYPT_ALG_HANDLE raw{};
    BCryptCheck(BCryptOpenAlgorithmProvider(&raw, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG), "Open test HMAC");
    struct Guard { BCRYPT_ALG_HANDLE h; ~Guard() { BCryptCloseAlgorithmProvider(h, 0); } } guard{raw};
    Bytes output(32);
    BCryptCheck(BCryptHash(raw, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), const_cast<PUCHAR>(message.data()),
        static_cast<ULONG>(message.size()), output.data(), 32), "Fixture HMAC"); return output;
}
Bytes Kdfa(std::span<const unsigned char> key, std::string_view label, std::span<const unsigned char> context, unsigned bits) {
    Bytes message; Append32(message, 1); Join(message, Text(label)); message.push_back(0); Join(message, context); Append32(message, bits);
    auto output = Hmac(key, message); output.resize(bits / 8); return output;
}
Bytes Aes(std::span<const unsigned char> rawKey, LPCWSTR mode, std::span<const unsigned char> input,
    Bytes iv, bool decrypt, bool pad = false) {
    Algorithm algorithm(BCRYPT_AES_ALGORITHM); PublicKey key;
    BCryptCheck(BCryptSetProperty(algorithm.value, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
        static_cast<ULONG>((wcslen(mode) + 1) * sizeof(wchar_t)), 0), "Set fixture AES mode");
    BCryptCheck(BCryptGenerateSymmetricKey(algorithm.value, &key.value, nullptr, 0, const_cast<PUCHAR>(rawKey.data()),
        static_cast<ULONG>(rawKey.size()), 0), "Import fixture AES key");
    Bytes output(input.size() + 32); ULONG length{};
    auto operation = decrypt ? BCryptDecrypt : BCryptEncrypt;
    BCryptCheck(operation(key.value, const_cast<PUCHAR>(input.data()), static_cast<ULONG>(input.size()), nullptr,
        iv.empty() ? nullptr : iv.data(), static_cast<ULONG>(iv.size()), output.data(), static_cast<ULONG>(output.size()),
        &length, pad ? BCRYPT_BLOCK_PADDING : 0), "Fixture AES operation");
    output.resize(length); return output;
}
Bytes DecryptCfb(std::span<const unsigned char> key, std::span<const unsigned char> encrypted) {
    Bytes feedback(16), plain;
    for (std::size_t offset = 0; offset < encrypted.size(); offset += 16) {
        const auto stream = Aes(key, BCRYPT_CHAIN_MODE_ECB, feedback, {}, false);
        const auto length = (std::min)(std::size_t{16}, encrypted.size() - offset);
        for (std::size_t i = 0; i < length; ++i) plain.push_back(static_cast<unsigned char>(encrypted[offset + i] ^ stream[i]));
        if (length == 16) feedback = Copy(encrypted.subspan(offset, 16));
    }
    return plain;
}
Bytes DecryptGcm(std::span<const unsigned char> keyBytes, const Bytes& nonce, const Bytes& encrypted, const Bytes& aad) {
    Require(encrypted.size() > 16, "Short fixture GCM"); Algorithm algorithm(BCRYPT_AES_ALGORITHM); PublicKey key;
    BCryptCheck(BCryptSetProperty(algorithm.value, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        sizeof(BCRYPT_CHAIN_MODE_GCM), 0), "Set test GCM");
    BCryptCheck(BCryptGenerateSymmetricKey(algorithm.value, &key.value, nullptr, 0, const_cast<PUCHAR>(keyBytes.data()),
        static_cast<ULONG>(keyBytes.size()), 0), "Import test GCM");
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth; BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = const_cast<PUCHAR>(nonce.data()); auth.cbNonce = static_cast<ULONG>(nonce.size());
    auth.pbAuthData = const_cast<PUCHAR>(aad.data()); auth.cbAuthData = static_cast<ULONG>(aad.size());
    auth.pbTag = const_cast<PUCHAR>(encrypted.data() + encrypted.size() - 16); auth.cbTag = 16;
    Bytes plain(encrypted.size() - 16); ULONG length{};
    BCryptCheck(BCryptDecrypt(key.value, const_cast<PUCHAR>(encrypted.data()), static_cast<ULONG>(plain.size()), &auth,
        nullptr, 0, plain.data(), static_cast<ULONG>(plain.size()), &length, 0), "Authenticate activation certificate");
    plain.resize(length); return plain;
}

Bytes EncryptedPkcs8(const TestKey& key, std::string_view password) {
    auto privateBlob = key.Export(BCRYPT_ECCPRIVATE_BLOB);
    Require(privateBlob.size() == 104, "Unexpected fixture private key");
    Bytes point{4}; Join(point, std::span(privateBlob).subspan(8, 64));
    const auto algorithm = Seq({Hex("06072a8648ce3d0201"), Hex("06082a8648ce3d030107")});
    auto plain = Seq({Number(0), algorithm, Der(4, Seq({Number(1), Der(4, std::span(privateBlob).subspan(72, 32)), Der(0xa1, Bits(point))}))});
    const auto random = RandomChallenge(); const Bytes salt(random.begin(), random.begin() + 16), iv(random.begin() + 16, random.end());
    constexpr std::uint32_t iterations = 20000;
    BCRYPT_ALG_HANDLE hash{};
    BCryptCheck(BCryptOpenAlgorithmProvider(&hash, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG), "Open fixture PBKDF2");
    struct Guard { BCRYPT_ALG_HANDLE h; ~Guard() { BCryptCloseAlgorithmProvider(h, 0); } } guard{hash};
    Bytes derived(32);
    BCryptCheck(BCryptDeriveKeyPBKDF2(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()), iterations, derived.data(), 32, 0), "Fixture PBKDF2");
    const auto cipher = Aes(derived, BCRYPT_CHAIN_MODE_CBC, plain, iv, false, true);
    auto result = Seq({Seq({Hex("06092a864886f70d01050d"), Seq({
        Seq({Hex("06092a864886f70d01050c"), Seq({Der(4, salt), Number(iterations), Seq({Hex("06082a864886f70d0209"), Hex("0500")})})}),
        Seq({Hex("060960864801650304012a"), Der(4, iv)})})}), Der(4, cipher)});
    SecureZeroMemory(plain.data(), plain.size()); SecureZeroMemory(privateBlob.data(), privateBlob.size()); SecureZeroMemory(derived.data(), derived.size());
    return result;
}
Bytes Pem(std::span<const unsigned char> der) {
    DWORD length{}; Require(CryptBinaryToStringA(der.data(), static_cast<DWORD>(der.size()), CRYPT_STRING_BASE64, nullptr, &length), "Fixture base64 size");
    std::string encoded(length, '\0'); Require(CryptBinaryToStringA(der.data(), static_cast<DWORD>(der.size()), CRYPT_STRING_BASE64, encoded.data(), &length), "Fixture base64");
    encoded.resize(strlen(encoded.c_str()));
    return Text("-----BEGIN ENCRYPTED PRIVATE KEY-----\r\n" + encoded + "-----END ENCRYPTED PRIVATE KEY-----\r\n");
}
Bytes Name(std::string_view value) { return Seq({Der(0x31, Seq({Hex("0603550403"), Der(0x0c, Text(value))}))}); }
Bytes CertificateTime(std::int64_t days) {
    FILETIME current{}; GetSystemTimeAsFileTime(&current);
    ULARGE_INTEGER ticks{}; ticks.LowPart = current.dwLowDateTime; ticks.HighPart = current.dwHighDateTime;
    ticks.QuadPart = static_cast<ULONGLONG>(static_cast<std::int64_t>(ticks.QuadPart) + days * 864000000000LL);
    FILETIME shifted{ticks.LowPart, ticks.HighPart}; SYSTEMTIME time{}; Require(FileTimeToSystemTime(&shifted, &time), "Fixture certificate time");
    std::ostringstream value; value << std::setfill('0') << std::setw(4) << time.wYear << std::setw(2) << time.wMonth
        << std::setw(2) << time.wDay << std::setw(2) << time.wHour << std::setw(2) << time.wMinute << std::setw(2) << time.wSecond << 'Z';
    return Der(0x18, Text(value.str()));
}
Bytes Spki(const TestKey& key, bool rsa) {
    if (!rsa) { const auto publicKey = key.Public(); Bytes point{4}; Join(point, std::span(publicKey).subspan(8));
        return Seq({Seq({Hex("06072a8648ce3d0201"), Hex("06082a8648ce3d030107")}), Bits(point)}); }
    const auto blob = key.Export(BCRYPT_RSAPUBLIC_BLOB); BCRYPT_RSAKEY_BLOB header{}; std::memcpy(&header, blob.data(), sizeof(header));
    Require(header.cbPublicExp == 3 && header.cbModulus == 256, "Fixture RSA key size");
    return Seq({Seq({Hex("06092a864886f70d010101"), Hex("0500")}), Bits(Seq({
        Integer(std::span(blob).subspan(sizeof(header) + header.cbPublicExp, header.cbModulus)),
        Integer(std::span(blob).subspan(sizeof(header), header.cbPublicExp))}))});
}
Bytes DerSignature(std::span<const unsigned char> signature) { return Seq({Integer(signature.first(32)), Integer(signature.last(32))}); }
Bytes Certificate(const TestKey& subjectKey, const TestKey& signer, bool ca) {
    const auto signatureAlgorithm = Seq({Hex("06082a8648ce3d040302")});
    Bytes extensions;
    Join(extensions, Seq({Hex("0603551d13"), Hex("0101ff"), Der(4, ca ? Seq({Hex("0101ff")}) : Seq({}))}));
    Join(extensions, Seq({Hex("0603551d0f"), Hex("0101ff"), Der(4, ca ? Hex("03020106") : Hex("03020520"))}));
    if (!ca) Join(extensions, Seq({Hex("0603551d25"), Der(4, Seq({Hex("06056781050801")}))}));
    const auto random = RandomChallenge();
    auto tbs = Seq({Der(0xa0, Number(2)), Integer(std::span(random).first(16)), signatureAlgorithm,
        Name("TEST ONLY NATIVE ROOT"), Seq({CertificateTime(-1), CertificateTime(1)}),
        Name(ca ? "TEST ONLY NATIVE ROOT" : "TEST ONLY SOFTWARE EK"), Spki(subjectKey, !ca), Der(0xa3, Der(0x30, extensions))});
    return Seq({tbs, signatureAlgorithm, Bits(DerSignature(signer.Sign(tbs)))});
}
Bytes TpmSignature(const TestKey& key, const Bytes& attest) {
    const auto signature = key.Sign(attest); Bytes wire; U16(wire, 0x18); U16(wire, 11);
    Sized(wire, std::span(signature).first(32)); Sized(wire, std::span(signature).last(32)); return wire;
}
Bytes SignedCertificate(tube::license::Certificate certificate,const ProductDescriptor& product,const TestKey& signer){
    Bytes body=Text(product.certificateFormat);
    for(const auto& value:{certificate.issuerId,certificate.licenseId,certificate.requestId,certificate.customerId,certificate.product})AppendText(body,value);
    Append32(body,static_cast<std::uint32_t>(certificate.strategy));Append32(body,static_cast<std::uint32_t>(certificate.kind));
    Append32(body,certificate.features);Append32(body,certificate.minMajor);Append32(body,certificate.maxMajor);Append64(body,certificate.issuedAt);
    Append64(body,certificate.notBefore);Append64(body,certificate.expiresAt);AppendField(body,certificate.devicePublicKey);
    if(certificate.kind==Kind::Trial){AppendField(body,certificate.trialNvPublic);Append64(body,certificate.trialInitialCounter);Append32(body,certificate.trialQuantumSeconds);}
    Join(body,signer.Sign(body));return body;
}

struct Fixture final {
    static constexpr std::string_view Password = "TEST-ONLY-native-fixture-密碼";
    static constexpr std::string_view IssuerId = "test-only-native-issuer";
    TestKey issuer, root, ak, ek{true};
    std::filesystem::path directory, store, requestPath;
    Bytes leaf, akArea, ekArea, qualified, trustedPublic;
    ProductDescriptor product;
    explicit Fixture(const std::filesystem::path& folder, ProductDescriptor selected = TubeDesignerProduct)
        : directory(folder), store(folder / L"store"), requestPath(folder / L"test.tdreq"), trustedPublic(issuer.Public()), product(selected) {
        product.issuerId = IssuerId; product.publicKey = trustedPublic;
        Require(!std::filesystem::exists(folder), "Fixture directory must be new");
        std::filesystem::create_directories(store / L"manufacturer-roots"); std::filesystem::create_directory(store / L"intermediates");
        const auto publicKey = issuer.Public();
        WriteNewFile(store / L"issuer-private.pem", Pem(EncryptedPkcs8(issuer, Password)));
        WriteNewFile(store / L"issuer-public.blob", publicKey);
        const auto config = "{\"format\":1,\"issuer_id\":\"" + std::string(IssuerId) + "\",\"public_sha256\":\"" + HexText(Hash(publicKey)) + "\"}";
        WriteNewFile(store / L"issuer.json", Text(config));
        WriteNewFile(store / L"manufacturer-roots" / L"test-root.cer", Certificate(root, root, true));
        leaf = Certificate(ek, root, false);
        const auto device = ak.Public(); U16(akArea, 0x23); U16(akArea, 11); Append32(akArea, 0x50072);
        Sized(akArea, HashBytes(Text(product.attestationPrimaryDomain)));
        for (auto v : std::array<std::uint16_t, 5>{0x10, 0x18, 11, 3, 0x10}) U16(akArea, v);
        Sized(akArea, std::span(device).subspan(8, 32)); Sized(akArea, std::span(device).subspan(40, 32));
        Bytes qualifiedInput; Append32(qualifiedInput, 0x40000001); Join(qualifiedInput, TpmSha256Name(akArea));
        qualified = Bytes{0, 11}; Join(qualified, HashBytes(qualifiedInput));
        U16(ekArea, 1); U16(ekArea, 11); Append32(ekArea, 0x300b2);
        Sized(ekArea, Hex("837197674484b3f81a90cc8d46a5d724fd52d76e06520b64f2a1da1b331469aa"));
        for (auto v : std::array<std::uint16_t, 5>{6, 128, 0x43, 0x10, 2048}) U16(ekArea, v); Append32(ekArea, 0);
        const auto rsa = ek.Export(BCRYPT_RSAPUBLIC_BLOB); BCRYPT_RSAKEY_BLOB h{}; std::memcpy(&h, rsa.data(), sizeof(h));
        Sized(ekArea, std::span(rsa).subspan(sizeof(h) + h.cbPublicExp, h.cbModulus));
        WriteNewFile(requestPath, Request(false));
    }
    Bytes Request(bool trial, std::uint64_t initial = 50, std::uint32_t nvIndex = 0x01501234) const {
        const auto nonce = RandomChallenge();
        Bytes attest; Append32(attest, 0xff544347); U16(attest, 0x8018); Sized(attest, qualified); Sized(attest, nonce);
        Join(attest, Bytes(16)); attest.push_back(1); Join(attest, Bytes(8)); Append32(attest, 0); Sized(attest, HashBytes({}));
        Bytes request = Text(trial ? product.trialRequestFormat : product.permanentRequestFormat);
        for (const auto& field : {Text(product.id), Copy(nonce), akArea, qualified, ekArea}) AppendField(request, field);
        Append32(request, 1); AppendField(request, leaf); AppendField(request, attest); AppendField(request, TpmSignature(ak, attest));
        if (trial) {
            Bytes nv; Append32(nv, nvIndex); U16(nv, 11); Append32(nv, 0x22040014); U16(nv, 0); U16(nv, 8);
            Bytes value; Append64(value, initial);
            Bytes evidence; Append32(evidence, 0xff544347); U16(evidence, 0x8014); Sized(evidence, qualified); Sized(evidence, nonce);
            Join(evidence, Bytes(16)); evidence.push_back(1); Join(evidence, Bytes(8)); Sized(evidence, TpmSha256Name(nv)); U16(evidence, 0); Sized(evidence, value);
            AppendField(request, nv); Append64(request, initial); AppendField(request, evidence); AppendField(request, TpmSignature(ak, evidence));
        }
        return request;
    }
    Bytes DecryptPackage(const std::filesystem::path& path,const TestKey* ekOwner=nullptr) const {
        const auto package = ReadFileBounded(path, 65536); Require(package.size() > 72, "Activation size");
        VerifySignature(issuer.Public(), std::span(package).first(package.size() - 64), std::span(package).last(64));
        Reader reader(std::span(package).first(package.size() - 64)); Check(Copy(reader.Take(8)) == Text("TDACT001"), "Activation protocol");
        const auto digest = reader.Field(32), name = reader.Field(68), credentialBlob = reader.Field(8192), secret = reader.Field(8192),
            nonce = reader.Field(12), encrypted = reader.Field(8192); Check(reader.End(), "Activation fields exact");
        Check(name == TpmSha256Name(akArea), "AK name bound in activation");
        BCRYPT_OAEP_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM, reinterpret_cast<PUCHAR>(const_cast<char*>("IDENTITY")), 9};
        Bytes seed(256); ULONG length{};
        BCryptCheck(BCryptDecrypt(ekOwner?ekOwner->key.value:ek.key.value, const_cast<PUCHAR>(secret.data()), static_cast<ULONG>(secret.size()), &padding,
            nullptr, 0, seed.data(), static_cast<ULONG>(seed.size()), &length, BCRYPT_PAD_OAEP), "Unwrap test credential seed");
        seed.resize(length); Check(seed.size() == 32, "Credential seed size");
        Require(credentialBlob.size() > 34, "Short credential blob");
        Check(credentialBlob[0] == 0 && credentialBlob[1] == 32, "Credential integrity TPM2B size");
        const auto encryptedCredential = std::span(credentialBlob).subspan(34);
        Bytes authenticated = Copy(encryptedCredential); Join(authenticated, name);
        Check(Copy(std::span(credentialBlob).subspan(2, 32)) == Hmac(Kdfa(seed, "INTEGRITY", {}, 256), authenticated), "Credential integrity");
        const auto credential = DecryptCfb(Kdfa(seed, "STORAGE", name, 128), encryptedCredential);
        Check(credential.size() == 34 && credential[0] == 0 && credential[1] == 32, "Credential TPM2B size");
        const auto certificate = DecryptGcm(std::span(credential).subspan(2), nonce, encrypted, digest);
        const auto parsed = VerifyCertificateForProduct(certificate, product, IssuerId, trustedPublic);
        Check(parsed.requestId == HexText(digest) && parsed.devicePublicKey == ak.Public(), "Package and inner certificate share request and device");
        return certificate;
    }
};
struct OwnedTemp final {
    std::filesystem::path path;
    OwnedTemp() : path(std::filesystem::temp_directory_path() / (L"td-native-issuer-tests-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64()))) { Require(!std::filesystem::exists(path), "Test root must be new"); std::filesystem::create_directory(path); }
    ~OwnedTemp() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void Tests(const std::filesystem::path& directory) {
    Fixture fixture(directory / L"permanent");
    const auto info = InspectStore(fixture.product,fixture.store); Check(info.issuerId == Fixture::IssuerId, "Store identity");
    const auto request = InspectRequest(fixture.product,fixture.requestPath,fixture.store);
    Check(!request.isTrial && request.deviceHash == HexText(Hash(fixture.ekArea)), "Permanent request identity");
    Policy policy; policy.customer = "原生测试客户"; policy.minMajor = 1; policy.maxMajor = 2;
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, "wrong-password", policy, fixture.directory / L"wrong.tdact"); }, "Wrong password rejected");
    Check(!std::filesystem::exists(fixture.directory / L"wrong.tdact"), "Wrong password produces no package");
    auto invalidPolicy = policy; invalidPolicy.customer.clear();
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, invalidPolicy, fixture.directory / L"no-customer.tdact"); }, "Empty customer rejected");
    invalidPolicy = policy; invalidPolicy.minMajor = 3;
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, invalidPolicy, fixture.directory / L"bad-version.tdact"); }, "Inverted version range rejected");
    invalidPolicy = policy; invalidPolicy.kind = static_cast<Kind>(0);
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, invalidPolicy, fixture.directory / L"bad-kind.tdact"); }, "Invalid kind rejected");
    for (const auto mask : {0U, 16U, 128U, 1024U, 4096U, 8192U}) {
        auto invalid = policy; invalid.features = mask;
        Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, invalid, fixture.directory / (std::to_wstring(mask) + L".tdact")); }, "Invalid permission set rejected");
    }
    auto tampered = fixture.Request(false); tampered.back() ^= 1;
    const auto badRequest = fixture.directory / L"tampered.tdreq"; WriteNewFile(badRequest, tampered);
    Reject([&] { InspectRequest(fixture.product,badRequest,fixture.store); }, "Quote tamper rejected");
    const auto output = fixture.directory / L"license.tdact";
    const auto issued = Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, policy, output);
    Check(!issued.reexported && issued.outputPath == output, "First generation commits output");
    const auto certificate = fixture.DecryptPackage(output); const auto c = VerifyCertificate(certificate, Fixture::IssuerId, fixture.issuer.Public());
    Check(c.kind == Kind::Permanent && c.features == KnownFeatures && c.minMajor == 1 && c.maxMajor == 2
        && c.requestId == request.digest && c.devicePublicKey == fixture.ak.Public(), "Native client accepts exact certificate policy");
    for (const auto& feature : FeatureCatalog) Check(HasFeature(c.features, feature.feature), "Every catalog permission survives signing");
    auto damaged = certificate; damaged.back() ^= 1;
    Reject([&] { VerifyCertificate(damaged, Fixture::IssuerId, fixture.issuer.Public()); }, "Certificate tamper rejected");
    const auto reexport = fixture.directory / L"reexport.tdact";
    const auto again = Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, policy, reexport);
    Check(again.reexported && again.licenseId == issued.licenseId && ReadFileBounded(output, 65536) == ReadFileBounded(reexport, 65536), "Exact repeated request reexports committed package");
    auto changed = policy; changed.features = 1;
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, changed, fixture.directory / L"changed.tdact"); }, "Same request changed policy rejected");
    changed = policy; changed.customer += "改变";
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, changed, fixture.directory / L"changed-customer.tdact"); }, "Same request changed customer rejected");
    changed = policy; changed.days = 29;
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, changed, fixture.directory / L"changed-days.tdact"); }, "Same request changed recorded days rejected");
    const auto protectedBytes = ReadFileBounded(output, 65536);
    Reject([&] { Generate(fixture.product,fixture.store, fixture.requestPath, Fixture::Password, policy, output); }, "Exclusive export refuses overwrite");
    Check(ReadFileBounded(output, 65536) == protectedBytes, "Existing output kept intact");
    for (const auto& descriptor : FeatureCatalog) {
        const auto bit = static_cast<std::uint32_t>(descriptor.feature);
        const auto mask = bit | descriptor.parent;
        const auto perFeatureRequest = fixture.directory / (L"feature-" + std::to_wstring(bit) + L".tdreq");
        const auto perFeatureOutput = fixture.directory / (L"feature-" + std::to_wstring(bit) + L".tdact");
        WriteNewFile(perFeatureRequest, fixture.Request(false)); auto exact = policy; exact.features = mask;
        Generate(fixture.product,fixture.store, perFeatureRequest, Fixture::Password, exact, perFeatureOutput);
        const auto selected = VerifyCertificate(fixture.DecryptPackage(perFeatureOutput), Fixture::IssuerId, fixture.issuer.Public());
        Check(selected.features == mask && HasFeature(selected.features, descriptor.feature), "Single catalog grant roundtrip");
    }

    Fixture retry(directory / L"retry");
    Reject([&] { Generate(retry.product,retry.store, retry.requestPath, Fixture::Password, policy, retry.directory / L"missing-directory" / L"out.tdact"); }, "Output failure surfaced");
    const auto retried = Generate(retry.product,retry.store, retry.requestPath, Fixture::Password, policy, retry.directory / L"recovered.tdact");
    Check(retried.reexported, "Ledger committed before failed export and retry recovers"); retry.DecryptPackage(retried.outputPath);

    Fixture concurrent(directory / L"concurrent");
    const auto firstPath = concurrent.directory / L"first.tdact", secondPath = concurrent.directory / L"second.tdact";
    auto firstAttempt = std::async(std::launch::async, [&] { return Generate(concurrent.product,concurrent.store, concurrent.requestPath, Fixture::Password, policy, firstPath); });
    auto secondAttempt = std::async(std::launch::async, [&] { return Generate(concurrent.product,concurrent.store, concurrent.requestPath, Fixture::Password, policy, secondPath); });
    const auto firstResult = firstAttempt.get(), secondResult = secondAttempt.get();
    Check(firstResult.reexported != secondResult.reexported && firstResult.licenseId == secondResult.licenseId,
        "Concurrent same request produces exactly one new issuance");
    Check(ReadFileBounded(firstPath, 65536) == ReadFileBounded(secondPath, 65536), "Concurrent outputs share exact committed package");
    concurrent.DecryptPackage(firstPath);

    Fixture signedLedger(directory / L"signed-ledger");
    Generate(signedLedger.product,signedLedger.store, signedLedger.requestPath, Fixture::Password, policy, signedLedger.directory / L"before-tamper.tdact");
    const auto ledgerDigest = HexText(Hash(ReadFileBounded(signedLedger.requestPath, 65536)));
    const auto recordPath = signedLedger.store / L"native-ledger" / (std::wstring(ledgerDigest.begin(), ledgerDigest.end()) + L".tdrec");
    auto damagedRecord = ReadFileBounded(recordPath, 32768); damagedRecord.back() ^= 1;
    std::filesystem::remove(recordPath); WriteNewFile(recordPath, damagedRecord);
    Reject([&] { Generate(signedLedger.product,signedLedger.store, signedLedger.requestPath, Fixture::Password, policy, signedLedger.directory / L"after-tamper.tdact"); },
        "Signed ledger byte tamper rejected");
    Check(!std::filesystem::exists(signedLedger.directory / L"after-tamper.tdact"), "Tampered ledger exports no package");

    Fixture trial(directory / L"trial"); const auto trialPath = trial.directory / L"trial.tdreq"; WriteNewFile(trialPath, trial.Request(true));
    auto trialPolicy = policy; trialPolicy.kind = Kind::Trial; trialPolicy.days = 1;
    Reject([&] { Generate(trial.product,trial.store, trial.requestPath, Fixture::Password, trialPolicy, trial.directory / L"wrong-kind.tdact"); }, "Trial requires NV request");
    Check(InspectRequest(trial.product,trialPath,trial.store).isTrial, "Trial evidence accepted");
    for (const auto days : {0U, 31U}) { auto invalid = trialPolicy; invalid.days = days;
        Reject([&] { Generate(trial.product,trial.store, trialPath, Fixture::Password, invalid, trial.directory / L"invalid-days.tdact"); }, "Trial duration bounded before first issuance"); }
    for (const auto initial : {std::uint64_t{0}, UINT64_MAX}) {
        const auto path = trial.directory / (L"counter-" + std::to_wstring(initial) + L".tdreq"); WriteNewFile(path, trial.Request(true, initial));
        Reject([&] { InspectRequest(trial.product,path,trial.store); }, "Signed invalid NV counter rejected");
    }
    const auto invalidIndex = trial.directory / L"invalid-index.tdreq"; WriteNewFile(invalidIndex, trial.Request(true, 50, 0x01000000));
    Reject([&] { InspectRequest(trial.product,invalidIndex,trial.store); }, "Signed invalid NV index rejected");
    const auto trialOut = trial.directory / L"trial.tdact"; Generate(trial.product,trial.store, trialPath, Fixture::Password, trialPolicy, trialOut);
    const auto tc = VerifyCertificate(trial.DecryptPackage(trialOut), Fixture::IssuerId, trial.issuer.Public());
    Check(tc.kind == Kind::Trial && tc.trialNvPublic.size() == 14 && tc.trialInitialCounter == 50 && tc.trialQuantumSeconds == 3600
        && tc.expiresAt - tc.notBefore == 86400, "NV trial fields survive native certificate verification");
    const auto trialAgain = Generate(trial.product,trial.store, trialPath, Fixture::Password, trialPolicy, trial.directory / L"trial-copy.tdact");
    Check(trialAgain.reexported, "Exact trial retry allowed");
    const auto freshTrial = trial.directory / L"fresh-trial.tdreq"; WriteNewFile(freshTrial, trial.Request(true));
    Reject([&] { Generate(trial.product,trial.store, freshTrial, Fixture::Password, trialPolicy, trial.directory / L"second-trial.tdact"); }, "New request same EK repeated trial rejected");

    Fixture untrusted(directory / L"untrusted");
    std::filesystem::rename(untrusted.store / L"manufacturer-roots" / L"test-root.cer", untrusted.directory / L"untrusted-root.cer");
    Reject([&] { InspectRequest(untrusted.product,untrusted.requestPath,untrusted.store); }, "No independent root rejected");
    Fixture changedRoot(directory / L"wrong-root");
    std::filesystem::remove(changedRoot.store / L"manufacturer-roots" / L"test-root.cer");
    WriteNewFile(changedRoot.store / L"manufacturer-roots" / L"wrong-root.cer", Certificate(fixture.root, fixture.root, true));
    Reject([&] { InspectRequest(changedRoot.product,changedRoot.requestPath,changedRoot.store); }, "Unrelated root rejected");
    Fixture badCertificate(directory / L"bad-ek-certificate"); badCertificate.leaf.back() ^= 1;
    const auto badCertificateRequest = badCertificate.directory / L"bad-certificate.tdreq"; WriteNewFile(badCertificateRequest, badCertificate.Request(false));
    Reject([&] { InspectRequest(badCertificate.product,badCertificateRequest,badCertificate.store); }, "EK certificate signature tamper rejected");
    Fixture mismatched(directory / L"private-public-mismatch");
    std::filesystem::remove(mismatched.store / L"issuer-private.pem");
    WriteNewFile(mismatched.store / L"issuer-private.pem", Pem(EncryptedPkcs8(fixture.issuer, Fixture::Password)));
    Reject([&] { Generate(mismatched.product,mismatched.store, mismatched.requestPath, Fixture::Password, policy, mismatched.directory / L"mismatch.tdact"); }, "Private public key mismatch rejected");
    Fixture oldLedger(directory / L"old-ledger");
    WriteNewFile(oldLedger.store / L"issuance.sqlite", Text("TEST ONLY prior ledger sentinel"));
    Reject([&] { Generate(oldLedger.product,oldLedger.store, oldLedger.requestPath, Fixture::Password, policy, oldLedger.directory / L"old-ledger.tdact"); }, "Prior ledger is not silently discarded");
}
void UpgradeTests(const std::filesystem::path& directory){
    Policy basePolicy;basePolicy.customer="升级测试客户";basePolicy.features=RequiredFeatures(Feature::ProductDesign);
    basePolicy.minMajor=1;basePolicy.maxMajor=2;
    auto nextPolicy=basePolicy;nextPolicy.features|=static_cast<std::uint32_t>(Feature::ProductBreakdown);
    auto finalPolicy=nextPolicy;finalPolicy.features|=static_cast<std::uint32_t>(Feature::ProductExport);
    Fixture fixture(directory/L"upgrade");const auto activation=fixture.directory/L"base.tdact";
    Generate(fixture.product,fixture.store,fixture.requestPath,Fixture::Password,basePolicy,activation);
    const auto baseFile=fixture.DecryptPackage(activation),publicKey=fixture.issuer.Public();
    const auto base=VerifyCertificate(baseFile,Fixture::IssuerId,publicKey);
    const auto certificatePath=fixture.directory/L"base.tdlic";WriteNewFile(certificatePath,baseFile);
    const auto original=InspectAuthorization(fixture.product,activation,fixture.store),fromCertificate=InspectAuthorization(fixture.product,certificatePath,fixture.store);
    Check(original.licenseId==base.licenseId&&original.customerId==base.customerId&&original.requestId==base.requestId
        &&original.policy.customer==basePolicy.customer&&original.policy.features==basePolicy.features
        &&fromCertificate.licenseId==original.licenseId&&!original.hasNewerAuthorization,"Original activation and installed certificate inspect without TPM");
    Check(!std::filesystem::exists(fixture.store/L"native-upgrades"),"Inspection does not create upgrade history");
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,basePolicy,fixture.directory/L"noop.tdact");},"No-op upgrade rejected");
    auto invalid=nextPolicy;invalid.customer+="changed";
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,invalid,fixture.directory/L"customer.tdact");},"Upgrade cannot change customer");
    invalid=nextPolicy;invalid.kind=Kind::Trial;
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,invalid,fixture.directory/L"kind.tdact");},"Upgrade cannot change authorization type");
    invalid=nextPolicy;invalid.minMajor=2;
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,invalid,fixture.directory/L"version.tdact");},"Upgrade cannot narrow version range");
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,activation,"wrong-password",nextPolicy,fixture.directory/L"password.tdact");},"Wrong upgrade signing password rejected");
    const auto nextPath=fixture.directory/L"first.tdact";const auto issued=GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,nextPolicy,nextPath);
    const auto package=ReadFileBounded(nextPath,16384),nextFile=ResolveUpgradeCertificate(package,baseFile,Fixture::IssuerId,publicKey);
    const auto next=VerifyCertificate(nextFile,Fixture::IssuerId,publicKey);
    Check(!issued.reexported&&issued.licenseId==base.licenseId&&next.licenseId==base.licenseId&&next.customerId==base.customerId
        &&next.requestId==base.requestId&&next.devicePublicKey==base.devicePublicKey&&next.features==nextPolicy.features,
        "Upgrade adds function 2 and preserves function 1 and the original lineage");
    Check(InspectAuthorization(fixture.product,nextPath,fixture.store).policy.features==nextPolicy.features
        &&InspectAuthorization(fixture.product,activation,fixture.store).hasNewerAuthorization,"Upgrade inspection exposes exact policy and superseded original");
    const auto nextCertificatePath=fixture.directory/L"next.tdlic";WriteNewFile(nextCertificatePath,nextFile);
    Check(InspectAuthorization(fixture.product,nextCertificatePath,fixture.store).policy.features==nextPolicy.features,"Installed upgraded certificate is usable for further issuance");
    const auto copy=fixture.directory/L"copy.tdact";const auto repeated=GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,nextPolicy,copy);
    Check(repeated.reexported&&ReadFileBounded(copy,16384)==package,"Same original and policy reexport exact committed upgrade");
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,finalPolicy,fixture.directory/L"fork.tdact");},"Superseded base cannot create another branch");
    const auto finalPath=fixture.directory/L"second.tdact";GenerateUpgrade(fixture.product,fixture.store,nextPath,Fixture::Password,finalPolicy,finalPath);
    const auto finalFile=ResolveUpgradeCertificate(ReadFileBounded(finalPath,16384),nextFile,Fixture::IssuerId,publicKey);
    Check(VerifyCertificate(finalFile,Fixture::IssuerId,publicKey).features==finalPolicy.features,"Successor file permits a second incremental upgrade");
    Reject([&]{ResolveUpgradeCertificate(ReadFileBounded(finalPath,16384),baseFile,Fixture::IssuerId,publicKey);},"Second upgrade cannot skip its exact predecessor");
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,finalPath,Fixture::Password,nextPolicy,fixture.directory/L"downgrade.tdact");},"Newest authorization cannot lose permissions");
    Check(GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,nextPolicy,fixture.directory/L"copy-after-second.tdact").reexported,
        "Old committed upgrade can still reexport after later upgrade");
    auto damaged=package;damaged.back()^=1;const auto damagedPath=fixture.directory/L"damaged-upgrade.tdact";WriteNewFile(damagedPath,damaged);
    Reject([&]{InspectAuthorization(fixture.product,damagedPath,fixture.store);},"Tampered upgrade package rejected on inspection");
    damaged=ReadFileBounded(activation,16384);damaged.back()^=1;const auto damagedActivation=fixture.directory/L"damaged.tdact";WriteNewFile(damagedActivation,damaged);
    Reject([&]{InspectAuthorization(fixture.product,damagedActivation,fixture.store);},"Tampered original activation cannot match trusted history");
    Fixture foreign(directory/L"foreign-upgrade");
    Reject([&]{InspectAuthorization(foreign.product,nextPath,foreign.store);},"Other issuer store cannot use an upgrade");

    Fixture retry(directory/L"upgrade-retry");const auto retryActivation=retry.directory/L"base.tdact";
    Generate(retry.product,retry.store,retry.requestPath,Fixture::Password,basePolicy,retryActivation);
    Reject([&]{GenerateUpgrade(retry.product,retry.store,retryActivation,Fixture::Password,nextPolicy,retry.directory/L"missing"/L"first.tdact");},"Upgrade output failure surfaced");
    Check(GenerateUpgrade(retry.product,retry.store,retryActivation,Fixture::Password,nextPolicy,retry.directory/L"recovered.tdact").reexported,
        "Upgrade ledger commits before export and retry recovers exact package");

    Fixture concurrent(directory/L"upgrade-concurrent");const auto concurrentActivation=concurrent.directory/L"base.tdact";
    Generate(concurrent.product,concurrent.store,concurrent.requestPath,Fixture::Password,basePolicy,concurrentActivation);
    const auto first=concurrent.directory/L"first.tdact",second=concurrent.directory/L"second.tdact";
    auto firstAttempt=std::async(std::launch::async,[&]{return GenerateUpgrade(concurrent.product,concurrent.store,concurrentActivation,Fixture::Password,nextPolicy,first);});
    auto secondAttempt=std::async(std::launch::async,[&]{return GenerateUpgrade(concurrent.product,concurrent.store,concurrentActivation,Fixture::Password,nextPolicy,second);});
    const auto firstResult=firstAttempt.get(),secondResult=secondAttempt.get();
    Check(firstResult.reexported!=secondResult.reexported&&ReadFileBounded(first,16384)==ReadFileBounded(second,16384),
        "Concurrent upgrade produces one committed successor and exact repeated export");

    Fixture trial(directory/L"upgrade-trial");const auto trialRequest=trial.directory/L"trial.tdreq",trialActivation=trial.directory/L"trial.tdact";
    WriteNewFile(trialRequest,trial.Request(true));auto trialPolicy=basePolicy;trialPolicy.kind=Kind::Trial;trialPolicy.days=1;
    Generate(trial.product,trial.store,trialRequest,Fixture::Password,trialPolicy,trialActivation);const auto trialBase=trial.DecryptPackage(trialActivation);
    auto trialNextPolicy=trialPolicy;trialNextPolicy.features=nextPolicy.features;const auto trialUpgrade=trial.directory/L"trial-upgrade.tdact";
    GenerateUpgrade(trial.product,trial.store,trialActivation,Fixture::Password,trialNextPolicy,trialUpgrade);
    const auto trialOriginal=VerifyCertificate(trialBase,Fixture::IssuerId,trial.issuer.Public());
    const auto trialNextFile=ResolveUpgradeCertificate(ReadFileBounded(trialUpgrade,16384),trialBase,Fixture::IssuerId,trial.issuer.Public(),trial.product);
    const auto trialNext=VerifyCertificateForProduct(trialNextFile,trial.product,Fixture::IssuerId,trial.issuer.Public());
    Check(trialNext.features==trialNextPolicy.features&&trialNext.issuedAt==trialOriginal.issuedAt
        &&trialNext.notBefore==trialOriginal.notBefore&&trialNext.expiresAt==trialOriginal.expiresAt
        &&trialNext.trialNvPublic==trialOriginal.trialNvPublic&&trialNext.trialInitialCounter==trialOriginal.trialInitialCounter
        &&trialNext.trialQuantumSeconds==trialOriginal.trialQuantumSeconds,"Trial gains permissions without restarting its time or NV counter");
    trialNextPolicy.features=finalPolicy.features;trialNextPolicy.days=2;
    Reject([&]{GenerateUpgrade(trial.product,trial.store,trialUpgrade,Fixture::Password,trialNextPolicy,trial.directory/L"extended.tdact");},"Upgrade cannot extend trial duration");

    auto permanentPolicy=trialNextPolicy;permanentPolicy.kind=Kind::Permanent;permanentPolicy.days=30;
    const auto permanentPath=trial.directory/L"permanent.tdact";
    const auto converted=GenerateUpgrade(trial.product,trial.store,trialUpgrade,Fixture::Password,permanentPolicy,permanentPath);
    const auto convertedPackage=ReadFileBounded(permanentPath,16384);
    const auto convertedFile=ResolveUpgradeCertificate(convertedPackage,trialNextFile,Fixture::IssuerId,trial.issuer.Public(),trial.product);
    const auto permanent=VerifyCertificateForProduct(convertedFile,trial.product,Fixture::IssuerId,trial.trustedPublic);
    Check(!converted.reexported&&permanent.kind==Kind::Permanent&&permanent.notBefore==0&&permanent.expiresAt==0
        &&permanent.trialNvPublic.empty()&&permanent.trialInitialCounter==0&&permanent.trialQuantumSeconds==0
        &&permanent.devicePublicKey==trialOriginal.devicePublicKey&&permanent.licenseId==trialOriginal.licenseId
        &&permanent.features==permanentPolicy.features,"Trial converts to permanent with exact device and license lineage");
    Check(InspectAuthorization(trial.product,permanentPath,trial.store).policy.kind==Kind::Permanent,
        "Converted authorization resolves through signed upgrade history");
    const auto convertedCopy=trial.directory/L"permanent-copy.tdact";
    Check(GenerateUpgrade(trial.product,trial.store,trialUpgrade,Fixture::Password,permanentPolicy,convertedCopy).reexported
        &&ReadFileBounded(convertedCopy,16384)==convertedPackage,"Trial conversion retry returns identical committed authorization");
    Reject([&]{GenerateUpgrade(trial.product,trial.store,permanentPath,Fixture::Password,trialNextPolicy,trial.directory/L"back-to-trial.tdact");},
        "Converted permanent authorization cannot become trial again");
    Reject([&]{GenerateUpgrade(trial.product,trial.store,trialActivation,Fixture::Password,permanentPolicy,trial.directory/L"stale-conversion.tdact");},
        "An old trial predecessor cannot bypass a newer upgrade to become permanent");

    const auto digest=HexText(base.bodyDigest);const auto recordPath=fixture.store/L"native-upgrades"/(std::wstring(digest.begin(),digest.end())+L".tdurec");
    auto corruptedRecord=ReadFileBounded(recordPath,32768);corruptedRecord.back()^=1;std::filesystem::remove(recordPath);WriteNewFile(recordPath,corruptedRecord);
    Reject([&]{InspectAuthorization(fixture.product,nextPath,fixture.store);},"Tampered signed upgrade ledger fails inspection");
    Reject([&]{GenerateUpgrade(fixture.product,fixture.store,nextPath,Fixture::Password,finalPolicy,fixture.directory/L"corrupted.tdact");},"Tampered upgrade ledger cannot issue further packages");
    const auto originalRequestDigest=HexText(Hash(ReadFileBounded(retry.requestPath,65536)));
    std::filesystem::remove(retry.store/L"native-ledger"/(std::wstring(originalRequestDigest.begin(),originalRequestDigest.end())+L".tdrec"));
    Reject([&]{InspectAuthorization(retry.product,retry.directory/L"recovered.tdact",retry.store);},"Upgrade history without original issuance is rejected");
}
void ProductTests(const std::filesystem::path& directory){
    constexpr std::array<FeatureDescriptor,3> selectedFeatures{{
        {Feature::PageProduct,"page.workspace","测试工作区",0},
        {Feature::ProductDesign,"workspace.create","创建",1},
        {Feature::ProductBreakdown,"workspace.process","处理",1},
    }};
    auto selected=TubeDesignerProduct;selected.id="test-only.second-product";selected.label="Synthetic second product";
    selected.features=selectedFeatures;selected.certificateFormat="XPCERT01";
    selected.permanentRequestFormat="XPREQ001";selected.trialRequestFormat="XPREQ002";
    selected.attestationPrimaryDomain="TestOnly.SecondProduct.AttestationPrimary.v1";
    Fixture fixture(directory/L"second-product",selected);
    Check(KnownProductFeatures(fixture.product)==49&&IsValidFeatureSet(fixture.product,17),"Selected product owns its exact permission catalog");
    Check(InspectRequest(fixture.product,fixture.requestPath,fixture.store).issuerId==Fixture::IssuerId,"Second product request uses its own format and AK policy");
    Policy policy;policy.customer="Synthetic multi-product customer";policy.features=17;policy.minMajor=1;policy.maxMajor=3;
    const auto activation=fixture.directory/L"second-product.tdact";
    Generate(fixture.product,fixture.store,fixture.requestPath,Fixture::Password,policy,activation);
    const auto certificate=fixture.DecryptPackage(activation);
    const auto parsed=VerifyCertificateForProduct(certificate,fixture.product,Fixture::IssuerId,fixture.trustedPublic);
    Check(parsed.product==fixture.product.id&&parsed.features==17,"Generated authorization is bound to selected product and catalog");
    auto wrongProduct=fixture.product;wrongProduct.id=TubeDesignerProduct.id;
    Reject([&]{InspectRequest(wrongProduct,fixture.requestPath,fixture.store);},"Same key does not allow enrollment for another product");
    Reject([&]{InspectAuthorization(wrongProduct,activation,fixture.store);},"Same key does not allow activation for another product");
    auto wrongPolicy=policy;wrongPolicy.features|=64;
    Reject([&]{Generate(fixture.product,fixture.store,fixture.requestPath,Fixture::Password,wrongPolicy,fixture.directory/L"foreign-feature.tdact");},
        "Foreign product feature cannot enter a selected product license");
    auto wrongDomain=fixture.product;wrongDomain.attestationPrimaryDomain=TubeDesignerProduct.attestationPrimaryDomain;
    Reject([&]{InspectRequest(wrongDomain,fixture.requestPath,fixture.store);},"Selected product AK policy cannot use another product domain");
    Reject([&]{InspectStore(TubeDesignerProduct,fixture.store);},"Store key is checked against compiled product trust");
    auto nextPolicy=policy;nextPolicy.features=49;const auto upgradePath=fixture.directory/L"second-product-upgrade.tdact";
    GenerateUpgrade(fixture.product,fixture.store,activation,Fixture::Password,nextPolicy,upgradePath);
    const auto upgraded=ResolveUpgradeCertificate(ReadFileBounded(upgradePath,16384),certificate,Fixture::IssuerId,fixture.trustedPublic,fixture.product);
    Check(VerifyCertificateForProduct(upgraded,fixture.product,Fixture::IssuerId,fixture.trustedPublic).features==49,
        "Second product encrypted authorization upgrades through shared signed protocol");
    Reject([&]{InspectAuthorization(wrongProduct,upgradePath,fixture.store);},"Signed upgrade cannot cross product boundary with the same issuer key");
    Fixture other(directory/L"other-product-device",selected);
    auto otherDevice=parsed;otherDevice.devicePublicKey=other.ak.Public();
    const auto otherCertificate=SignedCertificate(otherDevice,fixture.product,fixture.issuer);
    Reject([&]{ResolveUpgradeCertificate(ReadFileBounded(upgradePath,16384),otherCertificate,Fixture::IssuerId,fixture.trustedPublic,fixture.product);},
        "Upgrade cannot be applied to a valid same-issuer certificate for another device");
}
void BundleTests(const std::filesystem::path& directory){
    constexpr std::array<FeatureDescriptor,3> features{{
        {Feature::PageProduct,"page.workspace","工作区",0},{Feature::ProductDesign,"workspace.create","创建",1},
        {Feature::ProductBreakdown,"workspace.process","处理",1}}};
    auto secondProduct=TubeDesignerProduct;secondProduct.id="test-only.bundle-product";secondProduct.features=features;
    secondProduct.certificateFormat="XPCERT01";secondProduct.permanentRequestFormat="XPREQ001";secondProduct.trialRequestFormat="XPREQ002";
    secondProduct.attestationPrimaryDomain="TestOnly.BundleProduct.AttestationPrimary.v1";
    const auto shareEk=[](const Fixture& first,Fixture& second){
        second.ekArea=first.ekArea;second.leaf=first.leaf;
        std::filesystem::remove(second.store/L"manufacturer-roots"/L"test-root.cer");
        WriteNewFile(second.store/L"manufacturer-roots"/L"test-root.cer",ReadFileBounded(first.store/L"manufacturer-roots"/L"test-root.cer",16384));
        std::filesystem::remove(second.requestPath);WriteNewFile(second.requestPath,second.Request(false));
    };
    Fixture first(directory/L"bundle-first"),second(directory/L"bundle-second",secondProduct);shareEk(first,second);
    Policy initial;initial.customer="统一客户";initial.features=17;initial.minMajor=1;initial.maxMajor=2;
    std::vector<BundleInput> inputs{{&first.product,first.store,Text(Fixture::Password),first.requestPath,initial},
        {&second.product,second.store,Text(Fixture::Password),second.requestPath,initial}};
    std::array<BundleStoreRef,2> stores{{{&first.product,first.store},{&second.product,second.store}}};
    auto invalidInputs=inputs;invalidInputs[1].password=Text("wrong-password");
    Reject([&]{GenerateBundle(invalidInputs,std::nullopt,first.directory/L"wrong-password.tdact");},"All bundle keys are checked before any product is issued");
    Check(!std::filesystem::exists(first.store/L"native-ledger")&&!std::filesystem::exists(second.store/L"native-ledger"),
        "Wrong later product password leaves both product ledgers untouched");
    invalidInputs=inputs;invalidInputs[1].policy.customer="另一客户";
    Reject([&]{GenerateBundle(invalidInputs,std::nullopt,first.directory/L"other-customer.tdact");},"Bundle products require the same customer name");
    const auto originalPath=first.directory/L"combined.tdact";const auto generated=GenerateBundle(inputs,std::nullopt,originalPath);
    const auto originalBytes=ReadFileBounded(originalPath,static_cast<DWORD>(MaxAuthorizationFileBytes));const auto original=ParseAuthorizationBundle(originalBytes);
    Check(!generated.reexported&&original.slots.size()==2&&original.bundleId==generated.licenseId,"One authorization file contains two independently signed products");
    Check(InspectBundle(originalPath,stores).products.size()==2,"Bundle inspection resolves both exact policies through trusted ledgers");
    const auto copyPath=first.directory/L"combined-copy.tdact";
    Check(GenerateBundle(inputs,std::nullopt,copyPath).reexported&&ReadFileBounded(copyPath,static_cast<DWORD>(MaxAuthorizationFileBytes))==originalBytes,
        "Identical bundle issuance reexports exact cached signed bytes");
    const auto clientFile=[&](const AuthorizationBundle& bundle,const Fixture& fixture){
        const auto& slot=FindBundleSlot(bundle,fixture.product.id);const auto root=fixture.directory/(L"client-root-"+std::to_wstring(checks)+L".tdact");
        WriteNewFile(root,slot.chain.front());auto certificate=fixture.DecryptPackage(root,&first.ek);
        for(std::size_t i=1;i<slot.chain.size();++i)certificate=ResolveUpgradeCertificate(slot.chain[i],certificate,Fixture::IssuerId,fixture.trustedPublic,fixture.product);
        return certificate;
    };
    const auto firstCertificate=clientFile(original,first),secondCertificate=clientFile(original,second);
    Check(VerifyCertificateForProduct(firstCertificate,first.product,Fixture::IssuerId,first.trustedPublic).features==17
        &&VerifyCertificateForProduct(secondCertificate,second.product,Fixture::IssuerId,second.trustedPublic).features==17,
        "A fresh client can activate either product using its distinct AK on the common EK");
    inputs[0].policy.features=49;const auto upgradePath=first.directory/L"combined-upgrade.tdact";
    GenerateBundle(inputs,originalPath,upgradePath);const auto upgradedBytes=ReadFileBounded(upgradePath,static_cast<DWORD>(MaxAuthorizationFileBytes));
    const auto upgraded=ParseAuthorizationBundle(upgradedBytes);
    Check(FindBundleSlot(upgraded,first.product.id).chain.size()==2
        &&FindBundleSlot(upgraded,second.product.id).chain==FindBundleSlot(original,second.product.id).chain,
        "Upgrading one product retains every byte of the other product payload chain");
    VerifyAuthorizationBundleForProduct(upgradedBytes,second.product,Fixture::IssuerId,second.trustedPublic);
    inputs[0].policy.features=113;const auto finalPath=first.directory/L"combined-final.tdact";
    GenerateBundle(inputs,upgradePath,finalPath);const auto finalBytes=ReadFileBounded(finalPath,static_cast<DWORD>(MaxAuthorizationFileBytes));const auto finalBundle=ParseAuthorizationBundle(finalBytes);
    const auto latestFirst=clientFile(finalBundle,first);
    Check(FindBundleSlot(finalBundle,first.product.id).chain.size()==3
        &&VerifyCertificateForProduct(latestFirst,first.product,Fixture::IssuerId,first.trustedPublic).features==113,
        "The latest bundle contains every predecessor so first import can skip multiple delivered generations");
    Check(FindBundleSlot(finalBundle,second.product.id).chain==FindBundleSlot(original,second.product.id).chain,
        "Multiple upgrades never remove or alter the other product authorization");
    const auto finalCopy=first.directory/L"combined-final-copy.tdact";
    Check(GenerateBundle(inputs,upgradePath,finalCopy).reexported&&ReadFileBounded(finalCopy,static_cast<DWORD>(MaxAuthorizationFileBytes))==finalBytes,
        "Interrupted or repeated bundle upgrade recovers exact manifest signatures and payloads");
    Reject([&]{GenerateBundle(inputs,originalPath,first.directory/L"stale.tdact");},"An old bundle cannot fork a product upgrade chain");
    Reject([&]{GenerateBundle(std::span(inputs).first(1),finalPath,first.directory/L"removed-product.tdact");},"Bundle upgrades cannot omit another existing product");
    auto downgradeInputs=inputs;downgradeInputs[0].policy.features=17;
    Reject([&]{GenerateBundle(downgradeInputs,finalPath,first.directory/L"downgrade.tdact");},"Bundle upgrades cannot remove granted features");
    Reject([&]{GenerateBundle(inputs,upgradePath,finalPath);},"Bundle output creation never overwrites an existing file");
    Check(ReadFileBounded(finalPath,static_cast<DWORD>(MaxAuthorizationFileBytes))==finalBytes,"Collision leaves existing multi-product file intact");
    const auto installed=first.directory/L"latest.tdlic";WriteNewFile(installed,latestFirst);
    auto single=inputs[0];single.sourcePath=installed;const auto recoveredPath=first.directory/L"recovered-from-installed.tdact";
    GenerateBundle(std::span(&single,1),std::nullopt,recoveredPath);
    Check(FindBundleSlot(ParseAuthorizationBundle(ReadFileBounded(recoveredPath,static_cast<DWORD>(MaxAuthorizationFileBytes))),first.product.id).chain.size()==3,
        "An installed latest certificate recovers its original root and all signed upgrades from the ledger");
    auto tampered=finalBytes;tampered.back()^=1;
    Reject([&]{InspectBundle(tampered,stores);},"Tampering any product manifest signature rejects bundle inspection");
    auto omitted=finalBundle;omitted.slots.pop_back();
    Reject([&]{VerifyAuthorizationBundleForProduct(EncodeAuthorizationBundle(omitted),first.product,Fixture::IssuerId,first.trustedPublic);},
        "A removed product invalidates signatures over the entire manifest");
    Fixture unrelated(directory/L"bundle-wrong-device",secondProduct);
    auto wrongDeviceInputs=inputs;wrongDeviceInputs[1]={&unrelated.product,unrelated.store,Text(Fixture::Password),unrelated.requestPath,initial};
    Reject([&]{GenerateBundle(wrongDeviceInputs,std::nullopt,first.directory/L"mixed-device.tdact");},"Two product AK requests with different EKs cannot share one authorization");
    Check(!std::filesystem::exists(unrelated.store/L"native-ledger"),"Different-device bundle rejection produces no issuance");
    Fixture retryFirst(directory/L"bundle-retry-first"),retrySecond(directory/L"bundle-retry-second",secondProduct);shareEk(retryFirst,retrySecond);
    std::array<BundleInput,2> retries{{{&retryFirst.product,retryFirst.store,Text(Fixture::Password),retryFirst.requestPath,initial},
        {&retrySecond.product,retrySecond.store,Text(Fixture::Password),retrySecond.requestPath,initial}}};
    Reject([&]{GenerateBundle(retries,std::nullopt,retryFirst.directory/L"missing-directory"/L"bundle.tdact");},"Bundle output failure is surfaced after durable cache commit");
    const auto recovered=GenerateBundle(retries,std::nullopt,retryFirst.directory/L"retry.tdact");
    Check(recovered.reexported&&std::filesystem::exists(retryFirst.store/L"native-ledger")&&std::filesystem::exists(retrySecond.store/L"native-ledger"),
        "Retry after export failure reuses both committed authorizations and the exact signed bundle cache");
    Fixture partialFirst(directory/L"bundle-partial-first"),partialSecond(directory/L"bundle-partial-second",secondProduct);shareEk(partialFirst,partialSecond);
    const auto committedPath=partialFirst.directory/L"committed-before-interruption.tdact";
    const auto committed=Generate(partialFirst.product,partialFirst.store,partialFirst.requestPath,Fixture::Password,initial,committedPath);
    const auto committedBytes=ReadFileBounded(committedPath,16384);
    std::array<BundleInput,2> partial{{{&partialFirst.product,partialFirst.store,Text(Fixture::Password),partialFirst.requestPath,initial},
        {&partialSecond.product,partialSecond.store,Text(Fixture::Password),partialSecond.requestPath,initial}}};
    const auto partialPath=partialFirst.directory/L"resumed.tdact";GenerateBundle(partial,std::nullopt,partialPath);
    const auto resumed=ParseAuthorizationBundle(ReadFileBounded(partialPath,static_cast<DWORD>(MaxAuthorizationFileBytes)));
    Check(FindBundleSlot(resumed,partialFirst.product.id).chain.front()==committedBytes
        &&InspectBundle(partialPath,std::array<BundleStoreRef,2>{{{&partialFirst.product,partialFirst.store},{&partialSecond.product,partialSecond.store}}}).products[0].licenseId==committed.licenseId,
        "Recovery from a partly committed bundle preserves the first product authorization and only issues the missing product");
    Fixture concurrentFirst(directory/L"bundle-concurrent-first"),concurrentSecond(directory/L"bundle-concurrent-second",secondProduct);shareEk(concurrentFirst,concurrentSecond);
    std::array<BundleInput,2> concurrentInputs{{{&concurrentFirst.product,concurrentFirst.store,Text(Fixture::Password),concurrentFirst.requestPath,initial},
        {&concurrentSecond.product,concurrentSecond.store,Text(Fixture::Password),concurrentSecond.requestPath,initial}}};
    const std::array<BundleInput,2> reverseInputs{{concurrentInputs[1],concurrentInputs[0]}};
    const auto concurrentA=concurrentFirst.directory/L"concurrent-a.tdact",concurrentB=concurrentFirst.directory/L"concurrent-b.tdact";
    auto attemptA=std::async(std::launch::async,[&]{return GenerateBundle(concurrentInputs,std::nullopt,concurrentA);});
    auto attemptB=std::async(std::launch::async,[&]{return GenerateBundle(reverseInputs,std::nullopt,concurrentB);});
    const auto resultA=attemptA.get(),resultB=attemptB.get();
    Check(resultA.reexported!=resultB.reexported&&resultA.licenseId==resultB.licenseId
        &&ReadFileBounded(concurrentA,static_cast<DWORD>(MaxAuthorizationFileBytes))==ReadFileBounded(concurrentB,static_cast<DWORD>(MaxAuthorizationFileBytes)),
        "Concurrent bundles with reversed product input order lock consistently and produce one exact cached result");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--create-fixture") {
            // Explicit acceptance fixture output. Entirely new directory with
            // artificial, publicly known test credentials; never a live store.
            Fixture fixture(std::filesystem::absolute(argv[2]));
            WriteNewFile(fixture.directory / L"trial.tdreq", fixture.Request(true));
            WriteNewFile(fixture.directory / L"TEST-ONLY", Text("Synthetic software crypto fixture. No hardware enrollment.\n"));
            std::cout << "native_crypto_fixture=created\ntpm_hardware=not_exercised\n"; return 0;
        }
        Require(argc == 1, "Expected no arguments or --create-fixture <new-directory>");
        OwnedTemp temporary; Tests(temporary.path); UpgradeTests(temporary.path); ProductTests(temporary.path); BundleTests(temporary.path);
        std::cout << "native_issuer_checks=" << checks << "\ncrypto_interop=passed\ntpm_hardware=not_exercised\nproduction_state=not_accessed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << " (after " << checks << " checks)\n"; return 1; }
}
