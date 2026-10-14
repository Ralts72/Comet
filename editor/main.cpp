#include "editor.h"

int main(int argc, char** argv) {
    return Comet::launch(argc, argv,
        {.config_file = COMET_CONFIG_FILE, .config_profile = COMET_CONFIG_PROFILE},
        CometEditor::run_editor);
}
