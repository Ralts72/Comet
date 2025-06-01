#include "dialog.h"
#include "../common/macro.h"

namespace Comet {
    FileDialog::FileDialog(Type type)
        : m_type{type}, m_properties{SDL_CreateProperties()} {}

    FileDialog FileDialog::createOpenFileDialog() {
        return FileDialog{Type::OpenFile};
    }

    FileDialog FileDialog::createSaveDialog() {
        return FileDialog{Type::SaveFile};
    }

    FileDialog FileDialog::createOpenFolderDialog() {
        return FileDialog{Type::OpenFolder};
    }

    void FileDialog::setTitle(const std::string& title) {
        m_title_label = title;
    }

    void FileDialog::setAcceptButtonText(const std::string& text) {
        m_accept_label = text;
    }

    void FileDialog::setCancelButtonText(const std::string& text) {
        m_cancel_label = text;
    }

    void FileDialog::allowMultipleSelect(bool allow) {
        m_allow_multiple_select = allow;
    }

    void FileDialog::addFilter(const std::string& name, const std::string& pattern) {
        m_filters.push_back({name, pattern});
    }

    void FileDialog::setDefaultFolder(const std::string& folder) {
        m_default_folder = folder;
    }

    FileDialog& FileDialog::open() {
        std::future<std::vector<std::string>> f = m_promise.get_future();

        SDL_FileDialogType dialogType{};
        switch(m_type) {
            case Type::OpenFile: dialogType = SDL_FILEDIALOG_OPENFILE;
                break;
            case Type::SaveFile: dialogType = SDL_FILEDIALOG_SAVEFILE;
                break;
            case Type::OpenFolder: dialogType = SDL_FILEDIALOG_OPENFOLDER;
                break;
        }

        std::vector<SDL_DialogFileFilter> filters;
        filters.reserve(m_filters.size());
        for(const auto& [name, pattern]: m_filters) {
            filters.push_back({name.c_str(), pattern.c_str()});
        }

        SDL_CHECK(SDL_SetStringProperty(m_properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, m_default_folder.c_str()));
        SDL_CHECK(SDL_SetStringProperty(m_properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, m_title_label.c_str()));
        SDL_CHECK(SDL_SetStringProperty(m_properties, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, m_accept_label.c_str()));
        SDL_CHECK(SDL_SetStringProperty(m_properties, SDL_PROP_FILE_DIALOG_CANCEL_STRING, m_cancel_label.c_str()));
        SDL_CHECK(SDL_SetBooleanProperty(m_properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, m_allow_multiple_select));
        SDL_CHECK(SDL_SetNumberProperty(m_properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, filters.size()));
        SDL_CHECK(SDL_SetPointerProperty(m_properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, filters.data()));

        SDL_ShowFileDialogWithProperties(dialogType, &callback, this, m_properties);
        m_results = f.get();
        return *this;
    }

    const std::vector<std::string>& FileDialog::getSelected() const {
        return m_results;
    }

    void FileDialog::callback(void* userdata, const char* const* filelist, int) {
        auto* self = static_cast<FileDialog*>(userdata);
        std::vector<std::string> results;

        while(filelist && *filelist) {
            results.emplace_back(*filelist);
            ++filelist;
        }

        self->m_promise.set_value(std::move(results));
    }
} // namespace nickel
