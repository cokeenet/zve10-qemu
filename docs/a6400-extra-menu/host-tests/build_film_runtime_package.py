#!/usr/bin/env python3
"""Build an independent, pinned cold-boot package. Never install or connect."""
import hashlib
import json
from pathlib import Path
import re
import sys

TEMPLATES=Path(__file__).with_name('film-runtime-v2-templates')
DESTS={
 'ui':'a6400-film-ui-v3-persist.so','menu_job':'a6400-film-menu-job.elf',
 'worker':'a6400-film-job.elf','native':'a6400-film-native-command.elf',
 'ram_job':'a6400-film-ram-job.sh','capture_daemon':'a6400-film-capture-daemon.elf',
 'guard':'a6400-ui-load-guard.elf','check':'a6400-check-persist.req',
 'install':'a6400-install-persist.req','remove':'a6400-remove-persist.req'}
OPTIONAL={'ui_stop':'a6400-film-ui-stop.elf'}

def digest(data):return hashlib.sha256(data).hexdigest()
def fnv(data):
 h=2166136261
 for n in data:h=((h^n)*16777619)&0xffffffff
 return f'{h:08x}'
def require(condition,message):
 if not condition:raise ValueError(message)
def load_artifacts(spec,base):
 require(spec.get('schema')==1,'unsupported schema')
 require(spec.get('default_enabled') is False,'initial state must be OFF')
 require(spec.get('capture_persistent') is True,'capture daemon must be a reviewed persistent build')
 rows=spec.get('artifacts');require(isinstance(rows,list),'artifacts must be a list')
 artifacts={}
 for row in rows:
  role=row.get('role');require(role in (DESTS|OPTIONAL),'unknown artifact role')
  require(role not in artifacts,'duplicate artifact role')
  dest=(DESTS|OPTIONAL)[role];require(row.get('dest')==dest,'incorrect fixed runtime destination')
  p=Path(row['file']);p=p if p.is_absolute() else base/p
  require(p.is_file() and not p.is_symlink(),'artifact must be a regular file')
  data=p.read_bytes();expected=row.get('sha256','')
  require(re.fullmatch('[0-9a-f]{64}',expected) is not None,'invalid artifact SHA-256')
  require(digest(data)==expected,'artifact SHA-256 mismatch: '+role)
  if role=='ram_job':
   require(not any(x in data for x in (b'__NATIVE_SHA__',b'__WORKER_SHA__',b'__SETTINGS_SHA__')),'RAM job pins not filled')
  artifacts[role]={'dest':dest,'source':str(p.resolve()),'data':data,'sha256':expected,
                   'mode':0o644 if role in ('check','install','remove') else 0o755}
 require(DESTS.keys()<=artifacts.keys(),'missing required artifacts: '+','.join(sorted(DESTS.keys()-artifacts.keys())))
 p=Path(spec['menu_pins_file']);p=p if p.is_absolute() else base/p
 pins=json.loads(p.read_text());ui=artifacts['ui']
 require(pins.get('path')=='/tmp_bt/'+ui['dest'],'menu reader module pathname mismatch')
 require(pins.get('sha256')==ui['sha256'] and pins.get('bytes')==len(ui['data']) and pins.get('fnv')==fnv(ui['data']),'menu reader module pins mismatch')
 require(pins.get('lifetime_ms')==0 and pins.get('build_defines',{}).get('FILM_SETTINGS_PERSISTENT')==1,'menu reader must match persistent UI')
 require(pins.get('ui_bytes')==1120 and re.fullmatch(r'0x[0-9a-f]+',pins.get('ui_rva','')) is not None,'invalid UI layout pins')
 p=Path(spec['capture_pins_file']);p=p if p.is_absolute() else base/p
 capture_pins=json.loads(p.read_text())
 require(capture_pins.get('persistent') is True,'capture build is not persistent')
 require(capture_pins.get('runtime')=='/tmp_bt/'+artifacts['capture_daemon']['dest'] and
         capture_pins.get('arguments')==['watch'],'capture runtime command mismatch')
 for key,role in [('script','ram_job'),('settings','menu_job')]:
  pin=capture_pins.get(key,{})
  artifact=artifacts[role]
  require(pin.get('runtime')=='/tmp_bt/'+artifact['dest'] and
          pin.get('sha256')==artifact['sha256'] and pin.get('bytes')==len(artifact['data']) and
          pin.get('fnv')==fnv(artifact['data']),'capture '+key+' pins mismatch')
 require(capture_pins.get('daemon_sha256')==artifacts['capture_daemon']['sha256'] and
         capture_pins.get('daemon_bytes')==len(artifacts['capture_daemon']['data']),
         'capture build receipt ELF missing or mismatched')
 return artifacts,pins,capture_pins

def render(name,replacements):
 text=(TEMPLATES/name).read_text()
 for key,value in replacements.items():text=text.replace('@@'+key+'@@',str(value))
 require(re.search(r'@@[A-Z_]+@@',text) is None,'unfilled script template')
 return text.encode()

def build(spec_path,out):
 spec=json.loads(spec_path.read_text())
 artifacts,pins,capture_pins=load_artifacts(spec,spec_path.parent)
 require(not out.exists() and not out.is_symlink(),'output already exists')
 package_id=digest(json.dumps({'artifacts':{k:v['sha256'] for k,v in sorted(artifacts.items())},
                              'module_pins':pins,'capture_pins':capture_pins},sort_keys=True).encode())[:16]
 rows='\n'.join(f"{v['dest']} {v['dest']} {v['sha256']} {v['mode']:o}" for v in artifacts.values())
 values={'PACKAGE_ID':package_id,'ARTIFACT_ROWS':rows,
         'GUARD_SHA':artifacts['guard']['sha256'],'CHECK_SHA':artifacts['check']['sha256'],
         'INSTALL_SHA':artifacts['install']['sha256']}
 runtime=render('runtime-start.sh',values)
 stage_rows=rows+'\n'+f'runtime-start.sh a6400-film-runtime-start.sh {digest(runtime)} 755'
 prepare=render('prepare.sh',dict(values,ROWS=stage_rows))
 bootstrap=render('bootstrap-v2.sh',dict(values,PREPARE_SHA=digest(prepare),RUNTIME_SHA=digest(runtime)))
 files={f"A6400/RUNTIME/{v['dest']}":(v['data'],v['mode']) for v in artifacts.values()}
 files.update({'A6400/RUNTIME/runtime-start.sh':(runtime,0o755),
               'A6400/RUNTIME/prepare.sh':(prepare,0o755),'bootstrap-v2.sh':(bootstrap,0o755),
               'A6400/start.sh':(b'#!/bin/sh\n# Copy-only compatibility entry.\nexec /bin/sh /mnt/A6400/RUNTIME/prepare.sh "$@"\n',0o755)})
 files['menu-pins.json']=((json.dumps(pins,indent=2,sort_keys=True)+'\n').encode(),0o644)
 files['capture-pins.json']=((json.dumps(capture_pins,indent=2,sort_keys=True)+'\n').encode(),0o644)
 checksums=''.join(digest(data)+'  '+name+'\n' for name,(data,mode) in sorted(files.items()))
 files['SHA256SUMS']=(checksums.encode(),0o644)
 manifest={'schema':2,'package_id':package_id,'default_enabled':False,'capture_persistent':True,
           'module_pins':pins,'capture_pins':capture_pins,
           'daemon_command':['/tmp_bt/'+DESTS['capture_daemon'],'watch'],
           'stop_marker':'/tmp_bt/a6400-film-capture.stop',
           'deployment_status':'BUILT_ONLY_NOT_INSTALLED',
           'natural_touch_release_required':True,'early_boot_max_seconds':30,
           'files':[{'path':name,'sha256':digest(data),'bytes':len(data),'mode':f'{mode:o}'}
                    for name,(data,mode) in sorted(files.items())],
           'artifacts':[{k:v for k,v in item.items() if k!='data'}|{'role':role}
                        for role,item in artifacts.items()]}
 out.mkdir()
 for name,(data,mode) in files.items():
  p=out/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);p.chmod(mode)
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
 print('FILM_RUNTIME_PACKAGE_BUILT out='+str(out)+' package='+package_id)

def main():
 if len(sys.argv)!=3:
  print('usage: build_film_runtime_package.py ARTIFACTS.json NEW_OUTPUT_DIRECTORY',file=sys.stderr);return 2
 try:build(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).absolute())
 except (ValueError,KeyError,TypeError,OSError) as e:
  print('FILM_RUNTIME_PACKAGE_FAIL '+str(e),file=sys.stderr);return 1
 return 0
if __name__=='__main__':sys.exit(main())
