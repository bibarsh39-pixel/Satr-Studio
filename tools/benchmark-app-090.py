"""Actual application Build/Run path, real external console, checked pipe control.

First = no application build cache in a new profile, not reboot-cold Windows.
External passes check exit status; the same fixtures' output is verified in the
pipe control. No shell, persistent interpreter, skipped assertions or changed code.
"""
from pathlib import Path
import json, os, shutil, statistics, subprocess, time

r=Path(__file__).resolve().parents[1]
out=r/'evidence/0.9.0/app-benchmark'/time.strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True)
fixtures=out/'fixtures';fixtures.mkdir()
for f in (r/'tests/four_languages').iterdir():
    if f.is_file():shutil.copyfile(f,fixtures/f.name)
cases=[]
def add(file,expected,source=None):
    if source is not None:(fixtures/file).write_text(source,encoding='utf8')
    cases.append({'file':file,'expected':expected})
add('hello.c','C_HELLO','#include <stdio.h>\nint main(void){puts("C_HELLO");return 0;}\n')
add('hello.cpp','CPP_HELLO','#include <iostream>\nint main(){std::cout<<"CPP_HELLO\\n";}\n')
add('hello.py','PY_HELLO','print("PY_HELLO")\n')
add('hello.js','JS_HELLO','console.log("JS_HELLO");\n')
add('JavaSmall.java','JAVA_SMALL:42')
add('JavaComplex.java','JAVA_COMPLEX:4999950000')
add('user_inventory.cpp','New Total Inventory Value: $9202.38')
add('c_complex.c','C_OK:399980000')
add('cpp_complex.cpp','CPP_OK:4999950000')
add('python_complex.py','PYTHON_OK:499500')
add('javascript_complex.js','JS_OK:332833500')
n=2000;expected=n*(n-1)
functions=''.join(f'int f{i}(int x){{return x+{i};}}\n' for i in range(n))
calls=''.join(f'  sum+=f{i}({i});\n' for i in range(n))
add('large.c',f'LARGE_C:{expected}','#include <stdio.h>\n'+functions+'int main(void){int sum=0;\n'+calls+f'printf("LARGE_C:%d\\n",sum);return sum!={expected};}}\n')
add('large.cpp',f'LARGE_CPP:{expected}','#include <iostream>\n'+functions+'int main(){int sum=0;\n'+calls+f'std::cout<<"LARGE_CPP:"<<sum<<"\\n";return sum!={expected};}}\n')
add('large.py',f'LARGE_PY:{expected}',''.join(f'def f{i}(x):\n    return x+{i}\n' for i in range(n))+'values=[\n'+''.join(f'    f{i}({i}),\n' for i in range(n))+f']\nassert sum(values)=={expected}\nprint("LARGE_PY:"+str(sum(values)))\n')
add('large.js',f'LARGE_JS:{expected}',''.join(f'function f{i}(x){{return x+{i};}}\n' for i in range(n))+'const values=[\n'+''.join(f'    f{i}({i}),\n' for i in range(n))+f'];\nconst sum=values.reduce((a,b)=>a+b,0);\nif(sum!=={expected})throw Error("wrong sum");\nconsole.log("LARGE_JS:"+sum);\n')
java_functions=''.join(f'    static int f{i}(int x){{return x+{i};}}\n' for i in range(n))
java_calls=''.join(f'        sum+=f{i}({i});\n' for i in range(n))
add('JavaLarge.java',f'JAVA_LARGE:{expected}','public class JavaLarge {\n'+java_functions+'    public static void main(String[] args){ int sum=0;\n'+java_calls+f'        if(sum!={expected})throw new AssertionError("Wrong result"); System.out.println("JAVA_LARGE:"+sum);\n    }}\n}}\n')
(fixtures/'values.h').write_text('#define MULTIPLIER 2\n')
app=r/'dist/windows/Satr.exe'
env=os.environ.copy();env['PATH']=r'C:\Windows\System32;C:\Windows'
for key in ['PYTHONHOME','PYTHONPATH','NODE_PATH','NODE_COMPILE_CACHE','NODE_DISABLE_COMPILE_CACHE','NODE_V8_COVERAGE','SATR_TEST_SCREENSHOTS']:env.pop(key,None)
report={'method':'Actual application Build(true) to completion, including compilation, linking, fresh child process, external console output and UI completion detection. App/file opening excluded. Prepared console starts before timed Run. First uses a fresh application build-cache profile, not cold OS caches. All cases run sequentially without simultaneous packaging/build jobs. Pipe control verifies exact expected output; external pass verifies exit code. Repeated median uses five samples, never only the fastest.', 'cases':{}, 'directory':str(out)}
for mode,external,runs in [('output-control',False,1),('external',True,6)]:
    directory=out/mode;shutil.copytree(fixtures,directory/'fixtures')
    (directory/'benchmark.json').write_text(json.dumps({'cases':cases,'external':external,'runs':runs}))
    env['LOCALAPPDATA']=str(directory/'local');(directory/'local').mkdir()
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    with (directory/'app.log').open('w') as log:
        process=subprocess.Popen([str(app),'--language-test',str(directory),'--portable-test'],env=env,stdout=log,stderr=log,startupinfo=startup)
        process.wait(timeout=900)
    result=json.loads((directory/'gui-smoke.json').read_text(encoding='utf8'))
    if not result['passed']:
        print(json.dumps([v for v in result['checks'] if not v['passed']],ensure_ascii=False,indent=2),flush=True)
        raise RuntimeError(f'{mode} failed')
    for case in cases:
        values=[v for v in result['checks'] if v['name']==case['file']+'_run']
        assert len(values)==runs,(case,values)
        record=report['cases'].setdefault(case['file'],{'sourceLines':len((fixtures/case['file']).read_text(encoding='utf8').splitlines()),'bytes':(fixtures/case['file']).stat().st_size})
        record[mode]=values
        if external:
            record['firstRunMs']=values[0]['elapsedMs']
            record['repeatMedianMs']=statistics.median(v['elapsedMs'] for v in values[1:])
            record['repeatMinMs']=min(v['elapsedMs'] for v in values[1:])
            record['repeatMaxMs']=max(v['elapsedMs'] for v in values[1:])
    (out/'results.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
    print(mode,'passed',len(result['checks']),'checks',flush=True)
    if external:
        for name,c in report['cases'].items():print(name,'first',c['firstRunMs'],'repeat median',c['repeatMedianMs'],'ms',flush=True)
(r/'evidence/0.9.0/app-benchmark/latest.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
print('Saved',out,flush=True)
