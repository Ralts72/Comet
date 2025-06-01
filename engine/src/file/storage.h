#pragma once
#include "../pch.h"

namespace Comet {
    class ReadOnlyStorage {
    public:
        enum class Type { Title, User, Local };

        ReadOnlyStorage(Type type, const std::string& param1 = "", const std::string& param2 = "");

        virtual ~ReadOnlyStorage();

        bool isReady() const;

        void waitReady(uint32_t timeout = std::numeric_limits<uint32_t>::max()) const;

        uint64_t getFileSize(const std::string& filename) const;

        uint64_t getRemainingSpace() const;

        std::vector<char> readFile(const std::string& filename) const;

        bool readFile(const std::string& filename, void* buffer, uint64_t size) const;

    protected:
        SDL_Storage* m_storage{};
    };

    class WritableStorage: public ReadOnlyStorage {
    public:
        using ReadOnlyStorage::ReadOnlyStorage;

        virtual bool writeFile(const std::string& filename, const void* buffer, uint64_t size) const;

        virtual bool copyFile(const std::string& src, const std::string& dst) const;

        virtual bool removePath(const std::string& path) const;

        virtual bool movePath(const std::string& from, const std::string& to) const;
    };

    class AppReadOnlyStorage final: public ReadOnlyStorage {
    public:
        using ReadOnlyStorage::ReadOnlyStorage;
    };

    class LocalStorage final: public WritableStorage {
    public:
        LocalStorage();

        explicit LocalStorage(const std::string& base_path);
    };

    class UserStorage final: public WritableStorage {
    public:
        UserStorage();

        UserStorage(const std::string& org, const std::string& app);
    };

    class StorageManager {
    public:
        StorageManager(const std::string& org, const std::string& app);

        AppReadOnlyStorage& getAppReadOnlyStorage();

        const AppReadOnlyStorage& getAppReadOnlyStorage() const;

        UserStorage& getUserStorage();

        const UserStorage& getUserStorage() const;

        static std::unique_ptr<LocalStorage> acquireLocalStorage(const std::string& base_path = "");

    private:
        AppReadOnlyStorage m_app_storage;
        UserStorage m_user_storage;
    };
} // namespace Comet
