#pragma once
#include "../pch.h"

namespace Comet {
    class FileDialog {
    public:
        enum class Type { OpenFile, SaveFile, OpenFolder };

        static FileDialog createOpenFileDialog();

        static FileDialog createSaveDialog();

        static FileDialog createOpenFolderDialog();

        void setTitle(const std::string&);

        void setAcceptButtonText(const std::string&);

        void setCancelButtonText(const std::string&);

        void allowMultipleSelect(bool);

        void addFilter(const std::string& name, const std::string& pattern);

        void setDefaultFolder(const std::string&);

        FileDialog& open();

        [[nodiscard]] const std::vector<std::string>& getSelected() const;

    private:
        explicit FileDialog(Type type);

        static void callback(void* userdata, const char* const* filelist, int);

        struct Filter {
            std::string name;
            std::string pattern;
        };

        Type m_type;
        std::vector<Filter> m_filters;
        std::string m_default_folder;
        bool m_allow_multiple_select = false;
        std::string m_title_label;
        std::string m_accept_label;
        std::string m_cancel_label;
        SDL_PropertiesID m_properties;

        std::promise<std::vector<std::string>> m_promise;
        std::vector<std::string> m_results;
    };
}
