"""Exercise the installed Taby native Bluetooth bridge against the exact USB board.

Close the desktop app first. Optional touch checks show LEFT and RIGHT cards;
this does not read or change the desktop app's personal data or remembered link.
"""
import argparse
import asyncio
from datetime import datetime,timezone
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
from bleak import BleakScanner
from c6_verify import Session,require
ROOT=Path(__file__).resolve().parents[1]
SERVICE='54414259-0000-4000-a000-000000000001'

class NativeBridge:
 def __init__(self,path):
  self.process=subprocess.Popen([str(path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,bufsize=1)
  self.lines=queue.Queue();self.sequence=0
  def read():
   for line in self.process.stdout:
    self.lines.put(line)
   self.lines.put(None)
  threading.Thread(target=read,daemon=True).start()
 def request(self,op,**fields):
  self.sequence+=1;self.process.stdin.write(json.dumps({'id':self.sequence,'op':op,**fields})+'\n');self.process.stdin.flush()
  try:line=self.lines.get(timeout=16)
  except queue.Empty:raise TimeoutError('Native Bluetooth bridge timed out') from None
  require(line is not None and len(line)<16384,'Native bridge response missing/oversized')
  reply=json.loads(line);require(reply.get('id')==self.sequence,'Native bridge sequence mismatch')
  require(reply.get('ok') is True,'Native bridge: '+(reply.get('error') if isinstance(reply.get('error'),str) and re.fullmatch('[a-z_]{1,60}',reply['error']) else 'invalid_response'))
  return reply.get('result')
 def close(self):
  if self.process.poll() is None:
   self.process.stdin.close()
   try:self.process.wait(timeout=3)
   except subprocess.TimeoutExpired:self.process.terminate();self.process.wait(timeout=3)

def identity(value,info):
 require(isinstance(value,dict) and set(value)=={'protocol','deviceId','firmwareVersion'},'Native identity shape')
 require(value['protocol']==2 and value['deviceId']==info['device_id'] and value['firmwareVersion']==info['firmware_version'],'Native identity mismatch')
def choice(value):
 require(isinstance(value,dict) and set(value)=={'signal','choice'} and type(value['signal']) is int and 0<=value['signal']<=0xffffffff and value['choice'] in ('left','right','none'),'Native choice schema')
 return value

async def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);p.add_argument('--helper',type=Path,default=Path('/Applications/Taby.app/Contents/Resources/native/taby-bluetooth-probe'));p.add_argument('--touch-seconds',type=int,default=0);p.add_argument('--output',type=Path,default=ROOT/'.local/c6-native-bluetooth-verification.json');args=p.parse_args()
 require(args.helper.is_file(),'Installed native bridge is missing')
 s=Session(args.port);bridge=None;old_orientation=None;r={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'transport':'installed_app_native_bridge','rounds':[],'touch_checks':[]}
 try:
  info=json.loads(s.query('INFO','TABY:INFO '));require(info['hardware_target']=='c6-lcd-1.47','Wrong board');require(info.get('asset_storage')=='sd_card' and info['animation_count']==84,'Power-cycle the board to initialize the full SD pack before this test')
  old_orientation=json.loads(s.query('DISPLAY_ORIENTATION?','TABY:DISPLAY_ORIENTATION '))['mode']
  s.query('TRANSPORT_MODE ble','TABY:OK ')
  expected='Taby-'+info['device_id'][-8:]
  dev=await BleakScanner.find_device_by_filter(lambda d,a:d.name==expected and SERVICE in a.service_uuids,timeout=15)
  require(dev is not None,'Exact board advertisement not found')
  for n in range(2):
   bridge=NativeBridge(args.helper)
   candidates=bridge.request('scan');target=next((x for x in candidates if x.get('peripheralId','').lower()==dev.address.lower()),None)
   require(target is not None,'Exact board absent from native scan')
   r['native_identifier_case_matches_bleak']=target['peripheralId']==dev.address
   # The installed helper treats its opaque scan identifiers literally.
   identity(bridge.request('connect',peripheralId=target['peripheralId']),info)
   identity(bridge.request('state'),info);choice(bridge.request('choice'))
   bridge.request('send',command='confirmation')
   bridge.request('send',command='UI/title_subtitle?c6-native:DESKTOP CONNECTED|Native Bluetooth bridge')
   require(s.diag()['state'].startswith('UI_'),'Native text presentation not applied')
   bridge.request('send',command='UI/timer?c6-native-timer:CHECK|1|1||run|0');require(s.diag()['state'].startswith('UI_'),'Native timer not applied')
   bridge.request('send',command='S')
   orient=bridge.request('orientation',command='DISPLAY_ORIENTATION 90')
   require(re.fullmatch(r'OK DISPLAY_ORIENTATION 90 90 90 [01] rectangle',orient or '') is not None,'Native orientation response')
   require(bridge.request('orientation',command='DISPLAY_ORIENTATION?')==orient,'Native orientation readback mismatch')
   require(json.loads(s.query('DISPLAY_ORIENTATION?','TABY:DISPLAY_ORIENTATION '))['mode']=='90','USB canonical orientation mismatch')
   bridge.request('orientation',command='DISPLAY_ORIENTATION left')
   if n==0 and args.touch_seconds:
    for selection,label in [('left','LEFT'),('right','RIGHT')]:
     before=choice(bridge.request('choice'))['signal']
     bridge.request('send',command=f'UI/choice_2?c6-native-{selection}:TAP {label}|LEFT|RIGHT|Bluetooth touch check')
     print('Waiting for the button labelled '+label,flush=True)
     end=time.monotonic()+args.touch_seconds
     while time.monotonic()<end:
      value=choice(bridge.request('choice'))
      if value['signal']>before and value['choice']==selection:break
      time.sleep(.15)
     else:raise TimeoutError('No physical '+label+' choice observed')
     legacy=json.loads(s.query('CHOICE_SIGNAL','TABY:CHOICE_SIGNAL '))
     require(legacy['signal']==value['signal'] and legacy['selection']==('choice_1' if selection=='left' else 'choice_2'),'Choice transports disagree')
     r['touch_checks'].append({'choice':selection,'signal':value['signal'],'legacy_agrees':True})
   bridge.request('send',command='S');r['rounds'].append({'round':n,'identity_matches_usb':True,'state_identity':True,'choice_schema':True,'animation':True,'text':True,'timer':True,'stop':True,'orientation_set_and_readback':True})
   bridge.close();bridge=None;await asyncio.sleep(.7)
  r['firmware_version']=info['firmware_version'];r['final']=s.diag();require(all(r['final'][k]==0 for k in ('decode_errors','display_errors','touch_errors')),'Driver errors during native bridge test');r['passed']=True
 except BaseException as e:r.update(passed=False,error=type(e).__name__+': '+str(e));raise
 finally:
  if bridge:bridge.close()
  try:
   if old_orientation is not None:s.query('DISPLAY_ORIENTATION '+old_orientation,'TABY:DISPLAY_ORIENTATION ')
   s.query('CLEAR','TABY:OK ')
  except Exception as e:r['restore_error']=type(e).__name__
  s.connection.close();args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(r,indent=2)+'\n')
if __name__=='__main__':asyncio.run(main())
