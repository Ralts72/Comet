#include "ui/rml_context.h"
#include "support/engine_fixture.h"
#include "support/temporary_directory.h"
#include "common/scope_exit.h"
#include "core/window.h"
#include "render/overlay_record_context.h"

#include <RmlUi/Core.h>

#include <fstream>

namespace Comet::Tests {
    class RmlContextGpuTest: public EngineTest {};

    TEST_F(RmlContextGpuTest, ArbitraryPagesRenderAndFailedReplacementKeepsTheLiveDocument) {
        TemporaryDirectory resources;
        const auto write = [&](const char* file, const char* text) {
            std::ofstream output(resources.path() / file);
            output << text;
            return output.good();
        };
        ASSERT_TRUE(write("overlay.rml", R"rml(
            <rml><head><style>
            body { font-family: Comet; font-weight: bold; font-size: 14px; }
            </style></head><body data-model="ui"><p id="message">{{label}}</p>
            <button id="change" data-event-click="command()">Change</button></body></rml>)rml"));
        std::string label = "独立页面 / Hello";
        unsigned events = 0;
        auto created = Ui::RmlContext::create(engine->get_window(), engine->get_renderer(),
            {.resource_root = resources.path(), .font_directory = COMET_TEST_UI_FONT_DIRECTORY});
        ASSERT_TRUE(created) << created.error();
        auto ui = std::move(created).value();
        auto document = ui->replace_document(
            nullptr, "overlay.rml", {}, {}, [&](Rml::Context& context) -> Result<void> {
                auto model = context.CreateDataModel("ui");
                if(!model || !model.Bind("label", &label)
                    || !model.BindEventCallback("command",
                        [&](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
                            ++events;
                            label = "旧页面仍可更新";
                            handle.DirtyVariable("label");
                        }))
                    return Result<void>::failure("Cannot bind the page model");
                return Result<void>::success();
            });
        ASSERT_TRUE(document) << document.error();
        auto* live = document.value();
        ASSERT_NE(live->GetElementById("message"), nullptr);
        EXPECT_EQ(live->GetElementById("menu"), nullptr);
        EXPECT_EQ(live->GetElementById("apply"), nullptr);
        auto* old_context = &ui->context();
        auto* focused = live->GetElementById("change");
        ASSERT_TRUE(focused->Focus(true));

        ASSERT_TRUE(write("invalid.rml", "<rml><body><p>Invalid candidate</p></body></rml>"));
        auto rejected = ui->replace_document(
            live, "invalid.rml",
            [&](Rml::ElementDocument&) -> Result<void> {
                EXPECT_TRUE(ui->is_loading_document());
                return Result<void>::failure("Caller rejected this page");
            },
            {},
            [](Rml::Context& context) -> Result<void> {
                auto model = context.CreateDataModel("ui");
                if(!model || !model.BindFunc("candidate", [](Rml::Variant& value) { value = 1; }))
                    return Result<void>::failure("Cannot bind candidate model");
                return Result<void>::success();
            });
        EXPECT_FALSE(rejected);
        EXPECT_FALSE(ui->is_loading_document());
        EXPECT_TRUE(live->IsVisible(true));
        EXPECT_NE(live->GetElementById("message"), nullptr);
        EXPECT_EQ(&ui->context(), old_context);
        EXPECT_EQ(ui->context().GetFocusElement(), focused);
        EXPECT_EQ(Rml::GetNumContexts(), 1);
        EXPECT_NE(ui->take_draw_frame(), nullptr);
        focused->Click();
        ASSERT_TRUE(ui->update());
        EXPECT_EQ(events, 1u);
        EXPECT_EQ(live->GetElementById("message")->GetInnerRML(), label);

        live->Hide();
        ASSERT_TRUE(
            write("missing-resource.rml", "<rml><body><img src=\"missing.png\" /></body></rml>"));
        EXPECT_FALSE(ui->replace_document(live, "missing-resource.rml"));
        EXPECT_FALSE(live->IsVisible(true));
        live->Show();

        auto& renderer = engine->get_renderer();
        const ScopeExit clear_overlay([&] {
            renderer.wait_idle();
            renderer.set_overlay({});
        });
        renderer.set_overlay({.capture = [&] {
            OverlayDrawList draws;
            if(auto frame = ui->take_draw_frame())
                draws.push_back(std::move(frame));
            return draws;
        }});
        bool rendered = false;
        for(unsigned attempt = 0; attempt < 12 && !rendered; ++attempt) {
            engine->get_window().poll_events();
            auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error();
            if(prepared.value() == Renderer::FramePreparation::Ready) {
                ASSERT_TRUE(renderer.render_frame());
                rendered = true;
            }
        }
        EXPECT_TRUE(rendered);
    }
}
