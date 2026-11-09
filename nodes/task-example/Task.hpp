#pragma once
#include "../../platform/TaskService.hpp"
namespace tri::nodes::task_example {
struct TextResult final:soundnet::TaskPayload {std::string text;};
inline soundnet::TaskService::Job makeTask(std::string input) {
    return [input=std::move(input)](soundnet::TaskContext& context)->soundnet::TaskResult {
        auto result=std::make_shared<TextResult>();result->text=input;
        for(size_t i=0;i<result->text.size();++i) {
            if(context.cancelled())return soundnet::TaskResult::cancelled();
            // Deliberately small deterministic example; preserve UTF-8 bytes.
            auto& c=result->text[i];if(c>='a'&&c<='z')c=char(c-'a'+'A');
        }
        context.progress(100);
        return {soundnet::TaskStatus::Succeeded,result,{}};
    };
}
}
