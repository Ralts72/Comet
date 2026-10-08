#include "ui/rml_context.h"

#include "ui/rml_platform.h"
#include "ui/rml_renderer.h"
#include "common/scope_exit.h"
#include "core/window.h"
#include "diagnostics/logger.h"

#include <RmlUi/Core.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace Comet::Ui {
    namespace {
        std::atomic_bool runtime_active = false;
        constexpr std::size_t max_file_bytes = 32 * 1024 * 1024;
        constexpr const char* context_name = "CometUi";
        bool inside_root(const std::filesystem::path& path, const std::filesystem::path& root) {
            auto path_part = path.begin();
            for(auto root_part = root.begin(); root_part != root.end(); ++root_part, ++path_part) {
                if(path_part == path.end() || *path_part != *root_part)
                    return false;
            }
            return true;
        }

        class UiFiles final: public Rml::FileInterface {
        public:
            UiFiles(std::filesystem::path documents, std::filesystem::path fonts)
                : m_documents(std::move(documents)), m_fonts(std::move(fonts)) {}

            Rml::FileHandle Open(const Rml::String& name) override {
                if(name.find("://") != std::string::npos)
                    return 0;
                std::error_code error;
                auto path = std::filesystem::path(name);
                if(path.is_relative())
                    path = m_documents / path;
                path = std::filesystem::canonical(path, error);
                if(error || (!inside_root(path, m_documents) && !inside_root(path, m_fonts)))
                    return 0;
                const auto size = std::filesystem::file_size(path, error);
                if(error || size > max_file_bytes)
                    return 0;
                auto file = std::make_unique<File>();
                file->contents.resize(static_cast<std::size_t>(size));
                std::ifstream stream(path, std::ios::binary);
                if(!stream
                    || !stream.read(file->contents.data(), static_cast<std::streamsize>(size)))
                    return 0;
                const auto handle = ++m_next_handle;
                m_files.emplace(handle, std::move(file));
                return handle;
            }

            void Close(const Rml::FileHandle handle) override { m_files.erase(handle); }
            std::size_t Read(
                void* buffer, const std::size_t size, const Rml::FileHandle handle) override {
                const auto found = m_files.find(handle);
                if(found == m_files.end())
                    return 0;
                auto& file = *found->second;
                const auto amount = std::min(size, file.contents.size() - file.position);
                if(amount != 0)
                    std::memcpy(buffer, file.contents.data() + file.position, amount);
                file.position += amount;
                return amount;
            }
            bool Seek(const Rml::FileHandle handle, const long offset, const int origin) override {
                const auto found = m_files.find(handle);
                if(found == m_files.end())
                    return false;
                auto& file = *found->second;
                long long base = 0;
                switch(origin) {
                    case SEEK_SET:
                        break;
                    case SEEK_CUR:
                        base = static_cast<long long>(file.position);
                        break;
                    case SEEK_END:
                        base = static_cast<long long>(file.contents.size());
                        break;
                    default:
                        return false;
                }
                if(offset < -base || offset > static_cast<long long>(file.contents.size()) - base)
                    return false;
                file.position = static_cast<std::size_t>(base + offset);
                return true;
            }
            std::size_t Tell(const Rml::FileHandle handle) override {
                const auto found = m_files.find(handle);
                return found == m_files.end() ? 0 : found->second->position;
            }
            std::size_t Length(const Rml::FileHandle handle) override {
                const auto found = m_files.find(handle);
                return found == m_files.end() ? 0 : found->second->contents.size();
            }

        private:
            struct File {
                std::vector<char> contents;
                std::size_t position = 0;
            };
            std::filesystem::path m_documents;
            std::filesystem::path m_fonts;
            Rml::FileHandle m_next_handle = 0;
            std::unordered_map<Rml::FileHandle, std::unique_ptr<File>> m_files;
        };

        class UiSystem final: public Rml::SystemInterface {
        public:
            explicit UiSystem(Comet::Window& window) : m_window(window) {}
            double GetElapsedTime() override {
                return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started)
                    .count();
            }
            bool LogMessage(const Rml::Log::Type type, const Rml::String& message) override {
                if(type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) {
                    ++m_errors;
                    LOG_ERROR("RmlUi: {}", message);
                } else if(type == Rml::Log::LT_WARNING) {
                    ++m_warnings;
                    LOG_WARN("RmlUi: {}", message);
                }
                return true;
            }
            void SetClipboardText(const Rml::String& text) override {
                glfwSetClipboardString(m_window.get(), text.c_str());
            }
            void GetClipboardText(Rml::String& text) override {
                const auto* clipboard = glfwGetClipboardString(m_window.get());
                text = clipboard ? clipboard : "";
            }
            std::size_t m_errors = 0;
            std::size_t m_warnings = 0;

        private:
            Comet::Window& m_window;
            std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
        };
    }

    class RmlContext::Impl final {
    public:
        Impl(Window& window, Options options, std::unique_ptr<RmlRenderer> renderer)
            : m_window(window), m_options(std::move(options)), m_renderer(std::move(renderer)),
              m_system(window) {}

        ~Impl() {
            if(m_initialized) {
                if(m_context)
                    Rml::RemoveContext(context_name);
                Rml::Shutdown();
                Rml::SetRenderInterface(nullptr);
                Rml::SetFileInterface(nullptr);
                Rml::SetSystemInterface(nullptr);
            }
            if(m_owns_runtime)
                runtime_active = false;
        }

        Result<void> initialize() {
            if(runtime_active.exchange(true))
                return Result<void>::failure("A Comet UI context already owns the RmlUi core");
            m_owns_runtime = true;
            std::error_code error;
            if(m_options.resource_root.empty())
                return Result<void>::failure("A UI resource directory is required");
            m_resource_root = std::filesystem::canonical(m_options.resource_root, error);
            if(error)
                return Result<void>::failure(
                    "Cannot open UI resource directory: " + error.message());
            m_font_root = std::filesystem::canonical(
                m_options.font_directory.empty() ? std::filesystem::path(COMET_UI_FONT_DIRECTORY)
                                                 : m_options.font_directory,
                error);
            if(error)
                return Result<void>::failure("Cannot open UI font directory: " + error.message());
            m_files = std::make_unique<UiFiles>(m_resource_root, m_font_root);
            Rml::SetSystemInterface(&m_system);
            Rml::SetFileInterface(m_files.get());
            Rml::SetRenderInterface(&m_renderer->interface());
            if(!Rml::Initialise()) {
                Rml::SetRenderInterface(nullptr);
                Rml::SetFileInterface(nullptr);
                Rml::SetSystemInterface(nullptr);
                return Result<void>::failure("Cannot initialize RmlUi core");
            }
            m_initialized = true;
            if(m_options.fonts.empty()) {
                m_options.fonts = {{"Roboto-Bold.ttf", "Comet", false},
                    {"NotoSansSC-Bold.otf", "CometChinese", true}};
            }
            for(const auto& face : m_options.fonts) {
                const auto file = (m_font_root / face.file).string();
                bool loaded = false;
                if(face.family.empty())
                    loaded = Rml::LoadFontFace(file, face.fallback);
                else
                    loaded = Rml::LoadFontFace(file, face.family, Rml::Style::FontStyle::Normal,
                        Rml::Style::FontWeight::Auto, face.fallback);
                if(!loaded)
                    return Result<void>::failure("Cannot load UI font: " + face.file.string());
            }
            const auto size = m_window.get_framebuffer_size();
            m_context = Rml::CreateContext(context_name,
                {static_cast<int>(size.x), static_cast<int>(size.y)}, &m_renderer->interface());
            if(!m_context)
                return Result<void>::failure("Cannot create UI context");
            m_context->SetDensityIndependentPixelRatio(m_window.get_content_scale().x);
            return Result<void>::success();
        }

        Result<Rml::ElementDocument*> replace_document(Rml::ElementDocument* current,
            const std::filesystem::path& file, const PrepareDocument& prepare,
            const PrepareDocument& finalize) {
            using Replacement = Result<Rml::ElementDocument*>;
            const ScopeExit finish_load([this] { m_loading_document = false; });
            m_loading_document = true;
            const auto errors_before = m_system.m_errors;
            const auto warnings_before = m_system.m_warnings;
            Rml::Factory::ClearStyleSheetCache();
            Rml::Factory::ClearTemplateCache();
            auto* candidate = m_context->LoadDocument((m_resource_root / file).string());
            std::string failure;
            bool old_hidden = false;
            if(!candidate)
                failure = "Cannot load UI document: " + file.string();
            else if(prepare) {
                if(const auto prepared = prepare(*candidate); !prepared)
                    failure = prepared.error();
            }
            if(candidate && failure.empty()) {
                if(current && current->IsVisible(true)) {
                    current->Hide();
                    old_hidden = true;
                }
                candidate->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
                if(!m_context->Update())
                    failure = "Cannot update candidate UI document";
                else if(const auto validated = m_renderer->validate(*m_context); !validated)
                    failure = "Cannot prepare UI resources: " + validated.error().message;
            }
            if(candidate && failure.empty() && finalize) {
                if(auto result = finalize(*candidate); !result)
                    failure = result.error();
                else if(!m_context->Update())
                    failure = "Cannot finalize candidate UI document";
                else if(auto validated = m_renderer->validate(*m_context); !validated)
                    failure = "Cannot finalize UI resources: " + validated.error().message;
            }
            if(failure.empty()
                && (m_system.m_errors != errors_before || m_system.m_warnings != warnings_before))
                failure = "UI document or style parsing failed";
            if(const auto graphics = m_renderer->take_error(); graphics && failure.empty())
                failure = "Cannot load UI resources: " + graphics->message;
            if(!failure.empty()) {
                if(candidate)
                    candidate->Close();
                if(current && old_hidden)
                    current->Show(
                        Rml::ModalFlag::None, Rml::FocusFlag::Keep, Rml::ScrollFlag::None);
                m_context->Update();
                return Replacement::failure(std::move(failure));
            }
            if(current)
                current->Close();
            return Replacement::success(candidate);
        }

        Window& m_window;
        Options m_options;
        std::unique_ptr<RmlRenderer> m_renderer;
        UiSystem m_system;
        std::unique_ptr<UiFiles> m_files;
        std::filesystem::path m_resource_root;
        std::filesystem::path m_font_root;
        RmlPlatform m_platform;
        Rml::Context* m_context = nullptr;
        bool m_initialized = false;
        bool m_owns_runtime = false;
        bool m_loading_document = false;
    };

    RmlContext::RmlContext(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    RmlContext::~RmlContext() = default;

    Result<std::unique_ptr<RmlContext>, Error> RmlContext::create(
        Window& window, Renderer& renderer, Options options) {
        using Creation = Result<std::unique_ptr<RmlContext>, Error>;
        auto backend = RmlRenderer::create(renderer);
        if(!backend)
            return Creation::failure(backend.error().as_error());
        auto impl = std::make_unique<Impl>(window, std::move(options), std::move(backend).value());
        if(const auto initialized = impl->initialize(); !initialized)
            return Creation::failure({initialized.error()});
        return Creation::success(std::unique_ptr<RmlContext>(new RmlContext(std::move(impl))));
    }

    Rml::Context& RmlContext::context() {
        return *m_impl->m_context;
    }
    void RmlContext::process_input(const Input::Frame& input, bool modal_open) {
        m_impl->m_platform.update(*m_impl->m_context, m_impl->m_window, input, modal_open);
    }
    void RmlContext::set_capture_active(bool active) {
        m_impl->m_platform.set_capture_active(active);
    }
    void RmlContext::cancel_input() {
        m_impl->m_platform.cancel_input(*m_impl->m_context);
    }
    void RmlContext::stop_dispatch_preserve_focus() {
        m_impl->m_platform.stop_dispatch_preserve_focus(*m_impl->m_context);
    }
    bool RmlContext::text_input_active() const {
        return m_impl->m_platform.text_input_active();
    }
    bool RmlContext::pointer_blocked() const {
        return m_impl->m_context->IsMouseInteracting();
    }
    Result<void> RmlContext::update() {
        if(!m_impl->m_context->Update())
            return Result<void>::failure("Cannot update UI context");
        return Result<void>::success();
    }
    Result<Rml::ElementDocument*> RmlContext::replace_document(Rml::ElementDocument* current,
        const std::filesystem::path& file, const PrepareDocument& prepare,
        const PrepareDocument& finalize) {
        return m_impl->replace_document(current, file, prepare, finalize);
    }
    bool RmlContext::is_loading_document() const {
        return m_impl->m_loading_document;
    }
    Result<void, GraphicsError> RmlContext::render(OverlayRecordContext& frame) {
        return m_impl->m_renderer->render(frame, *m_impl->m_context);
    }
    void RmlContext::release_swapchain_resources() {
        m_impl->m_renderer->release_swapchain_resources();
    }
    Result<void, GraphicsError> RmlContext::rebuild_swapchain_resources(
        const SwapchainCompatibility& compatibility) {
        return m_impl->m_renderer->rebuild_swapchain_resources(compatibility);
    }
}
