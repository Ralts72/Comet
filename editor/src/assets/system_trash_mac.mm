#include "assets/system_trash.h"

#import <Foundation/Foundation.h>

#include <string>

namespace CometEditor::SystemTrash {
    Comet::Result<void> move(const std::filesystem::path& path) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error);
        if(error)
            return Comet::Result<void>::failure(
                "Cannot resolve system trash item: " + error.message());

        @autoreleasepool {
            NSString* name = [NSString stringWithUTF8String:absolute.string().c_str()];
            if(!name)
                return Comet::Result<void>::failure("System trash item path is not valid UTF-8");
            NSError* trash_error = nil;
            const auto url = [NSURL fileURLWithPath:name];
            if(![[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil
                                                         error:&trash_error]) {
                const char* message = [[trash_error localizedDescription] UTF8String];
                return Comet::Result<void>::failure(
                    message ? std::string(message) : "Cannot move item to system trash");
            }
        }
        return Comet::Result<void>::success();
    }
}
