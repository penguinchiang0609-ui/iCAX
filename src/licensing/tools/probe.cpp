#include "LicenseCore.h"
#include <tbs.h>
#include <iostream>

namespace {
struct TbsContext {
    TBS_HCONTEXT value{};
    ~TbsContext() { if (value) Tbsip_Context_Close(value); }
};
void ProbeNvPrerequisites() {
    TBS_CONTEXT_PARAMS2 parameters{};
    parameters.version = TBS_CONTEXT_VERSION_TWO;
    parameters.includeTpm20 = 1;
    TbsContext context;
    const auto status = Tbsi_Context_Create(reinterpret_cast<TBS_CONTEXT_PARAMS*>(&parameters), &context.value);
    std::cout << "nv_context_status=" << status << '\n';
    if (status != TBS_SUCCESS) return;
    // Size query only: never retrieve or print hierarchy authorization secrets.
    UINT32 size = 0;
    const auto authStatus = Tbsi_Get_OwnerAuth(context.value, TBS_OWNERAUTH_TYPE_STORAGE_20, nullptr, &size);
    std::cout << "storage_auth_query_status=0x" << std::hex << authStatus << std::dec << '\n';
    // TPM2_GetCapability(TPM_CAP_TPM_PROPERTIES, TPM_PT_PERMANENT, 1).
    // Read-only metadata: reveals whether storage hierarchy auth is set.
    const unsigned char command[] = {0x80,0x01,0,0,0,22,0,0,0x01,0x7a,
        0,0,0,6,0,0,2,0,0,0,0,1};
    std::array<unsigned char, 128> response{};
    UINT32 responseSize = static_cast<UINT32>(response.size());
    const auto readStatus = Tbsip_Submit_Command(context.value, TBS_COMMAND_LOCALITY_ZERO,
        TBS_COMMAND_PRIORITY_NORMAL, command, sizeof(command), response.data(), &responseSize);
    std::cout << "nv_metadata_query_status=0x" << std::hex << readStatus << std::dec << '\n';
    if (readStatus == TBS_SUCCESS && responseSize == 27) {
        tube::license::Reader reader(std::span(response).first(responseSize));
        reader.Take(2);
        const auto length = reader.U32(), rc = reader.U32();
        reader.Take(1);
        const auto capability = reader.U32(), count = reader.U32(), property = reader.U32(), value = reader.U32();
        if (length == responseSize && rc == 0 && capability == 6 && count == 1 && property == 0x200)
            std::cout << "storage_hierarchy_auth_set=" << ((value & 1) ? "true" : "false") << '\n';
    }
}
}

int main(int argc, char** argv) {
    // Default is read-only; the explicit exercise option creates a transient key.
    // Never defines NV indices, clears TPM or changes policy.
    TPM_DEVICE_INFO info{};
    const auto status = Tbsi_GetDeviceInfo(sizeof(info), &info);
    std::cout << "tbs_status=" << status << '\n';
    if (status != TBS_SUCCESS) return 2;
    std::cout << "tpm_version=" << info.tpmVersion << " interface=" << info.tpmInterfaceType << '\n';
    if (info.tpmVersion != TPM_VERSION_20) return 3;
    try {
        ProbeNvPrerequisites();
        tube::license::Provider provider;
        DWORD implementation{}, size{};
        tube::license::CngCheck(NCryptGetProperty(provider.value, NCRYPT_IMPL_TYPE_PROPERTY,
            reinterpret_cast<PBYTE>(&implementation), sizeof(implementation), &size, 0), "Read provider implementation");
        std::cout << "platform_provider=available implementation=" << implementation << '\n';
        const auto algorithm = NCryptIsAlgSupported(provider.value, BCRYPT_ECDSA_P256_ALGORITHM, 0);
        std::cout << "p256_status=" << static_cast<unsigned long>(algorithm) << '\n';
        DWORD certificateSize = 0;
        const auto ekStatus = NCryptGetProperty(provider.value, NCRYPT_PCP_EKCERT_PROPERTY,
            nullptr, 0, &certificateSize, 0);
        // EKCERT returns a certificate-store handle, NOT a DER certificate.
        // A successful size query alone does not prove that the store has a certificate.
        std::cout << "ek_store_query_status=0x" << std::hex << static_cast<unsigned long>(ekStatus)
            << std::dec << " handle_bytes=" << certificateSize << '\n';
        if (argc == 2 && std::string_view(argv[1]) == "--exercise-ephemeral-key") {
            // Explicit diagnostic: transient key only, destroyed when the handle closes.
            // No persistent key/NV allocation or owner/firmware configuration changes.
            tube::license::DeviceKey key;
            tube::license::CngCheck(NCryptCreatePersistedKey(provider.value, &key.value,
                NCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0, 0), "Create transient TPM key");
            DWORD policy = 0;
            tube::license::CngCheck(NCryptSetProperty(key.value, NCRYPT_EXPORT_POLICY_PROPERTY,
                reinterpret_cast<PBYTE>(&policy), sizeof(policy), 0), "Set nonexportable policy");
            tube::license::CngCheck(NCryptFinalizeKey(key.value, 0), "Finalize transient TPM key");
            const auto publicKey = tube::license::ExportDevicePublicKey(key.value);
            const auto challenge = tube::license::RandomChallenge();
            const auto signature = tube::license::SignDeviceChallenge(key.value, challenge);
            tube::license::VerifySignature(publicKey, challenge, signature);
            std::cout << "transient_tpm_challenge=passed\n";
        }
        std::cout << "key_attestation=not_qualified\nnv_counter=not_qualified\nproduction_ready=false\n";
        return algorithm == ERROR_SUCCESS ? 0 : 4;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 5;
    }
}
