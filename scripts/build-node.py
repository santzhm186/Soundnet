#!/usr/bin/env python3
"""Compile one independent Soundnet SDK 2 node package; never rebuild the host."""
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile
from pathlib import Path

def build(source, destination, sdk, library, architectures):
    source=source.resolve();sdk=sdk.resolve();library=library.resolve()
    declaration=(source/'Node.hpp').read_text()
    match=re.search(r'^// soundnet-module: ([a-z0-9][a-z0-9_.-]+)$',declaration,re.M)
    if not match:raise ValueError('Node.hpp must declare soundnet-module')
    key=match[1];namespace=source.name.replace('-','_')
    if not re.fullmatch(r'[a-z][a-z0-9_]*',namespace):raise ValueError('Invalid node directory name')
    destination.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.node-build-',dir=destination) as work:
        work=Path(work);stage=work/'source';(stage/'nodes').mkdir(parents=True)
        (stage/'platform').symlink_to(sdk/'platform',target_is_directory=True)
        local=stage/'nodes'/source.name
        shutil.copytree(source,local,symlinks=True,ignore=shutil.ignore_patterns('.DS_Store','__pycache__','*.o'))
        for path in local.rglob('*'):
            if path.is_symlink():raise ValueError('Node packages must not contain symbolic links')
        digest=hashlib.sha256()
        for base in [local,sdk/'platform']:
            for path in sorted(base.rglob('*')):
                if path.is_file() and (path.suffix in ('.hpp','.h','.cpp','.mm') or 'Resources' in path.relative_to(base).parts):
                    digest.update(str(path.relative_to(base)).encode());digest.update(path.read_bytes())
        revision=digest.hexdigest();suffix='_'+key.replace('.','_').replace('-','_')+'_'+revision[:16]
        classes=set()
        for base in [local,sdk/'platform']:
            for path in base.rglob('*'):
                if path.is_file() and path.suffix in ('.h','.hpp','.mm'):
                    classes.update(re.findall(r'@interface\s+([A-Za-z_]\w*)',path.read_text(errors='ignore')))
        aliases=stage/'ClassAliases.h'
        aliases.write_text('\n'.join('#define '+name+' '+name+suffix for name in sorted(classes))+'\n')
        wrapper='#include "platform/NodePackage.hpp"\n#include "nodes/'+source.name+'/Node.hpp"\n'
        editor='nullptr'
        if (local/'Editor.hpp').exists():
            wrapper+='#include "nodes/'+source.name+'/Editor.hpp"\n'
            editor='reinterpret_cast<void*>(&tri::nodes::'+namespace+'::editor)'
        wrapper+='extern "C" __attribute__((visibility("default"))) const tri::NodePackageAPI* soundnet_node_v2(){\n'
        wrapper+='static const tri::NodePackageAPI api=[] { tri::NodePackageAPI a; a.key="'+key+'"; a.module=&tri::nodes::'+namespace+'::module; a.editor='+editor+'; '+('a.editorContextSize=sizeof(tri::NodeEditorContext);' if (local/'Editor.hpp').exists() else '')+' return a; }();return &api;}\n'
        entry=stage/'Entry.mm';entry.write_text(wrapper)
        package=work/(key+'.soundnode');package.mkdir()
        sources=[str(p) for p in sorted(local.rglob('*')) if p.suffix in ('.cpp','.mm') and not p.name.endswith(('Tests.cpp','Tests.mm')) and 'tests' not in p.relative_to(local).parts]
        command=['xcrun','clang++','-std=c++17','-O2','-fobjc-arc','-fvisibility=hidden','-DSOUNDNET_DYNAMIC_NODES=1','-dynamiclib','-mmacosx-version-min=13.0','-I'+str(stage),'-include',str(aliases)]
        for architecture in architectures:command+=['-arch',architecture]
        command += [str(entry),*sources,str(library),'-install_name','@rpath/'+key+'.dylib']
        for framework in ['Cocoa','CoreML','AVFoundation','AudioToolbox','UniformTypeIdentifiers','QuartzCore']:command+=['-framework',framework]
        command+=['-o',str(package/'Node.dylib')]
        subprocess.run(command,check=True)
        resources=local/'Resources'
        if resources.is_dir():shutil.copytree(resources,package/'Resources')
        subprocess.run(['codesign','--force','--sign','-',str(package/'Node.dylib')],check=True,stdout=subprocess.DEVNULL)
        manifest={'key':key,'packageABI':2,'sdkVersion':2,'revision':revision,'library':'Node.dylib','sha256':hashlib.sha256((package/'Node.dylib').read_bytes()).hexdigest(),'resources':{}}
        if (package/'Resources').exists():
            for path in sorted((package/'Resources').rglob('*')):
                if path.is_file():manifest['resources'][str(path.relative_to(package/'Resources'))]=hashlib.sha256(path.read_bytes()).hexdigest()
        (package/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
        target=destination/package.name
        previous=work/'previous'
        if target.exists():target.rename(previous)
        try:package.rename(target)
        except BaseException:
            if previous.exists():previous.rename(target)
            raise
        return target

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path,help='Node directory, or all')
    parser.add_argument('destination',type=Path)
    parser.add_argument('--sdk',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--platform-library',type=Path,required=True)
    parser.add_argument('--arch',action='append',choices=['arm64','x86_64'])
    args=parser.parse_args();architectures=args.arch or ['arm64','x86_64']
    if str(args.source)=='all' and not (args.sdk/'nodes').is_dir():
        parser.error('all requires a source checkout with nodes/; an exported SDK builds one explicit node directory')
    sources=sorted((args.sdk/'nodes').glob('*/Node.hpp')) if str(args.source)=='all' else [args.source/'Node.hpp']
    for entry in sources:
        if entry.parent.name.startswith('_'):continue
        print('Built '+str(build(entry.parent,args.destination,args.sdk,args.platform_library,architectures)),flush=True)
if __name__=='__main__':main()
