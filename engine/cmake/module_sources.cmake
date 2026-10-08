# Explicit internal ownership; all implementations are delivered by engine.

set(COMET_FOUNDATION_SOURCES
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
    src/scene/scene_runtime.cpp
    src/scene/scene_runtime.h
    src/scene/systems/system.h
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
