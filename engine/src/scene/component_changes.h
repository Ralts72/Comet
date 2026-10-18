#pragma once

#include "common/export.h"

namespace Comet {
    class Scene;
    class Entity;
    struct ComponentChangeHooks {
        void (*changed)(Scene&, Entity, bool) = nullptr;
        void (*replacing)(Scene&, Entity, const void*, const void*) = nullptr;
        void (*removing)(Scene&, Entity) = nullptr;
    };
    template<typename T> inline constexpr ComponentChangeHooks component_change_hooks{};
    struct COMET_API ComponentChangeAccess {
        static void transform(Scene&, Entity, bool);
        static void attachment(Scene&, Entity, bool);
        static void render(Scene&, Entity, bool);
        static void mesh(Scene&, Entity, bool);
        static void replace_mesh(Scene&, Entity, const void*, const void*);
        static void physics(Scene&, Entity, bool);
        static void body(Scene&, Entity, bool);
        static void joint(Scene&, Entity, bool);
        static void particles(Scene&, Entity, bool);
        static void remove_wind(Scene&, Entity);
    };
}
