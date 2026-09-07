#include "TpmTransport.h"
#include "Enrollment.h"
#include "LicenseFiles.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace tube::license;
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--trial-time-test") {
            tpm::Context ctx; tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate()); std::uint32_t owned{};
            try {
                const auto trial = NewTrialCounter(ctx, ak, RandomChallenge(), owned);
                const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                Certificate c; c.kind = Kind::Trial; c.trialNvPublic = trial.area; c.trialInitialCounter = trial.initial;
                c.trialQuantumSeconds = 3600; c.notBefore = now - 7200; c.expiresAt = now + 86400;
                EnforceTrial(ctx, ak, c);
                const auto count = CertifiedTrialCounter(ctx, ak, trial.area, trial.initial);
                Require(count == trial.initial + 2, "Trial watermark failed to advance");
                Require(PlanTrialTime(c, count, now).target == count, "Trial performs unnecessary writes");
                bool rollback = false; try { PlanTrialTime(c, count, c.notBefore + 1); } catch (...) { rollback = true; }
                Require(rollback, "Trial clock rollback accepted");
                RemoveNewTrialCounter(ctx, owned); owned = 0;
                std::cout << "trial_time_advance=passed\ntrial_clock_rollback=rejected\ntrial_no_per_click_write=passed\ntest_index_removed=true\n";
                return 0;
            } catch (...) {
                if (owned) { try { RemoveNewTrialCounter(ctx, owned); } catch (...) { std::cerr << "Owned test NV cleanup failed: " << owned << '\n'; } }
                throw;
            }
        }
        if (argc == 4 && std::wstring_view(argv[1]) == L"--activation-package-test") {
            const auto pub = ReadFileBounded(argv[2], 72), package = ReadFileBounded(argv[3], 16384);
            const auto certificate = DecryptActivation(package, "hardware-test-only", pub);
            VerifyTpmAtSite<9001, Feature::Design>(certificate, "hardware-test-only", pub, 0);
            auto tampered = package; tampered.back() ^= 1;
            bool rejected = false; try { DecryptActivation(tampered, "hardware-test-only", pub); } catch (...) { rejected = true; }
            Require(rejected, "Tampered activation accepted");
            std::cout << "activation_decrypt=passed\ncertificate_verify=passed\nper_site_tpm_proof=passed\ntampered_activation=rejected\n";
            return 0;
        }
        Require(argc == 3, "Usage: td-license-enrollment-probe --public output | --activate credential-file");
        tpm::Context context;
        tpm::Object ak(context, 0x40000001, tpm::AkTemplate());
        const auto nonce = RandomChallenge(); const auto quote = tpm::Quote(context, ak, nonce);
        tpm::VerifyQuote(ak.publicArea, ak.qualifiedName, nonce, quote);
        std::cout << "restricted_ak_quote=passed\n";
        tpm::Object ek(context, 0x4000000b, tpm::RsaEkTemplate());
        if (std::wstring_view(argv[1]) == L"--public") {
            Bytes output; AppendField(output, ak.publicArea); AppendField(output, ak.qualifiedName); AppendField(output, ek.publicArea);
            Require(!std::filesystem::exists(argv[2]), "Refuse overwrite");
            std::ofstream file(std::filesystem::path(argv[2]), std::ios::binary);
            file.write(reinterpret_cast<const char*>(output.data()), output.size()); Require(file.good(), "Write failed");
            std::cout << "public_areas_exported=true\n";
        } else if (std::wstring_view(argv[1]) == L"--activate") {
            std::ifstream file(std::filesystem::path(argv[2]), std::ios::binary);
            Bytes data(4097); file.read(reinterpret_cast<char*>(data.data()), data.size()); data.resize(static_cast<std::size_t>(file.gcount()));
            Require(!data.empty() && data.size() <= 4096, "Invalid credential file");
            Reader r(data); const auto credential = r.Field(1024), secret = r.Field(1024), expected = r.Field(32);
            Require(r.End() && expected.size() == 32, "Invalid test fields");
            const auto actual = tpm::Activate(context, ak, ek, credential, secret);
            Require(actual == expected, "Credential activation mismatch");
            std::cout << "credential_activation=passed\n";
        } else throw std::runtime_error("Unknown command");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
