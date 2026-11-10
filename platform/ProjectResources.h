#pragma once
// Public control-thread project resource API. Resolved node configurations and
// AU states own their resources; disk manifests carry platform-managed refs.
#import <Foundation/Foundation.h>
#include "resources/MidiClip.hpp"

namespace tri::projectresources {
// Resource: {kind, name, data} for audio/clip/model, or
// {kind:"text", name, extension, text}. No filesystem paths are persisted here.
NSDictionary *importAsset(NSString *kind, NSURL *url, NSString **why);
NSDictionary *encodeClip(NSString *name, const MidiClip& clip, NSString **why);
bool decodeClip(NSDictionary *asset, MidiClip& clip, NSString **why);
NSData *audioFileData(NSDictionary *asset, NSString **why);
NSDictionary *assetResource(NSString *kind, NSDictionary *asset);
NSDictionary *textResource(NSString *name, NSString *extension, NSString *text);
// Collections contain {id,name,metadata,resources:{role:resourceID}} entries.
NSArray *collection(NSDictionary *node, NSString *key);
NSDictionary *entryResource(NSDictionary *node, NSDictionary *entry, NSString *role);
NSDictionary *appendEntry(NSDictionary *node, NSString *collectionKey, NSString *name,
                          NSDictionary *resourcesByRole, NSDictionary *metadata);
bool validate(NSDictionary *node, NSString **why, bool decodeResources=true);
}
