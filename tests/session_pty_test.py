"""Exercise the real reusable POSIX terminal: fresh process, stdin, termios, stop."""
import os,pty,json,time,select,signal,sys,pathlib
runner=pathlib.Path(sys.argv[1]).resolve();root=pathlib.Path(sys.argv[2]).resolve();root.mkdir(parents=True,exist_ok=True)
request=root/'request.json';result=root/'result.json'
request.write_text('{}')
pid,fd=pty.fork()
if pid==0:os.execv(str(runner),[str(runner),'--session',str(request),str(result)])
checks=[]
def check(ok,name):
 checks.append({'name':name,'passed':bool(ok)});print('PASS' if ok else 'FAIL',name,flush=True)
 if not ok:raise AssertionError(name)
def submit(i,script):
 temporary=root/'request.tmp';temporary.write_text(json.dumps({'id':str(i),'program':'/usr/bin/python3','cwd':str(root),'args':['-c',script]},ensure_ascii=False));temporary.replace(request)
def await_result(i,interactive=False):
 output=b'';deadline=time.monotonic()+10;sent=False
 while time.monotonic()<deadline:
  if select.select([fd],[],[],.02)[0]:
   output+=os.read(fd,65536)
   if interactive and b'INPUT_READY' in output and not sent:os.write(fd,b'21\n');sent=True
  try:
   data=json.loads(result.read_text())
   if data['id']==str(i):return data,output
  except (FileNotFoundError,ValueError):pass
 raise TimeoutError(output)
try:
 submit(1,"import os,sys;assert sys.stdin.isatty() and sys.stdout.isatty();print('INPUT_READY',flush=True);assert input()=='21';print('VALUE',42);sys.exit(7)")
 data,out=await_result(1,True);check(data['code']==7 and b'VALUE 42' in out,'real_tty_input_and_exit')
 submit(2,"import os,termios;print('FRESH',os.environ.get('SATR_SESSION_VARIABLE','missing'));os.environ['SATR_SESSION_VARIABLE']='modified';a=termios.tcgetattr(0);a[3]&=~termios.ECHO;termios.tcsetattr(0,termios.TCSANOW,a)")
 data,out=await_result(2);check(data['code']==0 and b'FRESH missing' in out,'second_run_fresh_process')
 submit(3,"import os,termios;assert termios.tcgetattr(0)[3]&termios.ECHO;assert 'SATR_SESSION_VARIABLE' not in os.environ;print('RESTORED')")
 data,out=await_result(3);check(data['code']==0 and b'RESTORED' in out,'terminal_modes_and_environment_restored')
 check(os.waitpid(pid,os.WNOHANG)==(0,0),'same_runner_survives_three_runs')
 os.write(fd,b'\n');until=time.monotonic()+3
 while time.monotonic()<until:
  ended,status=os.waitpid(pid,os.WNOHANG)
  if ended:break
  time.sleep(.02)
 check(ended==pid and os.waitstatus_to_exitcode(status)==0,'enter_closes_idle_console')
finally:
 try:os.kill(pid,signal.SIGKILL)
 except ProcessLookupError:pass
 os.close(fd)
 (root/'result-tests.json').write_text(json.dumps(checks,indent=2))
