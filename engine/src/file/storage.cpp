#include "storage.h"

namespace Comet {
    ReadOnlyStorage::ReadOnlyStorage(Type type, const std::string& param1, const std::string& param2) {
        switch(type) {
            case Type::Title:
                m_storage = SDL_OpenTitleStorage(param1.c_str(), {});
                break;
            case Type::User:
                m_storage = SDL_OpenUserStorage(param1.c_str(), param2.c_str(), {});
                break;
            case Type::Local:
                m_storage = SDL_OpenFileStorage(param1.c_str());
                break;
        }
    }

    ReadOnlyStorage::~ReadOnlyStorage() {
        if(m_storage) {
            SDL_CloseStorage(m_storage);
        }
    }

    bool ReadOnlyStorage::isReady() const {
        return SDL_StorageReady(m_storage);
    }

    void ReadOnlyStorage::waitReady(uint32_t timeout) const {
        constexpr uint32_t wait_interval = 10;
        uint32_t elapsed = 0;
        while(!isReady() && elapsed < timeout) {
            SDL_Delay(wait_interval);
            elapsed += wait_interval;
        }
    }

    uint64_t ReadOnlyStorage::getFileSize(const std::string& filename) const {
        Uint64 size = 0;
        SDL_GetStorageFileSize(m_storage, filename.c_str(), &size);
        return static_cast<uint64_t>(size);
    }

    uint64_t ReadOnlyStorage::getRemainingSpace() const {
        return static_cast<uint64_t>(SDL_GetStorageSpaceRemaining(m_storage));
    }

    std::vector<char> ReadOnlyStorage::readFile(const std::string& filename) const {
        const uint64_t size = getFileSize(filename);
        if(size == 0) return {};
        std::vector<char> buffer(size);
        SDL_ReadStorageFile(m_storage, filename.c_str(), buffer.data(), size);
        return buffer;
    }

    bool ReadOnlyStorage::readFile(const std::string& filename, void* buffer, uint64_t size) const {
        return SDL_ReadStorageFile(m_storage, filename.c_str(), buffer, size);
    }

    bool WritableStorage::writeFile(const std::string& filename, const void* buffer, uint64_t size) const {
        return SDL_WriteStorageFile(m_storage, filename.c_str(), buffer, size);
    }

    bool WritableStorage::copyFile(const std::string& src, const std::string& dst) const {
        return SDL_CopyStorageFile(m_storage, src.c_str(), dst.c_str());
    }

    bool WritableStorage::removePath(const std::string& path) const {
        return SDL_RemoveStoragePath(m_storage, path.c_str());
    }

    bool WritableStorage::movePath(const std::string& from, const std::string& to) const {
        return SDL_RenameStoragePath(m_storage, from.c_str(), to.c_str());
    }

    LocalStorage::LocalStorage()
        : WritableStorage(Type::Local) {}

    LocalStorage::LocalStorage(const std::string& base_path)
        : WritableStorage(Type::Local, base_path) {}

    UserStorage::UserStorage()
        : WritableStorage(Type::User) {}

    UserStorage::UserStorage(const std::string& org, const std::string& app)
        : WritableStorage(Type::User, org, app) {}

    StorageManager::StorageManager(const std::string& org, const std::string& app)
        : m_app_storage(ReadOnlyStorage::Type::Title, "", ""),
          m_user_storage(org, app) {}

    AppReadOnlyStorage& StorageManager::getAppReadOnlyStorage() {
        return m_app_storage;
    }

    const AppReadOnlyStorage& StorageManager::getAppReadOnlyStorage() const {
        return m_app_storage;
    }

    UserStorage& StorageManager::getUserStorage() {
        return m_user_storage;
    }

    const UserStorage& StorageManager::getUserStorage() const {
        return m_user_storage;
    }

    std::unique_ptr<LocalStorage> StorageManager::acquireLocalStorage(const std::string& base_path) {
        return std::make_unique<LocalStorage>(base_path);
    }
} // namespace Comet
