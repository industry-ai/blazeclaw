#include  pch.h
#include CredentialStore.h

#include <Windows.h>
#include <wincred.h>
#include <wincrypt.h>
#include <vector>
#include <fstream>
#include <iterator>

#pragma comment(lib, \Advapi32.lib\)
#pragma comment(lib, \Credui.lib\)

namespace blazeclaw::app {

bool CredentialStore::SaveCredential(const std::wstring& target, const std::string& secretUtf8) {
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.CredentialBlobSize = static_cast<DWORD>(secretUtf8.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secretUtf8.data()));
    return ::CredWriteW(&cred, 0) != FALSE;
}

std::optional<std::string> CredentialStore::LoadCredential(const std::wstring& target) {
    PCREDENTIALW pcred = nullptr;
    if (!::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &pcred)) return std::nullopt;
    std::optional<std::string> result;
    if (pcred->CredentialBlobSize > 0 && pcred->CredentialBlob) {
        const char* blob = reinterpret_cast<const char*>(pcred->CredentialBlob);
        result = std::string(blob, blob + pcred->CredentialBlobSize);
    }
    ::CredFree(pcred);
    return result;
}

bool CredentialStore::DeleteCredential(const std::wstring& target) {
    return ::CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
}

bool CredentialStore::SaveCredentialDPAPI(const std::wstring& filePath, const std::string& secretUtf8) {
    DATA_BLOB inBlob{};
    inBlob.cbData = static_cast<DWORD>(secretUtf8.size());
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(secretUtf8.data()));
    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &outBlob)) return false;
    std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) { LocalFree(outBlob.pbData); return false; }
    ofs.write(reinterpret_cast<const char*>(outBlob.pbData), outBlob.cbData);
    ofs.close();
    LocalFree(outBlob.pbData);
    return true;
}

std::optional<std::string> CredentialStore::LoadCredentialDPAPI(const std::wstring& filePath) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) return std::nullopt;
    std::vector<char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    DATA_BLOB inBlob{};
    inBlob.cbData = static_cast<DWORD>(data.size());
    inBlob.pbData = reinterpret_cast<BYTE*>(data.data());
    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) return std::nullopt;
    std::string result(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
    LocalFree(outBlob.pbData);
    return result;
}

} // namespace blazeclaw::app
