// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "error_journaling/document_picker.hpp"
#include "error_journaling/document_types.hpp"
#include "top_view_controller.hpp"

#include <string>
#include <utility>

// The controller's delegate is weak, so the delegate is held strongly until it has answered. It answers once.
@interface EJPickerDelegate : NSObject <UIDocumentPickerDelegate>
- (instancetype)initWithAnswer:(std::function<void(const ej::bridge::PickResult&)>)answer;
@end

@implementation EJPickerDelegate
{
    std::function<void(const ej::bridge::PickResult&)> _answer;
}

- (instancetype)initWithAnswer:(std::function<void(const ej::bridge::PickResult&)>)answer
{
    self = [super init];
    if (self != nil)
    {
        _answer = std::move(answer);
    }
    return self;
}

- (void)finish:(ej::bridge::PickResult)result
{
    if (_answer)
    {
        auto answer = std::move(_answer);
        _answer = nullptr;
        answer(result);
    }
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    (void)controller;
    ej::bridge::PickResult result;
    NSURL* const url = urls.firstObject;
    if (url != nil && url.isFileURL)
    {
        result.m_Outcome = ej::bridge::PickOutcome::PICKED;
        result.m_Path = std::filesystem::path(std::string(url.path.UTF8String));
    }
    else
    {
        result.m_Outcome = ej::bridge::PickOutcome::FAILED;
    }
    [self finish:result];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller
{
    (void)controller;
    [self finish:ej::bridge::PickResult{ej::bridge::PickOutcome::CANCELLED, {}}];
}
@end

namespace ej::bridge
{

namespace
{

class IosDocumentPicker final : public DocumentPicker
{
public:
    void pick_configuration(std::function<void(const PickResult&)> done) override
    {
        UIViewController* const presenter = top_view_controller();
        UTType* const config = [UTType typeWithIdentifier:[NSString stringWithUTF8String:std::string(CONFIG_TYPE_IDENTIFIER).c_str()]];
        NSMutableArray<UTType*>* types = [NSMutableArray arrayWithObject:UTTypeJSON];
        if (config != nil)
        {
            [types addObject:config];
        }
        if (presenter == nil)
        {
            // The delegate never runs on a path that did not present, so answer on the next turn of the main queue to
            // keep the promise that `done` comes after this call returns.
            dispatch_async(dispatch_get_main_queue(), ^{
                done(PickResult{PickOutcome::FAILED, {}});
            });
            return;
        }
        // asCopy: the system hands over a private copy, so no security-scoped access is needed.
        UIDocumentPickerViewController* const picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types
                                                                                                                   asCopy:YES];
        picker.allowsMultipleSelection = NO;
        m_Delegate = [[EJPickerDelegate alloc] initWithAnswer:[this, done = std::move(done)](const PickResult& result) {
            m_Delegate = nil;
            done(result);
        }];
        picker.delegate = m_Delegate;
        [presenter presentViewController:picker animated:YES completion:nil];
    }

private:
    EJPickerDelegate* m_Delegate = nil;
};

} // namespace

std::unique_ptr<DocumentPicker> make_document_picker()
{
    return std::make_unique<IosDocumentPicker>();
}

} // namespace ej::bridge
