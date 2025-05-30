#pragma once

#include "runtime.h"

#define RUN_APP(AppClass)               \
int main() {                                   \
    AppClass app;                              \
    Comet::run(&app);                        \
    return 0;                                  \
}
