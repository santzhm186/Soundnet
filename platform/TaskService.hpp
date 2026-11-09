#pragma once
// Control/background-thread service. Never call this API from an audio callback.
// Jobs own immutable input snapshots and return immutable payloads. Only the
// current project session + node + input revision may claim a result.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace soundnet {
enum class TaskStatus { Queued, Running, Succeeded, Failed, Cancelled };
inline bool taskActive(TaskStatus status) {
    return status==TaskStatus::Queued || status==TaskStatus::Running;
}
struct TaskBinding {
    uint64_t session=0,node=0,revision=0,id=0;
};
struct TaskPayload { virtual ~TaskPayload()=default; };
struct TaskResult {
    TaskStatus status=TaskStatus::Succeeded;
    std::shared_ptr<const TaskPayload> payload;
    std::string message;
    static TaskResult failure(std::string reason) {
        return {TaskStatus::Failed,nullptr,std::move(reason)};
    }
    static TaskResult cancelled() {return {TaskStatus::Cancelled,nullptr,{}};}
};
struct TaskCancellation {
    std::atomic<bool> requested{false};
    mutable std::mutex mutex;
    std::condition_variable changed;
    void cancel() {
        // Synchronize with waitFor's predicate/unlock boundary. An atomic flag
        // alone can lose the notification between that predicate and sleep.
        {std::lock_guard<std::mutex> lock(mutex);requested.store(true,std::memory_order_release);}
        changed.notify_all();
    }
};
class TaskContext {
    std::shared_ptr<TaskCancellation> cancellation;
    std::shared_ptr<std::atomic<int>> progressValue;
public:
    TaskContext(std::shared_ptr<TaskCancellation> token,std::shared_ptr<std::atomic<int>> progress)
        :cancellation(std::move(token)),progressValue(std::move(progress)){}
    bool cancelled() const {return cancellation->requested.load(std::memory_order_acquire);}
    void progress(int value) {progressValue->store(value<0?-1:(value>100?100:value),std::memory_order_release);}
    // Waits without spinning and wakes promptly for cancellation.
    bool waitFor(std::chrono::milliseconds duration) const {
        std::unique_lock<std::mutex> lock(cancellation->mutex);
        cancellation->changed.wait_for(lock,duration,[this]{return cancelled();});
        return cancelled();
    }
};
class TaskService {
public:
    using Job=std::function<TaskResult(TaskContext&)>;
    struct Snapshot {
        TaskBinding binding;
        TaskStatus status=TaskStatus::Cancelled;
        int progress=-1;
        bool cancellationRequested=false;
        std::string message;
        bool active() const {return taskActive(status);}
    };
    struct Completion {TaskBinding binding;TaskResult result;};
private:
    struct Task {
        TaskBinding binding;
        TaskStatus status=TaskStatus::Queued;
        Job job;
        std::shared_ptr<TaskCancellation> cancellation=std::make_shared<TaskCancellation>();
        std::shared_ptr<std::atomic<int>> progress=std::make_shared<std::atomic<int>>(-1);
        TaskResult result;
        bool delivered=false;
    };
    struct State {
        std::mutex mutex;
        std::condition_variable ready,idle;
        bool closed=false;
        uint64_t session=1,serial=0;
        size_t active=0,workerCount=0,queueLimit=16;
        std::map<uint64_t,uint64_t> revisions;
        std::map<uint64_t,std::shared_ptr<Task>> tasks;
        std::deque<std::shared_ptr<Task>> queue;
        bool current(const Task& task) const {
            const auto found=revisions.find(task.binding.node);
            const auto owner=tasks.find(task.binding.node);
            return !closed && task.binding.session==session && found!=revisions.end() &&
                found->second==task.binding.revision && owner!=tasks.end() && owner->second.get()==&task;
        }
    };
    std::shared_ptr<State> state;
    static void work(std::shared_ptr<State> shared) {
        for(;;) {
            std::shared_ptr<Task> task;
            Job job;
            {
                std::unique_lock<std::mutex> lock(shared->mutex);
                shared->ready.wait(lock,[&]{return shared->closed || !shared->queue.empty();});
                if(shared->queue.empty()) {
                    if(shared->closed) {--shared->workerCount;shared->idle.notify_all();return;}
                    continue;
                }
                task=shared->queue.front();shared->queue.pop_front();
                ++shared->active;
                task->status=TaskStatus::Running;
                job=std::move(task->job);
                if(!shared->current(*task))task->cancellation->cancel();
            }
            TaskResult result;
            TaskContext context(task->cancellation,task->progress);
            try {
                result=context.cancelled()?TaskResult::cancelled():job(context);
                if(taskActive(result.status))result=TaskResult::failure("Task returned a non-terminal result.");
            } catch(const std::exception& error) {result=TaskResult::failure(error.what());}
              catch(...) {result=TaskResult::failure("Background task failed.");}
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                // A cancelled, removed, replaced or superseded task can never
                // publish, even if its backend reported success afterwards.
                if(context.cancelled() || !shared->current(*task))result=TaskResult::cancelled();
                task->status=result.status;
                task->result=std::move(result);
                --shared->active;
                shared->idle.notify_all();
            }
        }
    }
public:
    explicit TaskService(size_t workers=2,size_t queueLimit=16):state(std::make_shared<State>()) {
        if(workers==0 || workers>8 || queueLimit==0)throw std::invalid_argument("Invalid task service budget.");
        state->queueLimit=queueLimit;
        state->workerCount=workers;
        // Workers own State, not this facade. shutdown/destruction only signals
        // cancellation and never blocks a UI thread while an external process
        // is terminating. Worker count is bounded and each worker exits closed.
        for(size_t i=0;i<workers;++i)std::thread(&TaskService::work,state).detach();
    }
    ~TaskService() {shutdown();}
    TaskService(const TaskService&)=delete;
    TaskService& operator=(const TaskService&)=delete;
    uint64_t session() const {std::lock_guard<std::mutex> lock(state->mutex);return state->session;}
    // Called on opening/restoring/replacing the project, even if its persisted
    // projectId is the same. Session identity is intentionally not persisted.
    void beginSession() {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->session;
        for(auto& row:state->tasks)row.second->cancellation->cancel();
        state->tasks.clear();state->revisions.clear();
    }
    void reconcile(const std::map<uint64_t,uint64_t>& revisions) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->revisions=revisions;
        for(auto it=state->tasks.begin();it!=state->tasks.end();) {
            if(!state->current(*it->second)) {
                it->second->cancellation->cancel();
                it=state->tasks.erase(it);
            } else ++it;
        }
    }
    bool submit(uint64_t node,uint64_t revision,Job job,TaskBinding& binding,std::string& why) {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto expected=state->revisions.find(node);
        if(state->closed || !job) {why="Task service is closed.";return false;}
        if(!node || expected==state->revisions.end() || expected->second!=revision) {
            why="Task inputs are no longer current.";return false;
        }
        auto previous=state->tasks.find(node);
        if(previous!=state->tasks.end() && taskActive(previous->second->status)) {
            why="This node already has an active task.";return false;
        }
        if(state->queue.size()>=state->queueLimit) {why="Background task queue is full.";return false;}
        auto task=std::make_shared<Task>();
        task->binding={state->session,node,revision,++state->serial};
        task->job=std::move(job);binding=task->binding;
        state->tasks[node]=task;state->queue.push_back(std::move(task));
        state->ready.notify_one();return true;
    }
    bool snapshot(uint64_t node,Snapshot& out) const {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found=state->tasks.find(node);
        if(found==state->tasks.end())return false;
        const auto& task=*found->second;
        out={task.binding,task.status,task.progress->load(std::memory_order_acquire),
             task.cancellation->requested.load(std::memory_order_acquire),task.result.message};
        return true;
    }
    void cancel(uint64_t node) {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found=state->tasks.find(node);
        if(found!=state->tasks.end()) {
            found->second->cancellation->cancel();
            // Cancellation is still effective after the worker finishes but
            // before the control thread claims its result.
            if(!taskActive(found->second->status)) {
                found->second->status=TaskStatus::Cancelled;
                found->second->result=TaskResult::cancelled();
            }
        }
    }
    std::vector<Completion> takeCompleted() {
        std::lock_guard<std::mutex> lock(state->mutex);
        std::vector<Completion> completed;
        for(auto& row:state->tasks) {
            auto& task=*row.second;
            if(taskActive(task.status) || task.delivered || !state->current(task))continue;
            task.delivered=true;
            completed.push_back({task.binding,std::move(task.result)});
            task.result.message=completed.back().result.message;
        }
        return completed;
    }
    void shutdown() {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(state->closed)return;
        state->closed=true;
        for(auto& row:state->tasks)row.second->cancellation->cancel();
        for(auto& task:state->queue)task->cancellation->cancel();
        state->tasks.clear();state->revisions.clear();
        state->ready.notify_all();
    }
    // Test/CLI shutdown hook. UI code must use nonblocking shutdown().
    bool waitIdle(std::chrono::milliseconds timeout) const {
        std::unique_lock<std::mutex> lock(state->mutex);
        return state->idle.wait_for(lock,timeout,[&]{return state->queue.empty() && state->active==0;});
    }
};
} // namespace soundnet
