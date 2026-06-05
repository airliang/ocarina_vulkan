#if defined(_WIN32)

#include "widgets.h"
#include "core/logging.h"
#include <objbase.h>
#include <commdlg.h>
#include <comutil.h>
#include <ShlObj_core.h>

namespace ocarina {

namespace {

template<typename TDialog>
bool file_dialog_common(const FileDialogFilterVec &filters, fs::path &path, DWORD options, const CLSID clsid) {
    (void)filters;
    TDialog *pDialog;
    if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pDialog)))) {
        OC_WARNING("file_dialog failure");
        return false;
    }

    if (IShellItem * pShellItem;
        SUCCEEDED(SHCreateItemFromParsingName(path.parent_path().c_str(), NULL, IID_IShellItem,
                                              reinterpret_cast<void **>(&pShellItem)))) {
        pDialog->SetFolder(pShellItem);
        pShellItem->Release();
    }

    pDialog->SetOptions(options | FOS_FORCEFILESYSTEM);
    if (pDialog->Show(nullptr) == S_OK) {
        if (IShellItem * pItem;
            pDialog->GetResult(&pItem) == S_OK) {
            pItem->Release();
            PWSTR pathStr;
            if (pItem->GetDisplayName(SIGDN_FILESYSPATH, &pathStr) == S_OK) {
                path = pathStr;
                CoTaskMemFree(pathStr);
                return true;
            }
        }
    }
    return false;
}

}// namespace

bool open_file_dialog_win32(std::filesystem::path &path, const FileDialogFilterVec &filters) noexcept {
    return file_dialog_common<IFileOpenDialog>(filters, path, FOS_FILEMUSTEXIST, CLSID_FileOpenDialog);
}

}// namespace ocarina

#endif
