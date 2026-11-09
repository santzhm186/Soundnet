#pragma once
#include "../../platform/NodeDescriptor.hpp"
// soundnet-module: soundnet.task-example
// soundnet-order: 110
// Hidden executable template: no audio callback, no central runtime branch.
namespace tri::nodes::task_example {
inline NodeModule module() {
    NodeSpec spec{"soundnet.task-example",nodeTypeID("soundnet.task-example"),"后台任务示例","开发",1,1,
        false,true,CapTask,TimeMode::None,0,{},{},{},nullptr,
        {{"request","输入文本","hello",4096,true,false},
         {"result","处理结果","",4096,true,true}}};
    return {std::move(spec),{},
        {{"后台任务示例","Background Task Example"},{"开发","Development"},
         {"输入文本","Input Text"},{"处理结果","Result"},{"正在处理…","Processing…"},
         {"开始","Start"},{"停止","Stop"},{"空闲","Idle"}}};
}
}
