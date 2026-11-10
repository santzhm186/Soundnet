#!/bin/bash
# Build one fixed platform/runner and three small packages, then refresh all
# revisions in one process. Never compile the repository's 46 node packages.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$(mktemp -d /tmp/soundnet-node-packages.XXXXXX)}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
arch="${SOUNDNET_BUILD_ARCH:-$(uname -m)}"
export PYTHONDONTWRITEBYTECODE=1
SOUNDNET_BUILD_ARCH="$arch" "$root/scripts/build-platform.sh" "$out/platform"
library="$out/platform/libSoundnetPlatform.dylib"
"$root/scripts/export-node-sdk.sh" "$library" "$out/sdk"
xcrun clang++ -std=c++17 -O2 -fobjc-arc -DSOUNDNET_DYNAMIC_NODES=1 \
  -arch "$arch" -mmacosx-version-min=13.0 -I"$root" \
  "$root/tests/NodePackageTests.mm" "$library" \
  -framework Cocoa -framework CoreML -framework AVFoundation \
  -Wl,-rpath,"$out/platform" -o "$out/NodePackageTests"
python3 - "$out" <<'PY'
import hashlib, json, sys
from pathlib import Path
base=Path(sys.argv[1])
# Fixture sources exist only under this test's build directory. The platform
# and runner have already been compiled without seeing any of these headers.
for variant,key,slug,factor,default in [
    ('v1','test.refresh-fixture','refresh-fixture',1,.25),
    ('v2','test.refresh-fixture','refresh-fixture',2,.5),
    ('additional','test.additional','additional',3,.25)]:
    directory=base/'source'/variant/slug;directory.mkdir(parents=True,exist_ok=True)
    (directory/'Resources').mkdir(exist_ok=True)
    (directory/'Resources'/'value.txt').write_text('1\n')
    namespace=slug.replace('-','_')
    (directory/'Node.hpp').write_text('''#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include <fstream>
#include <stdexcept>
// soundnet-module: KEY
namespace tri::nodes::NAMESPACE {
inline constexpr int gainID=parameterID("KEY","gain");
inline constexpr ParamInfo gainInfo{"Gain",0,4,DEFAULT,"x"};
class Kernel final:public NodeKernel {
    float value=0;
public:
    void prepare(double,int) override {}
    void configure(const KernelConfig& config) override {
        if(config.value(gainID,DEFAULT)==4.f)throw std::runtime_error("fixture configure exception");
        float resource=-1000;std::ifstream input(config.resourceDirectory+"/value.txt");input>>resource;
        value=config.value(gainID,DEFAULT)*FACTOR*resource;
    }
    void reset(KernelResetReason) noexcept override {}
    void process(KernelContext& context) noexcept override {
        if(auto* output=context.output("audio"))for(int i=0;i<context.frames;++i) {
            if(output->left)output->left[i]=value;if(output->right)output->right[i]=value;
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
inline bool validate(const KernelConfig& config,std::string&) {
    if(config.value(gainID,DEFAULT)==3.f)throw std::runtime_error("fixture validator exception");
    return true;
}
inline NodeModule module() {
    NodeSpec spec;spec.key="KEY";spec.type=nodeTypeID(spec.key);spec.title="Refresh fixture";spec.category="Test";
    spec.capabilities=CapGenerator|CapStream|CapOutputSink;spec.timeMode=TimeMode::Stream;
    spec.inputs={{"optional","Optional",Signal::Audio,RoleOutput,PortOptional}};
    spec.outputs={{"audio","Audio",Signal::Audio,RoleMain,0}};spec.params={gainID};spec.kernelFactory=create;spec.validateConfig=validate;
    return {std::move(spec),{{gainID,ParamSpec{&gainInfo,"gain",false}}}};
}
}
'''.replace('KEY',key).replace('NAMESPACE',namespace).replace('DEFAULT',str(default)+'f').replace('FACTOR',str(factor)+'.f'))
    (directory/'Editor.hpp').write_text('''#pragma once
#include "../../platform/NodeEditor.h"
@interface SNPackageFixtureView : NSView
-(NSInteger)revisionMarker;
@end
namespace tri::nodes::NAMESPACE {
inline NSView* editor(NodeEditorContext) {return [[SNPackageFixtureView alloc]initWithFrame:NSMakeRect(0,0,40,20)];}
}
'''.replace('NAMESPACE',namespace))
    (directory/'View.mm').write_text('''#include "Editor.hpp"
@implementation SNPackageFixtureView
-(NSInteger)revisionMarker {return MARKER;}
@end
'''.replace('MARKER',str(factor)))
for path in [base/'platform'/'libSoundnetPlatform.dylib',base/'NodePackageTests']:
    (base/(path.name+'.before.sha256')).write_text(hashlib.sha256(path.read_bytes()).hexdigest())
PY
for variant in v1 v2 additional; do
  module=refresh-fixture
  [[ "$variant" == additional ]] && module=additional
  python3 "$out/sdk/scripts/build-node.py" "$out/source/$variant/$module" "$out/$variant" \
    --sdk "$out/sdk" --platform-library "$out/sdk/lib/libSoundnetPlatform.dylib" --arch "$arch"
done
python3 - "$out" "$arch" <<'PY'
import hashlib,json,shutil,subprocess,sys
from pathlib import Path
base=Path(sys.argv[1]);key='test.refresh-fixture.soundnode'
(base/'empty').mkdir(exist_ok=True)
for kind in ['duplicate','bad-sdk','bad-sha','bad-resource','resource-only']:
    target=base/kind
    if target.exists():shutil.rmtree(target)
    shutil.copytree(base/'v2',target)
    if kind!='resource-only':
        shutil.copytree(base/'additional'/'test.additional.soundnode',target/'test.additional.soundnode')
    manifest=target/key/'manifest.json';data=json.loads(manifest.read_text())
    if kind=='duplicate':shutil.copytree(target/key,target/'duplicate.soundnode')
    elif kind=='bad-sdk':data['sdkVersion']=999
    elif kind=='bad-sha':data['sha256']='0'*64
    elif kind in ['bad-resource','resource-only']:
        resource=target/key/'Resources'/'value.txt';resource.write_text('0.5\n')
        if kind=='resource-only':data['resources']['value.txt']=hashlib.sha256(resource.read_bytes()).hexdigest()
    manifest.write_text(json.dumps(data,indent=2)+'\n')
assert (base/'v2'/key/'Node.dylib').read_bytes()==(base/'resource-only'/key/'Node.dylib').read_bytes()
# Deliberately incompatible entries are loaded once, rejected, and remembered.
# Each call leaves a marker so repeated rejection proves entry() is not retried.
for kind in ['bad-abi','entry-throws']:
    target=base/kind;target.mkdir(exist_ok=True)
    shutil.copytree(base/'additional'/'test.additional.soundnode',target/'test.additional.soundnode',dirs_exist_ok=True)
    package=target/('test.'+kind+'.soundnode');package.mkdir(exist_ok=True)
    source=base/(kind+'.mm')
    body='throw std::runtime_error("fixture entry exception");' if kind=='entry-throws' else 'static tri::NodePackageAPI api=[] {tri::NodePackageAPI a;a.abi=999;return a;}();return &api;'
    source.write_text('#include "platform/NodePackage.hpp"\n#include <cstdlib>\n#include <fstream>\n#include <stdexcept>\n'
        'extern "C" __attribute__((visibility("default"))) const tri::NodePackageAPI* soundnet_node_v2(){'
        'if(const char* path=std::getenv("SOUNDNET_PACKAGE_ENTRY_LOG"))std::ofstream(path,std::ios::app)<<"'+kind+'\\n";'+body+'}\n')
    library=package/'Node.dylib'
    subprocess.run(['xcrun','clang++','-std=c++17','-O1','-fobjc-arc','-fvisibility=hidden','-DSOUNDNET_DYNAMIC_NODES=1','-dynamiclib','-arch',sys.argv[2],'-mmacosx-version-min=13.0','-I'+str(base/'sdk'),str(source),'-framework','Foundation','-o',str(library)],check=True)
    subprocess.run(['codesign','--force','--sign','-',str(library)],check=True)
    (package/'manifest.json').write_text(json.dumps({'key':'test.'+kind,'packageABI':2,'sdkVersion':2,'library':'Node.dylib','sha256':hashlib.sha256(library.read_bytes()).hexdigest(),'resources':{}}))
# Exported builds have no host, generated registry, or neighboring node code.
for name in ('src','ui','nodes'):assert not (base/'sdk'/name).exists()
linked=base/'source'/'symlink'/'refresh-fixture'
if linked.exists():shutil.rmtree(linked)
shutil.copytree(base/'source'/'v1'/'refresh-fixture',linked,dirs_exist_ok=True)
(linked/'Forbidden.hpp').unlink(missing_ok=True)
(linked/'Forbidden.hpp').symlink_to(base/'sdk'/'platform'/'NodeKernel.hpp')
rejected=subprocess.run(['python3',str(base/'sdk'/'scripts'/'build-node.py'),str(linked),str(base/'symlink-rejected'),'--sdk',str(base/'sdk'),'--platform-library',str(base/'sdk'/'lib'/'libSoundnetPlatform.dylib'),'--arch',sys.argv[2]],capture_output=True,text=True)
assert rejected.returncode and 'symbolic links' in rejected.stderr,rejected.stdout+rejected.stderr
print('PASS: public-only exported SDK builds separate Objective-C sources and rejects source symlinks')
PY
python3 - "$out" <<'PY'
import subprocess,sys,time
from pathlib import Path
base=Path(sys.argv[1]);(base/'code-cache-inodes.json').unlink(missing_ok=True)
for attempt in (1,2):
    begin=time.perf_counter()
    subprocess.run([str(base/'NodePackageTests'),str(base)],check=True)
    print(f'Node package process {attempt}: {time.perf_counter()-begin:.3f} seconds',flush=True)
PY
python3 - "$out" <<'PY'
import hashlib,sys
from pathlib import Path
base=Path(sys.argv[1])
for path in [base/'platform'/'libSoundnetPlatform.dylib',base/'NodePackageTests']:
    assert hashlib.sha256(path.read_bytes()).hexdigest()==(base/(path.name+'.before.sha256')).read_text(),str(path)+' was rebuilt or changed'
print('PASS: platform library and host runner SHA-256 unchanged across every node update')
PY
