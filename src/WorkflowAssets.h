#pragma once
#import <Foundation/Foundation.h>
#include "MidiClip.hpp"
#include "NoteModel.hpp"
#include "GraphSpec.hpp"

namespace triassets {
// The portable dictionary contains the original SMF and/or an evaluated pitch map.
// All parsing, model loading, and memory allocation take place off the render thread.
NSDictionary *importAsset(NSString *kind, NSURL *url, NSString **why);
bool decode(NSDictionary *assets, tri::MidiClip &clip, tri::NoteModel &model, NSString **why);
bool decodeGraph(NSDictionary *document, tri::GraphSpec &graph, NSString **why);
bool validate(NSDictionary *assets, NSString **why);
}
