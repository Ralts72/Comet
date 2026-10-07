#include "assets/system_text_editor.h"

#import <AppKit/NSWorkspace.h>
#import <Foundation/Foundation.h>

#include <string>

namespace CometEditor::SystemTextEditor {
    Comet::Result<void> open(const std::filesystem::path& path) {
        @autoreleasepool {
            NSString* name = [NSString stringWithUTF8String:path.string().c_str()];
            if(!name)
                return Comet::Result<void>::failure("Text editor source path is not valid UTF-8");

            NSURL* vscode = [[NSWorkspace sharedWorkspace]
                URLForApplicationWithBundleIdentifier:@"com.microsoft.VSCode"];
            NSArray<NSString*>* arguments = vscode ? @[@"-a", vscode.path, name] : @[@"-t", name];
            NSError* launch_error = nil;
            NSTask* task = [NSTask launchedTaskWithExecutableURL:[NSURL fileURLWithPath:@"/usr/bin/open"]
                                                       arguments:arguments
                                                           error:&launch_error
                                              terminationHandler:nil];
            if(!task) {
                std::string message = "Cannot start the text editor";
                const char* detail = [[launch_error localizedDescription] UTF8String];
                if(detail) {
                    message += ": ";
                    message += detail;
                }
                return Comet::Result<void>::failure(message);
            }

            // 只等待短命的 open 启动器，不等待文本编辑器关闭。
            [task waitUntilExit];
            if([task terminationReason] != NSTaskTerminationReasonExit || [task terminationStatus] != 0)
                return Comet::Result<void>::failure(
                    "Text editor open request failed (status " +
                    std::to_string([task terminationStatus]) + ")");
        }
        return Comet::Result<void>::success();
    }
}
