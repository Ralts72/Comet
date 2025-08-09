#pragma once

namespace Comet {
#if defined(_WIN32) || defined(_WIN64)
#ifdef COMET_EXPORTS
#define COMET_API __declspec(dllexport)
#else
#define COMET_API __declspec(dllimport)
#endif
#else
#define COMET_API __attribute__((visibility("default")))
#endif
}
