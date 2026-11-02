// Explicit read-only hardware/network acceptance. Never linked into a product.
#include "EnrollmentRepair.h"
#include <iostream>
using namespace tube::license;
int wmain(int argc, wchar_t** argv) {
    try {
        // Explicit public reference vector: exercise the exact production HTTP
        // path without accessing a TPM, a license, or a certificate cache.
        if (argc == 4 && std::wstring_view(argv[1]) == L"--public-reference") {
            const auto area = ReadFileBounded(argv[2], 1024);
            const auto response = DownloadIntelEnrollmentResponse(area);
            WriteNewFile(argv[3], Bytes(response.begin(), response.end()), 24576);
            std::cout << "http=200\nresponse_bytes=" << response.size()
                << "\ntpm_accessed=false\ncertificate_cache_written=false\n";
            return 0;
        }
        Require(argc == 2 && std::wstring_view(argv[1]) == L"--intel-network", "Expected --intel-network");
        tpm::Context context;
        const auto vendor = EnrollmentTpmManufacturer(context);
        std::cout << "manufacturer=" << std::hex << vendor << std::dec << '\n';
        Require(vendor == 0x494e5443, "Network probe is for Intel TPM only");
        tpm::Object ek(context, 0x4000000b, tpm::RsaEkTemplate());
        const auto response = DownloadIntelEnrollmentResponse(ek.publicArea);
        std::cout << "http=200\nresponse_bytes=" << response.size() << "\ncertificate_saved=false\n";
        return 0;
    } catch (const EnrollmentRepairError& error) {
        std::cout << "code=" << error.code << "\nmessage=" << error.what() << '\n'; return 2;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
