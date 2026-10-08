# Explicit internal ownership; all implementations are delivered by engine.

set(COMET_FOUNDATION_SOURCES
    src/common/parameters.cpp
    src/common/parameters.h
    src/common/file_io.cpp
    src/common/retry_backoff.cpp
    src/common/uuid.cpp
    src/core/frame_timer.cpp
    src/core/geometry.cpp
    src/core/task_scheduler.cpp
    src/core/project_paths.cpp
    src/diagnostics/logger.cpp
    src/diagnostics/profiler.cpp
    src/diagnostics/frame_diagnostics.cpp
    src/diagnostics/timing_history.cpp
)

set(COMET_SERIALIZATION_SOURCES
    src/common/json.cpp
)

set(COMET_SHADER_CONTRACTS_SOURCES
    src/graphics/pipeline/shader_interface.cpp
)

set(COMET_ASSET_DATA_SOURCES
    src/asset/script.cpp
    src/asset/script.h
    src/asset/data/script_sources.cpp
    src/asset/data/script_sources.h
    src/asset/handle.cpp
    src/asset/import_settings.cpp
    src/asset/metadata.cpp
    src/asset/registry.cpp
    src/asset/data/material_data.cpp
    src/asset/serialization/material_serializer.cpp
    src/asset/serialization/metadata_serializer.cpp
    src/asset/serialization/shader_program_serializer.cpp
)

set(COMET_INPUT_SOURCES
    src/input/input.cpp
    src/input/input_actions.cpp
    src/input/input_overrides.cpp
    src/input/player_input_settings.cpp
    src/input/player_input_edit.cpp
    src/input/input_state.cpp
    src/input/runtime_input.cpp
)

set(COMET_WORLD_SOURCES
    src/scene/entity.cpp
    src/scene/component_registry.cpp
    src/scene/property.cpp
    src/scene/scene.cpp
    src/scene/scene_serializer.cpp
    src/scene/scene_settings.cpp
    src/scene/script_component.cpp
    src/scene/audio_source_component.cpp
)

set(COMET_RUNTIME_SOURCES
    src/scene/runtime_session.cpp
    src/scene/runtime_session.h
    src/scene/runtime_services.h
    src/audio/audio_commands.h
    src/physics/physics_commands.cpp
    src/physics/physics_commands.h
    src/scene/scene_runtime.cpp
    src/scene/scene_runtime.h
    src/scene/systems/system.h
)

set(COMET_AUDIO_SOURCES
    src/audio/audio.cpp
    src/audio/audio.h
    src/audio/audio_commands.h
    src/audio/audio_service.cpp
    src/audio/audio_service.h
    src/scene/systems/audio_system.cpp
    src/scene/systems/audio_system.h
)

set(COMET_PHYSICS_SOURCES
    src/physics/physics_service.cpp
    src/physics/physics_service.h
    src/scene/systems/physics_system.cpp
    src/scene/systems/physics_system.h
)

set(COMET_SCRIPTING_SOURCES
    src/scripting/script_instance.cpp
    src/scripting/script_instance.h
    src/scripting/script_compiler.h
    src/scripting/lua_bindings.cpp
    src/scripting/lua_bindings.h
    src/scene/systems/script_system.cpp
    src/scene/systems/script_system.h
)

set(COMET_ASSET_PIPELINE_SOURCES
    src/asset/database.cpp
    src/asset/database_scan.cpp
    src/asset/artifact/mesh_artifact.cpp
    src/asset/artifact/environment_artifact.cpp
    src/asset/artifact/shader_program_artifact.cpp
    src/asset/import/input_snapshot.cpp
    src/asset/import/mesh_importer.cpp
    src/asset/import/import_service.cpp
    src/asset/import/asset_task_queue.cpp
    src/asset/import/texture_importer.cpp
    src/asset/import/environment_importer.cpp
    src/asset/import/environment_lighting.cpp
)

set(COMET_RUNTIME_ASSETS_SOURCES
    src/asset/asset_manager.cpp
    src/asset/asset_manager.h
    src/asset/asset_manager_async.cpp
    src/asset/asset_manager_scripts.cpp
    src/asset/runtime/script_loader.cpp
    src/asset/runtime/asset_loader.cpp
    src/asset/runtime/asset_loader.h
    src/asset/runtime/render_asset_publisher.h
)
