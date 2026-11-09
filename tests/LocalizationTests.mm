#import "../ui/SNLocalization.h"
#include "../src/NodeRegistry.hpp"
#include <cassert>
#include <cstdio>
#include <thread>

static BOOL containsChinese(NSString *text) {
    return [text rangeOfCharacterFromSet:[NSCharacterSet characterSetWithRange:NSMakeRange(0x4e00,0x9fff-0x4e00)]].location!=NSNotFound;
}

int main() {
    @autoreleasepool {
        // Never touch the production preference domain, even when launched by
        // itself while a real Soundnet instance is open.
        NSString *suite=[@"com.soundnet.localization-tests." stringByAppendingString:NSUUID.UUID.UUIDString];
        setenv("SOUNDNET_PREFERENCES_SUITE",suite.UTF8String,1);

        // Pure language-specific lookup leaves the session language intact.
        assert([SNLocalizedForLanguage(@"滤波器",YES) isEqualToString:@"Filter"]);
        assert([SNLocalizedForLanguage(@"Cutoff",NO) isEqualToString:@"截止频率"]);
        assert([SNLocalizedForLanguage(@"custom-name-123",YES) isEqualToString:@"custom-name-123"]);
        assert([SNLocalizedForLanguage(@"soundnet.filter",NO) isEqualToString:@"soundnet.filter"]);
        assert([SNLocalizedForLanguage(@"filter.cutoff",YES) isEqualToString:@"filter.cutoff"]);

        // Node-local words can be authored without touching the platform table.
        // Equal source strings in different modules must remain independent,
        // including the reverse lookup for English-authored parameter captions.
        std::vector<tri::NodeModule> localModules={
            {tri::NodeSpec{"example.first",701,"First","Test"},{},{{"同名标签","First meaning"}}},
            {tri::NodeSpec{"example.second",702,"Second","Test"},{},{{"同名标签","Second meaning"}}}
        };
        assert(std::strcmp(tri::nodeTranslation(701,"同名标签",true,localModules),"First meaning")==0);
        assert(std::strcmp(tri::nodeTranslation(702,"同名标签",true,localModules),"Second meaning")==0);
        assert(std::strcmp(tri::nodeTranslation(702,"Second meaning",false,localModules),"同名标签")==0);
        assert(!tri::nodeTranslation(701,"Second meaning",false,localModules));
        assert(!tri::nodeTranslation(703,"同名标签",true,localModules));
        const int gain=tri::nodeTypeForKey("soundnet.gain");
        assert([SNNodeLocalizedForLanguage(gain,@"增益",YES) isEqualToString:@"Gain"]);
        assert([SNNodeLocalizedForLanguage(gain,@"Gain",NO) isEqualToString:@"增益"]);
        assert([SNNodeLocalizedForLanguage(gain,@"撤销",YES) isEqualToString:@"Undo"]);
        assert([SNNodeLocalizedForLanguage(gain,@"gain",YES) isEqualToString:@"gain"]);
        assert([SNNodeLocalizedForLanguage(gain,nil,YES) isEqualToString:@""]);

        // Dynamic diagnostic details are opaque. A user name that happens to
        // contain another diagnostic or a format marker must remain intact.
        NSString *detail=@"我的音色 🎹 / filter.cutoff / 参数超出范围：%@";
        NSString *diagnostic=[@"未知的输入端口：" stringByAppendingString:detail];
        NSString *translated=[@"Unknown input port: " stringByAppendingString:detail];
        assert([SNLocalizedDiagnosticForLanguage(diagnostic,YES) isEqualToString:translated]);
        assert([SNLocalizedDiagnosticForLanguage(translated,NO) isEqualToString:diagnostic]);
        assert([SNLocalizedDiagnosticForLanguage(diagnostic,NO) isEqualToString:diagnostic]);
        assert([SNLocalizedDiagnosticForLanguage(@"参数超出范围：filter.cutoff",YES)
            isEqualToString:@"Parameter out of range: filter.cutoff"]);
        assert([SNLocalizedDiagnosticForLanguage(@"缺失节点类型：author.custom",YES)
            isEqualToString:@"Missing node type: author.custom"]);
        assert([SNLocalizedDiagnosticForLanguage(@"备注：参数超出范围：filter.cutoff",YES)
            isEqualToString:@"备注：参数超出范围：filter.cutoff"]);
        assert([SNLocalizedDiagnosticForLanguage(@"未注册的诊断：我的音色",YES)
            isEqualToString:@"未注册的诊断：我的音色"]);
        assert([SNLocalizedDiagnosticForLanguage(@"图中存在回路。",YES)
            isEqualToString:@"The graph contains a cycle."]);
        assert([SNLocalizedDiagnosticForLanguage(nil,YES) isEqualToString:@""]);

        // Every built-in node, port, and enum can be shown in English. Parameter
        // captions authored in English must also have a Chinese display label.
        for(int type:tri::nodeCatalog()) {
            const auto *spec=tri::nodeSpec(type);
            assert(spec);
            assert(!containsChinese(SNNodeLocalizedForLanguage(type,@(spec->title),YES)));
            assert(!containsChinese(SNNodeLocalizedForLanguage(type,@(spec->category),YES)));
            for(const auto &port:spec->inputs)assert(!containsChinese(SNNodeLocalizedForLanguage(type,@(port.label),YES)));
            for(const auto &port:spec->outputs)assert(!containsChinese(SNNodeLocalizedForLanguage(type,@(port.label),YES)));
            for(int p:tri::nodeParamList(type)) {
                const auto *param=tri::paramSpec(p);
                assert(param);
                assert(!containsChinese(SNEnglish(@(tri::paramGroupLabel(param->group)))));
                NSString *name=@(param->info->name);
                if(![name isEqualToString:@"BPM"])
                    assert(containsChinese(SNNodeLocalizedForLanguage(type,name,NO)));
                for(int i=0;i<param->optionCount;++i)
                    assert(!containsChinese(SNNodeLocalizedForLanguage(type,@(param->options[i]),YES)));
            }
        }

        // Translations must preserve printf argument types and their order;
        // an otherwise valid translation can crash an NSString format call.
        NSRegularExpression *format=[NSRegularExpression regularExpressionWithPattern:
            @"%(?:[0-9]+\\$)?[-+ #0]*(?:[0-9]+|\\*)?(?:\\.(?:[0-9]+|\\*))?(?:hh|ll|h|l|j|z|t|L)?[@diuoxXfFeEgGaAcsp%]"
            options:0 error:nil];
        for(NSString *source in SNEnglishStrings()) {
            NSString *target=SNEnglishStrings()[source];
            NSArray *a=[format matchesInString:source options:0 range:NSMakeRange(0,source.length)];
            NSArray *b=[format matchesInString:target options:0 range:NSMakeRange(0,target.length)];
            assert(a.count==b.count);
            for(NSUInteger i=0;i<a.count;++i)
                assert([[source substringWithRange:[a[i] range]] isEqualToString:[target substringWithRange:[b[i] range]]]);
        }
        for(const auto& module:tri::nodeModules())for(const auto& row:module.translations) {
            assert(row.source&&*row.source&&row.en&&*row.en);
            NSString *source=@(row.source),*target=@(row.en);
            NSArray *a=[format matchesInString:source options:0 range:NSMakeRange(0,source.length)];
            NSArray *b=[format matchesInString:target options:0 range:NSMakeRange(0,target.length)];
            assert(a.count==b.count);
            for(NSUInteger i=0;i<a.count;++i)
                assert([[source substringWithRange:[a[i] range]] isEqualToString:[target substringWithRange:[b[i] range]]]);
        }

        // Defaults can be changed by another process after a UI has opened.
        // Only explicit language selection may change this session's language.
        NSUserDefaults *preferences=SNLanguagePreferences();
        [preferences setObject:@"zh" forKey:@"SoundnetLanguage"];
        assert([preferences synchronize]);
        assert(!SNIsEnglish());
        __block int notifications=0;
        id observer=[[NSNotificationCenter defaultCenter] addObserverForName:SNLanguageDidChangeNotification
            object:nil queue:nil usingBlock:^(NSNotification *note){
                assert(NSThread.isMainThread);
                ++notifications;
            }];
        SNSetEnglish(YES);
        assert(SNIsEnglish() && notifications==1);
        NSUserDefaults *other=[[NSUserDefaults alloc] initWithSuiteName:suite];
        assert([other synchronize]);
        assert([[other stringForKey:@"SoundnetLanguage"] isEqualToString:@"en"]);

        // Simulate a delayed external write, then read from concurrent UI and
        // worker paths: neither should revert without a change notification.
        [other setObject:@"zh" forKey:@"SoundnetLanguage"];
        assert([other synchronize]);
        dispatch_apply(128,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^(size_t index){
            @autoreleasepool {
                assert(SNIsEnglish());
                assert([SNLocalized(@"滤波器") isEqualToString:@"Filter"]);
            }
        });
        assert(notifications==1);
        SNSetEnglish(NO);
        assert(!SNIsEnglish() && notifications==2);
        assert([other synchronize]);
        assert([[other stringForKey:@"SoundnetLanguage"] isEqualToString:@"zh"]);
        [other setObject:@"en" forKey:@"SoundnetLanguage"];
        assert([other synchronize]);
        assert(!SNIsEnglish());
        assert([SNLocalized(@"Cutoff") isEqualToString:@"截止频率"]);
        SNSetEnglish(NO); // Re-selecting the same language still persists it.
        assert(notifications==2);
        assert([other synchronize]);
        assert([[other stringForKey:@"SoundnetLanguage"] isEqualToString:@"zh"]);

        // Background callers update the atomic state immediately and notify
        // AppKit observers on the main thread.
        std::thread writer([]{
            @autoreleasepool { SNSetEnglish(YES); }
        });
        writer.join();
        assert(SNIsEnglish());
        assert([other synchronize]);
        assert([[other stringForKey:@"SoundnetLanguage"] isEqualToString:@"en"]);
        NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:1];
        while(notifications<3 && deadline.timeIntervalSinceNow>0)
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
        assert(notifications==3);

        [[NSNotificationCenter defaultCenter] removeObserver:observer];
        [preferences removePersistentDomainForName:suite];
        assert([preferences synchronize]);
        puts("PASS: bilingual coverage, stable session language and isolated preference persistence");
    }
}
