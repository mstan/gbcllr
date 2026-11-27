#!/usr/bin/env python3
"""Prepared game review. UI adapted from SuperMetroidRecomp sm_replay_review.py."""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def stamp():
    return datetime.now(timezone.utc).isoformat(timespec='milliseconds')


def atomic_json(path, value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,indent=2),encoding='utf-8')
    temporary.replace(path)


class ReviewGame:
    def __init__(self,root,headless=False):
        self.root=root
        self.manifest=json.loads((root/'manifest.json').read_text())
        self.runtime=root/'runtime';self.runtime.mkdir(exist_ok=True)
        (self.runtime/'notes').mkdir(exist_ok=True)
        self.headless=headless;self.process=None;self.logs=[];self.serial=0
        self.case=None;self.paused=True;self.control=None

    def command(self,command):
        if not self.process or self.process.poll() is not None:
            raise RuntimeError('The test game is closed. Load a scene to restart it.')
        self.serial+=1
        request=self.control/('request-'+str(self.serial)+'.txt');temporary=request.with_suffix('.tmp')
        temporary.write_text(str(self.serial)+' '+command+'\n',encoding='utf-8');temporary.replace(request)
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            if self.process.poll() is not None:raise RuntimeError('Game exited; see runtime logs.')
            try:
                reply=json.loads((self.control/('reply-'+str(self.serial)+'.json')).read_text())
                if reply['id']==self.serial:
                    if not reply.get('ok'):raise RuntimeError(reply.get('error','Review request failed'))
                    self.paused=reply['paused'];return reply
            except (FileNotFoundError,PermissionError,json.JSONDecodeError):pass
            time.sleep(.02)
        raise RuntimeError('The test game has not replied yet. Its process was left running.')

    def close(self):
        if self.process and self.process.poll() is None:
            try:self.command('quit');self.process.wait(timeout=5)
            except Exception:
                if self.process.poll() is None:self.process.terminate();self.process.wait(timeout=10)
        self.process=None
        for f in self.logs:f.close()
        self.logs=[]

    def start(self,case,source=1):
        self.close();self.case=case
        exe=Path(case['exe'])
        if hashlib.sha256(exe.read_bytes()).hexdigest()!=case['binary_sha256']:
            raise RuntimeError('The prepared game build has changed. Keep this review bundle with its matching build.')
        if case.get('checkpoint_sha256') and hashlib.sha256(Path(case['checkpoint']).read_bytes()).hexdigest()!=case['checkpoint_sha256']:
            raise RuntimeError('The prepared checkpoint has changed. Restore its matching review bundle.')
        profile=self.runtime/'profiles'/case['id'];profile.mkdir(parents=True,exist_ok=True)
        seed=Path(case.get('profile_seed',self.root/'unused-profile'))
        for name in ['desktop.cfg','native.sav','native-dx-package.gbmod']:
            if not(profile/name).exists() and (seed/name).is_file():shutil.copy2(seed/name,profile/name)
        if case.get('dx_mod'):
            shutil.copy2(case['dx_mod'],profile/'native-dx-package.gbmod')
        (profile/'states').mkdir(exist_ok=True)
        if not(profile/'states/slot-1.state').exists():shutil.copy2(case['checkpoint'],profile/'states/slot-1.state')
        cfg=profile/'desktop.cfg'
        lines=cfg.read_text().splitlines()if cfg.exists()else['gbcllr-desktop 1']
        values={'fullscreen':'0','volume':'100','audio':'1','scale':'3','source':str(source),'adaptive_view':'1','view_width':'-1'}
        lines=[line for line in lines if line.split(' ',1)[0]not in values]
        cfg.write_text('\n'.join(lines+[k+' '+v for k,v in values.items()])+'\n')
        commands=profile/'start.commands'
        commands.write_text('load-state '+json.dumps(Path(case['checkpoint']).as_posix())+'\nview-width '+str(case.get('width',512))+' 144\n')
        self.control=self.runtime/'control'/(case['id']+'-'+str(time.time_ns()));self.control.mkdir(parents=True)
        env=os.environ.copy();env['GBCLLR_REVIEW_DIR']=str(self.control)
        env.pop('SDL_AUDIODRIVER',None);env.pop('SDL_VIDEODRIVER',None)
        if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin;'+env['PATH']
        if self.headless:env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        self.logs=[(self.control/'stdout.log').open('wb'),(self.control/'stderr.log').open('wb')]
        argv=[str(exe),'--rom',case['rom'],'--boot-rom',case['boot'],'--model',case['model'],'--mod-package',case['mod'],'--window','--skip-launcher','--profile',str(profile),'--commands',str(commands)]
        if self.headless:argv.append('--mute')
        self.process=subprocess.Popen(argv,cwd=self.root,env=env,stdout=self.logs[0],stderr=self.logs[1],creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.serial=0;state=self.command('state')
        if state['rom']!=case['rom_sha256']:raise RuntimeError('The loaded cartridge does not match this review scene.')
        if case.get('window_size'):
            self.command('resize '+' '.join(map(str,case['window_size'])))
        self.reset()
        atomic_json(self.root/'session.json',dict(pid=self.process.pid,scenario=case['id'],paused=True,started=stamp()))

    def reset(self):
        self.pause();self.command('loadstate '+json.dumps(Path(self.case['checkpoint']).as_posix()))
        return self.command('step 1 0')

    def state(self):return self.command('state')
    def play(self):return self.command('play')
    def pause(self):return self.command('pause')

    def capture(self,prefix):
        relative='notes/'+prefix
        screenshot=self.runtime/(relative+'.bmp');checkpoint=self.runtime/(relative+'.state')
        self.command('screenshot '+json.dumps(screenshot.as_posix()))
        self.command('savestate '+json.dumps(checkpoint.as_posix()))
        return dict(screenshot='runtime/'+relative+'.bmp',checkpoint='runtime/'+relative+'.state',state=self.state(),binary_sha256=self.case['binary_sha256'])


def run_ui(root):
    import tkinter as tk
    from tkinter import ttk,messagebox
    game=ReviewGame(root)
    path=root/'observations.json'
    notes=json.loads(path.read_text()) if path.exists() else dict(version=1,binary_sha256=game.manifest.get('source','prepared review'),scenarios={})
    heading=game.manifest.get('title','GB/GBC game review')
    window=tk.Tk();window.title(heading)
    height=min(game.manifest.get('panel_height',740),window.winfo_screenheight()-80)
    window.geometry(f'720x{height}+{max(20,window.winfo_screenwidth()-760)}+40');window.minsize(670,660)
    style=ttk.Style();style.configure('Title.TLabel',font=('Segoe UI',15,'bold'))
    body=ttk.Frame(window,padding=16);body.pack(fill='both',expand=True)
    ttk.Label(body,text=heading,style='Title.TLabel').pack(anchor='w')
    ttk.Label(body,text='Prepared checks. Restart affects only this private test game.').pack(anchor='w',pady=(4,8))
    listing=tk.Listbox(body,height=6,font=('Segoe UI',11),exportselection=False)
    listing.pack(fill='x')
    for case in game.manifest['scenarios']:listing.insert('end',case['id']+'   '+case['title'])
    listing.selection_set(0)
    controls=ttk.Frame(body);controls.pack(fill='x',pady=10)
    source=tk.StringVar(value=game.manifest.get('input','Keyboard'))
    ttk.Label(controls,text='Input:').pack(side='left')
    input_choice=ttk.Combobox(controls,textvariable=source,values=['Keyboard','Controller'],state='readonly',width=12)
    input_choice.pack(side='left',padx=(5,12))
    status=tk.StringVar(value='Preparing first scenario...')
    title=tk.StringVar();steps=tk.StringVar();expected=tk.StringVar();clip=tk.StringVar()
    ttk.Label(body,textvariable=title,font=('Segoe UI',12,'bold'),wraplength=670).pack(anchor='w',pady=(3,5))
    ttk.Label(body,textvariable=steps,wraplength=670,justify='left').pack(anchor='w',pady=4)
    ttk.Label(body,textvariable=expected,wraplength=670,justify='left').pack(anchor='w',pady=4)
    ttk.Label(body,textvariable=clip,foreground='#555555',wraplength=670).pack(anchor='w',pady=5)
    ttk.Separator(body).pack(fill='x',pady=7)
    ttk.Label(body,text='Your notes - what happened, where, and the movement that triggered it:').pack(anchor='w')
    text=tk.Text(body,height=6,font=('Segoe UI',11),wrap='word',undo=True);text.pack(fill='both',expand=True,pady=6)
    verdict=ttk.Frame(body);verdict.pack(fill='x',pady=5)
    history=tk.StringVar()
    ttk.Label(body,textvariable=history,wraplength=670).pack(anchor='w',pady=4)
    ttk.Label(body,textvariable=status,foreground='#174e7a',wraplength=670).pack(anchor='w',pady=5)
    ttk.Label(body,text=game.manifest.get('controls','Keyboard: arrows move | Z = A / jump | X = B / attack | Enter = Start | Esc = Mods')+'\nClick the game to play. Initial setup is paused. Closing this panel closes its test game.',wraplength=670,foreground='#555555').pack(anchor='w',pady=5)
    current=[None]
    pending_notes=None

    def flush_notes():
        nonlocal notes,pending_notes
        if pending_notes is None:return False
        atomic_json(path,pending_notes)
        notes=pending_notes;pending_notes=None
        return True

    def save_draft():
        nonlocal pending_notes
        if current[0]:
            staged=deepcopy(pending_notes if pending_notes is not None else notes)
            row=staged['scenarios'].setdefault(current[0]['id'],dict(observations=[]))
            value=text.get('1.0','end-1c')
            if row.get('draft')!=value:
                row['draft']=value;staged['updated']=stamp();pending_notes=staged
        return flush_notes()

    def describe(case):
        title.set(case['title']);steps.set(case['steps']);expected.set(case['expected'])
        clip.set(case.get('scope','Brief prepared check; other content remains untested.'))
        row=notes['scenarios'].get(case['id'],{});events=row.get('observations',[])
        history.set('Not reviewed yet.' if not events else 'Saved observations: '+str(len(events))+' | Latest: '+events[-1]['status']+' | Earlier failures remain in history.')

    def safe(action):
        try:action()
        except Exception as e:status.set(str(e));messagebox.showerror('Review setup',str(e))

    def load():
        save_draft();case=game.manifest['scenarios'][listing.curselection()[0]]
        status.set('Loading '+case['id']+'...');window.update_idletasks()
        game.start(case,2 if source.get()=='Controller' else 1)
        current[0]=case;text.delete('1.0','end');text.insert('1.0',notes['scenarios'].get(case['id'],{}).get('draft',''))
        describe(case);status.set('Paused and ready. Click Play, then click the game window.')

    def play():
        game.play();status.set('Playing. Use Pause here to freeze a problem; Save screenshot + note records the current view.')

    def pause():
        game.pause();status.set('Paused. Write your observation, or click Play to continue.')

    def restart():
        save_draft();game.reset();status.set('Scenario restored and paused. Click Play to retry.')

    def record(value):
        nonlocal pending_notes
        save_draft();case=current[0]
        game.pause()
        prefix=datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+case['id']
        evidence=game.capture(prefix)
        staged=deepcopy(notes)
        row=staged['scenarios'].setdefault(case['id'],dict(observations=[]))
        row['observations'].append(dict(at=stamp(),status=value,note=text.get('1.0','end-1c'),**evidence))
        staged['updated']=stamp();pending_notes=staged;flush_notes();describe(case)
        status.set('Paused. Saved '+value+' with screenshot. Notes: observations.json')

    ttk.Button(controls,text='Load selected (paused)',command=lambda:safe(load)).pack(side='left',padx=3)
    ttk.Button(controls,text='Restart',command=lambda:safe(restart)).pack(side='left',padx=3)
    ttk.Button(controls,text='Play',command=lambda:safe(play)).pack(side='left',padx=3)
    ttk.Button(controls,text='Pause',command=lambda:safe(pause)).pack(side='left',padx=3)
    input_choice.bind('<<ComboboxSelected>>',lambda event:safe(load))
    for label,value in [('Bug found','Bug found'),('Pass (reviewed)','Pass'),('Defer','Deferred'),('Save screenshot + note','Note')]:
        ttk.Button(verdict,text=label,command=lambda value=value:safe(lambda:record(value))).pack(side='left',padx=(0,6))
    def autosave():
        retrying=pending_notes is not None
        try:
            changed=save_draft()
            if changed and current[0]:describe(current[0])
            if changed and retrying:status.set('Pending notes saved. Your observation history is intact.')
        except Exception as e:
            status.set('Notes are not saved yet; retrying automatically. '+str(e))
        finally:window.after(3000,autosave)
    def close():
        try:save_draft()
        except Exception as e:
            status.set('Unsaved notes kept here. The review panel is still open.')
            messagebox.showerror('Notes not saved','The review panel was kept open so your notes can be saved.\n\n'+str(e))
            return
        game.close();window.destroy()
    window.protocol('WM_DELETE_WINDOW',close)
    window.after(100,lambda:safe(load));window.after(3000,autosave)
    try:window.mainloop()
    finally:game.close()


def verify(root):
    game=ReviewGame(root,headless=True);results=[]
    try:
        for case in game.manifest['scenarios']:
            game.start(case);initial=game.state();time.sleep(.1)
            assert game.state()['cycles']==initial['cycles'],'initial pause advanced'
            stepped=game.command('step 8 0');assert stepped['cycles']>initial['cycles']
            game.play();time.sleep(.15);game.pause();before=game.state();time.sleep(.1)
            assert game.state()['cycles']==before['cycles'],'paused game advanced'
            first=game.capture('control-check-'+case['id']);second=game.capture('control-check-again-'+case['id'])
            assert (root/first['screenshot']).read_bytes()==(root/second['screenshot']).read_bytes(),'paused screenshot changed'
            game.reset();assert game.state()['cycles']==initial['cycles'],'restart did not restore scene'
            results.append(dict(id=case['id'],initial=initial,stepped=stepped,paused=before,evidence=first))
            print(case['id']+' selector load/pause/play/restart/capture passed',flush=True)
        atomic_json(root/'selector-storage-check.json',dict(status='Automated control/storage check; not human acceptance',results=results))
    finally:game.close()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',required=True,type=Path);p.add_argument('--verify',action='store_true')
    a=p.parse_args();verify(a.root.resolve())if a.verify else run_ui(a.root.resolve())
