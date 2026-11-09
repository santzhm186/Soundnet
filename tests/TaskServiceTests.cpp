#include "../platform/TaskService.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
using namespace soundnet;
using namespace std::chrono_literals;
struct Number final:TaskPayload {int value;explicit Number(int n):value(n){}};
static TaskResult number(int n) {return {TaskStatus::Succeeded,std::make_shared<Number>(n),{}};}
int main() {
    TaskService tasks(2,8);
    tasks.reconcile({{1,10},{2,20}});
    TaskBinding a,b;std::string why;
    std::atomic<bool> firstStarted{false};
    assert(tasks.submit(1,10,[&](TaskContext& ctx){firstStarted=true;ctx.waitFor(5s);return number(10);},a,why));
    assert(tasks.submit(2,20,[](TaskContext&){return number(20);},b,why));
    while(!firstStarted)std::this_thread::yield();
    tasks.cancel(1);
    assert(tasks.waitIdle(2s));
    auto finished=tasks.takeCompleted();
    assert(finished.size()==2);
    for(const auto& done:finished) {
        if(done.binding.node==1)assert(done.result.status==TaskStatus::Cancelled && !done.result.payload);
        else assert(done.result.status==TaskStatus::Succeeded &&
                    std::static_pointer_cast<const Number>(done.result.payload)->value==20);
    }
    assert(tasks.takeCompleted().empty());

    // Editing this node or reusing its ID cannot apply an old result. Changes
    // to an unrelated node do not invalidate this node's fixed input snapshot.
    firstStarted=false;
    assert(tasks.submit(1,10,[&](TaskContext& ctx){firstStarted=true;ctx.waitFor(5s);return number(99);},a,why));
    while(!firstStarted)std::this_thread::yield();
    tasks.reconcile({{1,11},{2,21}});
    assert(tasks.waitIdle(2s));
    assert(tasks.takeCompleted().empty());
    assert(!tasks.submit(1,10,[](TaskContext&){return number(0);},a,why));
    assert(tasks.submit(1,11,[](TaskContext&){return number(11);},a,why));
    tasks.reconcile({{1,11},{2,22}});
    assert(tasks.waitIdle(2s));
    finished=tasks.takeCompleted();assert(finished.size()==1&&finished[0].binding.revision==11);

    // Cancellation remains effective in the worker-success/control-commit gap.
    assert(tasks.submit(1,11,[](TaskContext&){return number(11);},a,why));
    assert(tasks.waitIdle(2s));tasks.cancel(1);
    finished=tasks.takeCompleted();assert(finished.size()==1&&finished[0].result.status==TaskStatus::Cancelled);

    // Session identity is separate from projectId and survives equal content.
    firstStarted=false;
    assert(tasks.submit(1,11,[&](TaskContext& ctx){firstStarted=true;ctx.waitFor(5s);return number(1);},a,why));
    while(!firstStarted)std::this_thread::yield();
    tasks.beginSession();tasks.reconcile({{1,11}});
    assert(tasks.submit(1,11,[](TaskContext&){return number(2);},b,why));
    assert(a.session!=b.session);
    assert(tasks.waitIdle(2s));
    finished=tasks.takeCompleted();assert(finished.size()==1&&finished[0].binding.id==b.id);

    // Active work is bounded, queued jobs cancel without invoking their body.
    TaskService bounded(1,2);bounded.reconcile({{1,1},{2,1},{3,1},{4,1}});
    firstStarted=false;std::atomic<int> running{0},maximum{0},queuedInvocations{0};
    assert(bounded.submit(1,1,[&](TaskContext& ctx){maximum=++running;firstStarted=true;ctx.waitFor(5s);--running;return number(1);},a,why));
    while(!firstStarted)std::this_thread::yield();
    assert(bounded.submit(2,1,[&](TaskContext&){++queuedInvocations;return number(2);},b,why));
    assert(bounded.submit(3,1,[&](TaskContext&){++queuedInvocations;return number(3);},b,why));
    assert(!bounded.submit(4,1,[](TaskContext&){return number(4);},b,why));
    const auto before=std::chrono::steady_clock::now();bounded.shutdown();
    assert(std::chrono::steady_clock::now()-before<100ms);
    assert(bounded.waitIdle(2s));assert(maximum==1&&queuedInvocations==0);
    std::puts("PASS: task ownership, input revisions, session isolation, cancellation and bounded workers");
}
