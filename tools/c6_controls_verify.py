"""Exercise C6 saved controls and real LCD cards; keep one owner USB session.

This deliberately restarts the device to verify NVS persistence, restores
brightness/eye/orientation settings afterward, and leaves a two-choice card
for optional human touch feedback. Does not format cards or modify firmware.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess,sys,time
from c6_verify import Session, require
ROOT=Path(__file__).resolve().parents[1]
CARDS=(
 'UI/title:HELLO TABY','UI/text:NEW SCREENS|Text and touch on C6',
 'UI/icon:water','UI/title_icon:DRINK WATER|water',
 'UI/title_icon_row:COFFEE BREAK|coffee',
 'UI/title_icon_subtitle:STAY FOCUSED|target|One thing at a time',
 'UI/title_action:OPEN APP|OPEN|phone',
 'UI/choice_2:CHOOSE|YES|NO|Touch a button',
 'UI/timer:FOCUS|65|300|Stay with it|run',
 'UI/progress:UPLOADING|45|Nearly halfway')


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True)
 p.add_argument('--output',type=Path,default=ROOT/'.local/c6-controls-verification.json')
 p.add_argument('--touch-seconds',type=int,default=0)
 a=p.parse_args();s=Session(a.port)
 report={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'passed':False,'cards':[]}
 old=None
 try:
  s.query('PING','TABY:');info=json.loads(s.query('INFO','TABY:INFO '))
  require(info['hardware_target']=='c6-lcd-1.47','Wrong hardware target')
  old={'brightness':json.loads(s.query('BRIGHTNESS?','TABY:BRIGHTNESS '))['percent'],
       'eye':json.loads(s.query('EYE_MOTION?','TABY:EYE_MOTION '))['mode'],
       'orientation':json.loads(s.query('DISPLAY_ORIENTATION?','TABY:DISPLAY_ORIENTATION '))['mode']}
  require(info['preferences_storage_ready'],'NVS unavailable')
  s.query('BRIGHTNESS 43','TABY:BRIGHTNESS ')
  require(json.loads(s.query('BRIGHTNESS?','TABY:BRIGHTNESS '))['percent']==43,'Brightness failed')
  s.query('BRIGHTNESS 101','TABY:ERR ')
  require(json.loads(s.query('BRIGHTNESS?','TABY:BRIGHTNESS '))['percent']==43,'Invalid value mutated brightness')
  for mode in ('calm','still','normal'):
   require(json.loads(s.query('EYE_MOTION '+mode,'TABY:EYE_MOTION '))['mode']==mode,'Eye mode failed')
  for mode in ('90','180','270','0'):
   result=json.loads(s.query('DISPLAY_ORIENTATION '+mode,'TABY:DISPLAY_ORIENTATION '))
   require(result['rotation_degrees']==int(mode),'Rotation failed')
   s.query('confirmation','TABY:OK ');time.sleep(.35)
   diag=s.diag();require(not diag['display_errors'] and not diag['decode_errors'],'Rotation rendering failed')
  if info['display_orientation_auto_supported']:
   s.query('DISPLAY_ORIENTATION auto','TABY:DISPLAY_ORIENTATION ')
   time.sleep(.25)
   auto=json.loads(s.query('DISPLAY_ORIENTATION?','TABY:DISPLAY_ORIENTATION '))
   require(auto['mode']=='auto' and auto['auto_supported'],'Auto selection failed')
   report['auto_mode_query']=auto
  s.query('BRIGHTNESS 54','TABY:BRIGHTNESS ');s.query('EYE_MOTION still','TABY:EYE_MOTION ')
  s.query('DISPLAY_ORIENTATION 180','TABY:DISPLAY_ORIENTATION ')
  s.connection.close()
  subprocess.run([sys.executable,str(ROOT/'tools/device.py'),'reset','--port',a.port],check=True,capture_output=True)
  s=Session(a.port);s.query('PING','TABY:')
  require(json.loads(s.query('BRIGHTNESS?','TABY:BRIGHTNESS '))['percent']==54,'Brightness not saved')
  require(json.loads(s.query('EYE_MOTION?','TABY:EYE_MOTION '))['mode']=='still','Eye mode not saved')
  require(json.loads(s.query('DISPLAY_ORIENTATION?','TABY:DISPLAY_ORIENTATION '))['rotation_degrees']==180,'Orientation not saved')
  report['persistence_across_hardware_reset']=True
  s.query('BRIGHTNESS 70','TABY:BRIGHTNESS ');s.query('EYE_MOTION normal','TABY:EYE_MOTION ')
  for orientation in ('0','90'):
   s.query('DISPLAY_ORIENTATION '+orientation,'TABY:DISPLAY_ORIENTATION ')
   for card in CARDS:
    result=s.query(card,'TABY:OK ');time.sleep(.15)
    require(s.diag()['state'].startswith('UI_'),'Card state not active')
    report['cards'].append({'orientation':int(orientation),'command':card,'reply':result})
  previous=s.diag()['state']
  s.query('UI/title!greeting*:REJECT','TABY:ERR ')
  require(s.diag()['state']==previous,'Unsupported effect replaced card')
  s.query('DISPLAY_ORIENTATION 0','TABY:DISPLAY_ORIENTATION ')
  s.query('UI/choice_2?c6-check:LOOKING GOOD?|YES|NO|Tap one button','TABY:OK ')
  initial=json.loads(s.query('CHOICE_SIGNAL','TABY:CHOICE_SIGNAL '))
  if a.touch_seconds:
   print('Awaiting physical choice/touch input...',flush=True)
   end=time.monotonic()+a.touch_seconds
   while time.monotonic()<end:
    time.sleep(.25)
    choice=json.loads(s.query('CHOICE_SIGNAL','TABY:CHOICE_SIGNAL '))
    if choice['signal']!=initial['signal']:break
  report['choice']=json.loads(s.query('CHOICE_SIGNAL','TABY:CHOICE_SIGNAL '))
  report['choice_changed']=report['choice']['signal']!=initial['signal']
  report['final']=s.diag()
  require(report['final']['display_errors']==0 and report['final']['decode_errors']==0,'Display error')
  require(report['final']['touch_errors']==0,'Touch controller error')
  report['passed']=True
  print(json.dumps(report,indent=2),flush=True)
 except Exception as e:
  report['error']=str(e);raise
 finally:
  if old:
   try:
    s.query('CLEAR','TABY:OK ')
    s.query('BRIGHTNESS '+str(old['brightness']),'TABY:BRIGHTNESS ')
    s.query('EYE_MOTION '+old['eye'],'TABY:EYE_MOTION ')
    s.query('DISPLAY_ORIENTATION '+old['orientation'],'TABY:DISPLAY_ORIENTATION ')
   except Exception as e:report['restore_error']=str(e)
  s.connection.close();a.output.parent.mkdir(parents=True,exist_ok=True)
  a.output.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
