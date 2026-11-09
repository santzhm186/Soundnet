#include "Node.hpp"
#include "Task.hpp"
#include <cassert>
#include <cstdio>
int main() {
    auto module=tri::nodes::task_example::module();
    assert(module.spec.capabilities==tri::CapTask && !module.spec.kernelFactory);
    assert(module.spec.inputs.empty() && module.spec.outputs.empty());
    soundnet::TaskService tasks(1);tasks.reconcile({{1,1}});
    soundnet::TaskBinding binding;std::string why;
    assert(tasks.submit(1,1,tri::nodes::task_example::makeTask("hello 节点"),binding,why));
    assert(tasks.waitIdle(std::chrono::seconds(2)));
    auto results=tasks.takeCompleted();assert(results.size()==1);
    auto text=std::dynamic_pointer_cast<const tri::nodes::task_example::TextResult>(results[0].result.payload);
    assert(text&&text->text=="HELLO 节点");
    std::puts("PASS: independent task module without an audio kernel or app dependencies");
}
