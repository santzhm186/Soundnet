# Soundnet

English · [简体中文](README.zh-CN.md)

Soundnet is a native macOS editor for building music workflows from connected nodes. Route MIDI, stereo audio, timecode, and triggers through a patch to build an instrument, arrange a progression, or process incoming audio. Soundnet **2.9.1** runs as a standalone app and as an Audio Unit v2 instrument for hosts such as Logic Pro.

![Example Soundnet workflow with MIDI clips, synthesizers, and trigger routing](docs/workflow.png)

## Get started

1. Open the standalone app and choose **Open** in the toolbar.
2. Load [Three Loops, Then Wait](examples/three-loops-then-wait.json).
3. Click **Play**. The first MIDI clip loops three times, stops, and starts the second clip after a one-second wait. Click **Stop** or press **⌘.** to stop the entire workflow.

Use the node library to search by English or Chinese name, category, or stable node ID. Click a node to insert it or drag it to a specific spot. Connect an output to a matching input, then select the node to edit its parameters. The inspector groups related controls in collapsible sections. Use the language selector at the top right to switch between English and Simplified Chinese.

## What you can build

| Workflow | Example patch |
| --- | --- |
| Play a software instrument | Local MIDI → Oscillator → Audio Output |
| Arrange a chord progression | Chord Progression → Three-Voice Arranger → three Oscillators → Mixer → Audio Output |
| Record and process audio | Audio Input → EQ → Compressor → Audio Output |
| Trigger a clip or loop | Start Trigger → Audio Clip → Limiter → Audio Output |
| Send notes to another instrument | MIDI Clip → Scale Quantizer → MIDI Output |
| Generate a song, then edit its score | AI Music → Audio Output, and its ABC score → MIDI Clip → Scale Quantizer |

New projects include one Timecode node and one Project Settings node. Timecode supplies a common timeline; tempo belongs to each musical node, with no global BPM or separate tempo node. Project Settings controls the processing format and standalone audio devices; AU devices and the effective sample rate remain host-managed.

The same Audio Input and Audio Output nodes work in both editions. The standalone app uses the devices selected in Project Settings. The AU reads the host sidechain and returns stereo audio to the host track. Audio Output also feeds offline renders and stems.

Each node instance has its own parameters, assets, and runtime state. MIDI, stereo audio, trigger, and timecode ports are color coded. One output can feed several inputs; ordinary inputs accept one source, while trigger inputs can accept several. Soundnet validates connections before executing the graph.

- **Instruments and arrangement:** oscillators, polyphonic sampling, chord progressions, arpeggiation, and a three-voice arranger with independently configurable high, middle, and low pitch ranges.
- **Audio processing:** mixing, EQ, delay, filtering, reverb, compression, limiting, modulation, and more. Supported effects can be bypassed without removing their connections.
- **Clips and control:** MIDI clips with a piano-roll editor, audio clips with reverse and loop controls, start/stop triggers, event counters, waits, and probability gates.
- **Recording and export:** record audio input or output to WAV, or render a workflow offline as stereo WAV, separate WAV stems, or a standard MIDI file. Offline export supports cancellation.
- **Local generation:** an AI Music node drives a locally installed audio.cpp CLI (ACE-Step 1.5 or YuE2) in the background. The result becomes an embedded audio asset, so playback, export and stems behave exactly like an audio clip; YuE2 also returns an ABC score that converts into an editable MIDI clip for the piano roll, and the edited score can be fed back for another render. Each node has its own stop control for cancelling a run or stopping playback.
- **Local pitch models:** map MIDI pitches with a compatible Core ML model or a JSON pitch table. Try [PitchMirror](examples/PitchMirror.mlmodel) with [FourBeat](examples/FourBeat.mid).

Try [Three-Voice Arrangement](examples/three-voice-arrangement.v4.json), [Sampler Pluck](examples/sampler-pluck.json), or [Audio Clip and Compressor](examples/audio-clip-compressor.json). The [Chinese user guide](docs/使用说明.txt) covers editing shortcuts, routing, and node controls in more detail.

## Install on macOS

Soundnet requires macOS 13 or later and runs on Apple Silicon and Intel Macs.

- **Standalone:** open the local `Soundnet-2.9.1-macos.pkg` to install the app in `/Applications`, or extract the ZIP and copy `Soundnet.app` there.
- **Audio Unit:** copy `Soundnet.component` from the ZIP into `~/Library/Audio/Plug-Ins/Components/`, then reopen the host. In Logic Pro, load it from **AU Instruments → Soundnet → Soundnet**. The component uses the same identifier as the earlier Ternary plug-in; keep one copy of that component installed.

Local builds are ad hoc signed. The installer package is unsigned, and the artifacts are not Apple-notarized.

## Build from source

Use Xcode Command Line Tools with C++23 support. From the repository root:

```sh
./build-all.sh build/release
./scripts/package-app.sh build/release
./scripts/package-release.sh build/release
```

The build produces universal `Soundnet.app` and `Soundnet.component` bundles in `build/release/`. Packaging writes the standalone installer and an App + AU ZIP under `build/`. The ZIP also includes example workflows, both READMEs, and license notices.

The standalone executable can render workflows without opening the editor:

```sh
app=build/release/Soundnet.app/Contents/MacOS/Soundnet
"$app" --validate examples/three-voice-arrangement.v4.json
"$app" --export examples/three-loops-then-wait.json output.wav 8 48000 24
"$app" --export-stems examples/audio-clip-compressor.json stems 8 48000 24
"$app" --export-midi examples/clip-to-midi.json output.mid 8
"$app" --self-test
```

## Project format and development

Workflows use version 4 JSON with stable node, port, and node-local parameter names. Unsupported node versions are retained as inactive placeholders in the editor and refused by strict export. In older projects, replace legacy I/O with Audio Input and Audio Output. Replace version-1 Arpeggiator, MIDI Clip, Chord Progression, and Velocity nodes with current instances, then set their BPM locally. This repository’s examples use the current node versions.

Start development with [AGENTS.md](AGENTS.md), the [project constitution](docs/项目宪法.md), and the [node authoring guide](docs/编写新节点.md). New processing modules own a directory under `nodes/`, with `Node.hpp`, `Kernel.hpp`, `Tests.cpp`, and an optional `Editor.hpp`; pure background modules can use `Task.hpp` instead of an audio kernel. Builds discover the descriptor and editor headers automatically. Each module also owns its bilingual display strings. The public contracts live in `platform/NodeDescriptor.hpp`, `platform/NodeKernel.hpp`, and `platform/TaskService.hpp`; new modules do not add type branches to the central renderer.

`gain`, `midi-split`, and `timecode` demonstrate independent kernels; the hidden `task-example` module demonstrates background work through public editor callbacks. Kernels receive prepared, immutable audio, MIDI, and model handles from `KernelConfig`, with public resource types in `platform/resources/`.

Existing descriptors and processor sources now belong to their node directories. Some algorithms still use the transitional engine and require migration one node at a time; an explicit adapter list prevents new modules from depending on that path. See the [implementation map](docs/节点与连接标准-实现映射.md) for the supported interfaces and remaining work.

Run `./scripts/test-all.sh` for the regression suite and `./scripts/verify-release.sh` for release checks, including universal builds, signatures, and example renders. See the [changelog](CHANGELOG.md) for version history.

## Acknowledgments

Thanks to [hehongze88@gmail.com](mailto:hehongze88@gmail.com) for building the code that sets up the synthesizer nodes, implements modulation and sound-generation logic, and maps sound across the high, middle, and low voices. Santzhm186 submitted that contribution to Soundnet.

## License

Soundnet's own code is available under the [MIT License](LICENSE). The bundled Apple AudioUnitSDK retains its original [Apache-2.0 license](au/vendor/LICENSE.txt). See [NOTICE](NOTICE) for attribution and third-party information.
