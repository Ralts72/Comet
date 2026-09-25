#include "assets/system_trash.h"

#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>

namespace CometEditor::SystemTrash {
    Comet::Result<void> move(const std::filesystem::path& path) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error);
        if(error)
            return Comet::Result<void>::failure(
                "Cannot resolve system trash item: " + error.message());

        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if(FAILED(initialized))
            return Comet::Result<void>::failure("Cannot initialize Windows shell operation");
        IFileOperation* operation = nullptr;
        IShellItem* item = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation));
        if(SUCCEEDED(result))
            result = operation->SetOperationFlags(
                FOF_NOCONFIRMATION | FOF_NOERRORUI | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE);
        if(SUCCEEDED(result))
            result = SHCreateItemFromParsingName(
                absolute.c_str(), nullptr, IID_PPV_ARGS(&item));
        if(SUCCEEDED(result))
            result = operation->DeleteItem(item, nullptr);
        if(SUCCEEDED(result))
            result = operation->PerformOperations();
        BOOL aborted = FALSE;
        if(SUCCEEDED(result))
            result = operation->GetAnyOperationsAborted(&aborted);
        if(item)
            item->Release();
        if(operation)
            operation->Release();
        CoUninitialize();
        if(FAILED(result) || aborted)
            return Comet::Result<void>::failure("Cannot move item to Windows Recycle Bin");
        return Comet::Result<void>::success();
    }
}

#elif defined(__linux__)
#include <gio/gio.h>

namespace CometEditor::SystemTrash {
    Comet::Result<void> move(const std::filesystem::path& path) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error);
        if(error)
            return Comet::Result<void>::failure(
                "Cannot resolve system trash item: " + error.message());

        GFile* file = g_file_new_for_path(absolute.c_str());
        GError* trash_error = nullptr;
        const gboolean moved = g_file_trash(file, nullptr, &trash_error);
        g_object_unref(file);
        if(!moved) {
            const std::string message = trash_error
                                            ? trash_error->message
                                            : "Cannot move item to system trash";
            if(trash_error)
                g_error_free(trash_error);
            return Comet::Result<void>::failure(message);
        }
        return Comet::Result<void>::success();
    }
}

#else
namespace CometEditor::SystemTrash {
    Comet::Result<void> move(const std::filesystem::path&) {
        return Comet::Result<void>::failure("System trash is not supported on this platform");
    }
}
#endif
