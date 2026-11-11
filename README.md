# Soundnet

English · [简体中文](README.zh-CN.md)

Soundnet is a native macOS app for composing, playing, and processing music on a node canvas. Connect note sources to instruments and effects, route audio from a DAW through a patch, or use triggers to decide when clips start and stop. Run the same project in the standalone app or as an Audio Unit v2 instrument.

![Soundnet canvas with the five node categories, a three-voice arrangement across four color zones, and the selected node’s inspector](docs/workflow.png)

*The [Three-Voice Arrangement](examples/three-voice-arrangement.v4.json) patch, laid out on the current canvas.*

## Try a project

1. Open Soundnet and choose **Open**. Load [Three Loops, Then Wait](examples/three-loops-then-wait.json).
2. Click **Play**. The first MIDI clip plays three loops and stops, then a trigger starts the second clip one second later. Click **Stop** or press **⌘.** to stop the graph.
3. Drag a node in from the library. Pull a cable from an output jack and release it over a compatible input.

Try [Audio Clip and Compressor](examples/audio-clip-compressor.json) for audio effects, [Sampler Pluck](examples/sampler-pluck.json) for a MIDI-controlled instrument, or [Clip to MIDI](examples/clip-to-midi.json) for MIDI output. See the [example notes](examples/README.md) for what starts each patch.

## Work on the canvas

The node library groups **Notes**, **Sound Sources**, and **Audio Processing** as creative nodes. **Transport & Control** and **Project & I/O** have a separate background. Search in English or Chinese, click a result to insert it, or drag it onto the canvas.

The canvas follows the signal path through four softly blended zones: **Note Sources**, **Instruments**, **Effects**, and **Outputs**. Zone boundaries follow the distribution of the nodes. You can place cards freely inside their zone. While you drag a new node from the library, the destination zone can widen and neighboring zones can slide sideways with their cards and cables. Releasing commits the insertion and the move together. **Esc** cancels and **⌘Z** undoes a completed drop. Hold **Shift** while dropping if the canvas prompts for more room.

Cables follow the pointer and snap to a compatible input. Node cards show key parameters and, where available, an audio waveform. Select a node to edit its full settings in the inspector.

## Play, process, and export

- **Compose and arrange.** Edit MIDI clips in a piano roll with note and controller lanes. Build chord progressions, split them into three voices, or send MIDI to external instruments.
- **Build instruments and effects.** Play oscillators or samples, process stereo audio through effects, and use triggers for loops, delays, and conditional playback. Timecode provides a shared reference, and nodes that use tempo set it individually.
- **Work with a DAW.** The standalone app uses the audio devices selected in Project Settings. The AU receives MIDI from its host instrument track, reads incoming audio through the host sidechain, and returns stereo audio to the track.
- **Render and reuse.** Record input or output to WAV, or render the graph offline as stereo WAV, separate stems, or a standard MIDI file.
- **Generate locally.** The optional YuE2 and ACE-Step nodes use a locally installed audio.cpp CLI. Generated takes can be auditioned and saved with the project. YuE2 can also turn its ABC score into an editable MIDI clip.

Soundnet saves projects as v6 JSON. When a project has a matching `.assets` folder, keep it alongside the JSON when moving or backing up the project.

## Build and install

Soundnet 3.0.0 targets macOS 13 and later on Apple Silicon and Intel. Use Xcode Command Line Tools with C++23 support.

```sh
./build-all.sh build/release
./scripts/package-app.sh build/release
./scripts/package-release.sh build/release
```

The build writes the app, the AU component, and the node SDK to `build/release/`. The packaging scripts produce the standalone installer and an App + AU ZIP under `build/`. Local binaries are ad hoc signed. The installer is unsigned and the artifacts are not Apple-notarized.

To install the AU, copy `Soundnet.component` into `~/Library/Audio/Plug-Ins/Components/` and reopen the host. In Logic Pro, choose **AU Instruments → Soundnet → Soundnet** on a software instrument track. Soundnet and the earlier Ternary component share an identifier, so install only one of them.

```sh
app=build/release/Soundnet.app/Contents/MacOS/Soundnet
"$app" --validate examples/three-voice-arrangement.v4.json
"$app" --export examples/three-loops-then-wait.json output.wav 8
./scripts/test-all.sh build/tests
"$app" --open examples/three-voice-arrangement.v4.json
```

## Develop a node

Soundnet uses SDK 2 and node package ABI 2. Each node lives under `nodes/<slug>/` and builds into its own `.soundnode` package. Install a compatible package in `~/Library/Application Support/Soundnet/Nodes` and click **Refresh** in the node library. Packages with the same key can override the bundled version, and ordinary updates take effect without rebuilding the host.

Read [AGENTS.md](AGENTS.md), the [project constitution](docs/项目宪法.md), and the [node authoring guide](docs/编写新节点.md). See the [Chinese user guide](docs/使用说明.txt) for operating details, the [changelog](CHANGELOG.md) for release history, and the [3.0.0 verification record](docs/3.0.0-基础验收.md) for release checks and their scope.

## Credits and license

Thanks to [hehongze88@gmail.com](mailto:hehongze88@gmail.com) for the synthesizer setup, modulation and sound-generation logic, and sound mapping for the high, middle, and low voices. Santzhm186 submitted that contribution to Soundnet.

Soundnet’s own code uses the [MIT License](LICENSE). The bundled Apple AudioUnitSDK retains its [Apache-2.0 license](au/vendor/LICENSE.txt). See [NOTICE](NOTICE).
