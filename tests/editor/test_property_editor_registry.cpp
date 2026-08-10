#include "property_editor_registry.h"

#include <gtest/gtest.h>

namespace {
    struct TestComponent {
        float value = 0.0f;
    };

    TEST(PropertyEditorRegistryTest, DispatchesByPropertyType) {
        CometEditor::PropertyEditorRegistry registry;
        bool editor_called = false;
        ASSERT_TRUE(registry.register_editor(
            Comet::PropertyType::Float,
            [&editor_called](const Comet::PropertyDescriptor&, void* value) {
                editor_called = true;
                *static_cast<float*>(value) = 3.0f;
                return true;
            }));

        TestComponent component;
        const Comet::PropertyDescriptor property =
                Comet::make_property_descriptor(
                    "value", "Value", &TestComponent::value);

        EXPECT_TRUE(registry.edit_property(property, &component.value));
        EXPECT_TRUE(editor_called);
        EXPECT_FLOAT_EQ(component.value, 3.0f);
        EXPECT_TRUE(registry.contains(Comet::PropertyType::Float));
        EXPECT_FALSE(registry.contains(Comet::PropertyType::Bool));
    }

    TEST(PropertyEditorRegistryTest, AppliesChangeCallbackAfterEditing) {
        CometEditor::PropertyEditorRegistry registry;
        ASSERT_TRUE(registry.register_editor(
            Comet::PropertyType::Float,
            [](const Comet::PropertyDescriptor&, void* value) {
                *static_cast<float*>(value) = 12.0f;
                return true;
            }));

        TestComponent component;
        const Comet::PropertyDescriptor property =
                Comet::make_property_descriptor(
                    "value",
                    "Value",
                    &TestComponent::value,
                    {},
                    [](float& value) {
                        value = 5.0f;
                    });

        EXPECT_TRUE(registry.edit_property(property, &component.value));
        EXPECT_FLOAT_EQ(component.value, 5.0f);
    }

    TEST(PropertyEditorRegistryTest, RejectsDuplicateEditorsAndMissingValues) {
        CometEditor::PropertyEditorRegistry registry;
        const auto editor = [](const Comet::PropertyDescriptor&, void*) {
            return false;
        };
        ASSERT_TRUE(registry.register_editor(Comet::PropertyType::Float, editor));
        EXPECT_FALSE(registry.register_editor(Comet::PropertyType::Float, editor));

        const Comet::PropertyDescriptor property =
                Comet::make_property_descriptor(
                    "value", "Value", &TestComponent::value);
        EXPECT_FALSE(registry.edit_property(property, nullptr));
    }
}
