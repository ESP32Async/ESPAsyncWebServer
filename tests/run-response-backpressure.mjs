// SPDX-License-Identifier: LGPL-3.0-or-later
// Run: node tests/run-response-backpressure.mjs [--baseline WebResponses.cpp] [--output EXECUTABLE]
// No SDK/library installation, download, board contact or extra compiler cache.
// Compile the actual method from the exact reviewed source with transport mocks.
import {readFileSync,mkdirSync} from 'node:fs';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const here=dirname(fileURLToPath(import.meta.url)),root=resolve(here,'..');
const options={};for(let i=2;i<process.argv.length;i+=2){
  if(!['--baseline','--output'].includes(process.argv[i])||!process.argv[i+1])throw Error('Use optional --baseline SOURCE and --output EXECUTABLE');
  options[process.argv[i]]=resolve(process.argv[i+1]);
}
const out=options['--output']||resolve(root,'build/validation/response-backpressure'+(process.platform==='win32'?'.exe':''));
mkdirSync(dirname(out),{recursive:true});
const harness=readFileSync(resolve(here,'response_backpressure.cpp'),'utf8');
if(!readFileSync(resolve(root,'src/WebResponseImpl.h'),'utf8').includes('bool _source_ended{false};'))throw Error('Patched production response is missing its EOF state');
function method(path,baseline){
  const source=readFileSync(path,'utf8'),start=source.indexOf('size_t AsyncAbstractResponse::write_send_buffs('),
      end=source.indexOf('size_t AsyncAbstractResponse::_readDataFromCacheOrContent(',start);
  if(start<0||end<0)throw Error('Actual method boundaries changed; no fixture executed');
  const digest=createHash('sha256').update(source).digest('hex');
  if(baseline&&digest!=='ec9481a3183e53f3ec56a3ab22faae926403aadaf3afb4e41551d04f915d75e7')throw Error('Baseline is not reviewed upstream4d4eaca source');
  console.log(path+' sha256='+digest);
  return harness.replace('// ACTUAL_IMPLEMENTATION',()=>source.slice(start,end));
}
for(const baseline of options['--baseline']?[true,false]:[false])for(const credits of [0,1]){
  const path=baseline?options['--baseline']:resolve(root,'src/WebResponses.cpp');
  const compiled=spawnSync(process.env.CXX||'g++',['-std=c++17','-Wall','-Wextra','-Werror','-O0','-g0',
    '-DASYNCWEBSERVER_USE_CHUNK_INFLIGHT='+credits,'-x','c++','-','-o',out],
    {input:method(path,baseline),encoding:'utf8',windowsHide:true,timeout:30000});
  if(compiled.error||compiled.status!==0)throw Error('Fixture compile failed: '+compiled.stderr);
  const result=spawnSync(out,[],{encoding:'utf8',windowsHide:true,timeout:20000});
  console.log((baseline?'BASELINE':'PATCHED')+' credits='+credits+' exit='+result.status+' '+result.stdout+result.stderr);
  if(result.error||result.status!==(baseline?1:0))throw Error('Unexpected baseline/patch fixture outcome');
}
