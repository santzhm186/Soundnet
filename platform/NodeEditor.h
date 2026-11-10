#pragma once
// Optional editor ABI. Only Objective-C++ UI targets include this header.
// Core kernels and descriptors have no Cocoa dependency. Commit replaces one
// node through the host's normal validation/undo/autosave transaction.
#import <Cocoa/Cocoa.h>
#include "TaskService.hpp"
namespace tri {
// Invoked on the owning editor/control thread for a still-current task. Return
// replacement node content to commit through validation, undo and autosave, or
// nil to leave configuration unchanged. Never write the document from a worker.
using NodeTaskApply=std::function<NSDictionary*(const soundnet::TaskResult&,NSDictionary* current)>;
struct NodeEditorContext {
    NSDictionary* configuration=nil;
    NSDictionary* environment=nil;
    NSString* resourceDirectory=nil;
    void (^commit)(NSDictionary* replacement)=nil;
    // Creates a node through one validated, undoable project transaction.
    // The host binds this action to the originating live project and instance.
    bool (^createNode)(NSString* nodeKey,NSDictionary* configuration)=nil;
    bool (^isCurrent)(void)=nil;
    void (^sendMidi)(uint8_t status,uint8_t data1,uint8_t data2)=nil;
    void (^resumePlayback)(void)=nil;
    void (^stopPlayback)(void)=nil;
    bool (^audioInputEnabled)(void)=nil;
    void (^setAudioInputEnabled)(bool)=nil;
    NSString* (^localize)(NSString* text)=nil;
    // Available only to CapTask modules. These closures are bound to this
    // project/node instance; modules cannot change the service's session or
    // cancel another node. Call all three closures only on the main/editor
    // thread. Off-thread submit/snapshot return false (submit explains why),
    // and cancel is a no-op. Only Job runs on a worker; its captures must own
    // immutable request data and must not access editor objects or this context.
    std::function<bool(soundnet::TaskService::Job,NodeTaskApply,std::string&)> submitTask;
    std::function<void()> cancelTask;
    std::function<bool(soundnet::TaskService::Snapshot&)> taskSnapshot;
    // An inspector can stay open across edits. Capture the latest committed
    // configuration and matching submission callback before building a job.
    // Returns false if the inspector no longer owns the selected node.
    std::function<bool(NodeEditorContext&)> captureTaskContext;
    NSDictionary* (^midiCapture)(NSString* action,int tempoParameter)=nil;
    bool recordingMidi=false;
    NSString* text(NSString* value) const {return localize?localize(value):value;}
};
using NodeEditorFactory=NSView*(*)(NodeEditorContext);
}
