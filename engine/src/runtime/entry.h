#pragma once
#include "runtime.h"

#define RUN_APP(App)                           \
int main() {                                   \
    App app;                                   \
    Comet::run(&app);                          \
    return 0;                                  \
}
