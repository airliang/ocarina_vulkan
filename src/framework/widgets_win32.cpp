#if defined(_WIN32)

#include "widgets.h"
#include "core/logging.h"
#include <objbase.h>
#include <commdlg.h>
#include <comutil.h>
#include <ShlObj_core.h>

#include <vector>

namespace ocarina {

namespace {

[[nodiscard]] ocarina_vector<COMDLG_FILTERSPEC> make_filter_specs(
    const FileDialogFilterVec& filters,
    ocarina_vector<ocarina_wstring>& owned_strings) {
    ocarina_vector<COMDLG_FILTERSPEC> specs;
    if (filters.empty()) {
        return specs;
    }

    owned_strings.reserve(filters.size() * 2);
    specs.reserve(filters.size());
    for (const FileDialogFilter& filter : filters) {
        ocarina_wstring name = filter.desc.empty()
            ? string_to_wstring(filter.ext)
            : string_to_wstring(filter.desc);
        ocarina_wstring pattern = L"*." + string_to_wstring(filter.ext);
        owned_strings.push_back(std::move(name));
        owned_strings.push_back(std::move(pattern));
        specs.push_back(COMDLG_FILTERSPEC{
            owned_strings[owned_strings.size() - 2].c_str(),
            owned_strings[owned_strings.size() - 1].c_str()});
    }
    return specs;
}

template<typename TDialog>
bool file_dialog_common(const FileDialogFilterVec& filters, fs::path& path, DWORD options, const CLSID clsid) {
    TDialog* pDialog = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pDialog))) || pDialog == nullptr) {
        OC_WARNING("file_dialog failure: CoCreateInstance");
        return false;
    }

    if (!path.empty()) {
        if (IShellItem* pShellItem = nullptr;
            SUCCEEDED(SHCreateItemFromParsingName(
                path.parent_path().c_str(),
                nullptr,
                IID_IShellItem,
                reinterpret_cast<void**>(&pShellItem)))) {
            pDialog->SetFolder(pShellItem);
            pShellItem->Release();
        }
    }

    ocarina_vector<ocarina_wstring> filter_strings;
    const ocarina_vector<COMDLG_FILTERSPEC> filter_specs = make_filter_specs(filters, filter_strings);
    if (!filter_specs.empty()) {
        pDialog->SetFileTypes(static_cast<UINT>(filter_specs.size()), filter_specs.data());
        pDialog->SetFileTypeIndex(1);
    }

    pDialog->SetOptions(options | FOS_FORCEFILESYSTEM);

    bool success = false;
    if (pDialog->Show(nullptr) == S_OK) {
        IShellItem* pItem = nullptr;
        if (pDialog->GetResult(&pItem) == S_OK && pItem != nullptr) {
            PWSTR pathStr = nullptr;
            // Must GetDisplayName before Release — the previous code released first and always failed.
            if (pItem->GetDisplayName(SIGDN_FILESYSPATH, &pathStr) == S_OK && pathStr != nullptr) {
                path = pathStr;
                CoTaskMemFree(pathStr);
                success = true;
            }
            pItem->Release();
        }
    }

    pDialog->Release();
    return success;
}

}// namespace

bool open_file_dialog_win32(std::filesystem::path& path, const FileDialogFilterVec& filters) noexcept {
    return file_dialog_common<IFileOpenDialog>(filters, path, FOS_FILEMUSTEXIST, CLSID_FileOpenDialog);
}

}// namespace ocarina

#endif
