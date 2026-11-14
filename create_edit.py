from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np, imageio, imageio_ffmpeg, math, wave, subprocess, os
W,H,FPS,DUR=720,1280,24,18
src=Image.open('assets/cosmos.jpg').convert('RGB')
# crop image to portrait viewport
sw,sh=src.size
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
font_b='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
def ease(x): return x*x*(3-2*x)
def draw_text(draw, text, y, size, color=(255,255,255), bold=True, tracking=1):
    f=ImageFont.truetype(font_b if bold else font,size)
    # centered, multi-line
    lines=text.split('\n'); heights=[]
    for line in lines: heights.append(draw.textbbox((0,0),line,font=f,stroke_width=0)[3])
    yy=y-sum(heights)//2
    for line,hh in zip(lines,heights):
        box=draw.textbbox((0,0),line,font=f); x=(W-(box[2]-box[0]))//2
        draw.text((x+2,yy+4),line,font=f,fill=(0,0,0,210),stroke_width=2,stroke_fill=(0,0,0,160))
        draw.text((x,yy),line,font=f,fill=color)
        yy+=hh+8

def frame(t):
    # slow push into the horizon; subtle drift
    p=t/DUR; scale=1.08+0.16*ease(p)
    cw,ch=int(W/scale),int(H/scale)
    # image source crop and resize
    ar=sw/sh; target=W/H
    if ar>target:
        crop_h=sh; crop_w=int(sh*target)
        left=int((sw-crop_w)*(.48+.04*p)); top=0
    else:
        crop_w=sw; crop_h=int(sw/target)
        left=0; top=int((sh-crop_h)*(.52-.08*p))
    im=src.crop((left,top,left+crop_w,top+crop_h)).resize((W,H),Image.Resampling.LANCZOS)
    # dark vignette/gradient and cinematic bars
    arr=np.asarray(im).astype(np.float32)
    yy,xx=np.mgrid[0:H,0:W]; nx=(xx-W/2)/(W/2); ny=(yy-H/2)/(H/2)
    vign=np.clip(1-.27*(nx*nx+ny*ny),.55,1)
    arr*=vign[:,:,None]
    im=Image.fromarray(np.uint8(np.clip(arr,0,255))).convert('RGBA')
    overlay=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(overlay)
    # gentle lower gradient
    for y in range(760,H):
        a=int(120*(y-760)/(H-760)); d.line((0,y,W,y),fill=(2,8,19,a))
    im=Image.alpha_composite(im,overlay)
    d=ImageDraw.Draw(im)
    # top micro-label
    small=ImageFont.truetype(font_b,18)
    label='КИНО, КОТОРОЕ ОСТАЁТСЯ С ТОБОЙ'
    bb=d.textbbox((0,0),label,font=small); d.text(((W-bb[2]+bb[0])//2,66),label,font=small,fill=(231,195,128,240))
    scenes=[(0,3.2,'А ЕСЛИ\nДОМ — ЭТО НЕ МЕСТО?'),(3.2,6.5,'А ЕСЛИ ДОМ —\nЭТО ТЕ, КТО ЖДЁТ?'),(6.5,10.0,'МИЛЛИОНЫ КИЛОМЕТРОВ\nНЕ ОТМЕНЯЮТ ОДНОГО…'),(10,13.8,'ОБЕЩАНИЯ\nВЕРНУТЬСЯ.'),(13.8,16.3,'ВО ВСЕЛЕННОЙ\nЕСТЬ СИЛА СИЛЬНЕЕ ВРЕМЕНИ.'),(16.3,18,'ЛЮБОВЬ.\nОНА НАХОДИТ ПУТЬ.')]
    for a,b,txt in scenes:
        if a<=t<b:
            alpha=min(1,(t-a)/.35,(b-t)/.28); alpha=max(0,min(alpha,1))
            # highlight last word when possible
            draw_text(d,txt,880,39 if t<13.8 else 34,(255,255,255),True)
            # thin gold rule
            d.rounded_rectangle((W//2-34,1014,W//2+34,1018),radius=2,fill=(233,185,101,int(235*alpha)))
            break
    # end slate title
    if t>14.8:
        alpha=min(1,(t-14.8)/.5)
        f=ImageFont.truetype(font_b,17); text='ТВОЯ ИСТОРИЯ — ТВОЯ ГАЛАКТИКА'
        bb=d.textbbox((0,0),text,font=f); d.text(((W-(bb[2]-bb[0]))//2,1170),text,font=f,fill=(235,195,128,int(245*alpha)))
    # subtle film bars
    d.rectangle((0,0,W,20),fill=(0,0,0,95)); d.rectangle((0,H-20,W,H),fill=(0,0,0,95))
    return np.asarray(im.convert('RGB'))

# render video frames
raw='edit_video_only.mp4'
writer=imageio.get_writer(raw,fps=FPS,codec='libx264',quality=8,macro_block_size=1,ffmpeg_params=['-pix_fmt','yuv420p'])
for i in range(FPS*DUR): writer.append_data(frame(i/FPS))
writer.close()
# original ambient score: layered warm synth-like sine pads, slow pulse, no sampled material
sr=44100; n=sr*DUR; t=np.arange(n)/sr
# chord movement designed for the emotional build
freqs=[[55,82.41,110,164.81],[49,73.42,98,146.83],[41.2,61.74,82.41,123.47],[55,82.41,110,164.81]]
audio=np.zeros(n,dtype=np.float64)
for sec in range(4):
    ix=(t>=sec*4)&(t<(sec+1)*4); local=t[ix]-sec*4
    attack=np.minimum(local/1.4,1); release=np.minimum((4-local)/.8,1); env=attack*release
    chord=np.zeros(ix.sum())
    for j,f in enumerate(freqs[sec]): chord += np.sin(2*np.pi*f*t[ix]+j*.45)*(.20/(j+1)**.5)
    audio[ix]+=chord*env
# high, distant shimmer + sparse low pulse
for f,a in [(329.63,.025),(392,.018),(493.88,.014)]: audio+=a*np.sin(2*np.pi*f*t+np.sin(2*np.pi*.08*t))*np.sin(np.pi*np.minimum(t/DUR,1))
pulse=(np.sin(2*np.pi*1.0*t)>0.94).astype(float)
pulse=np.convolve(pulse,np.ones(2500)/2500,mode='same')
audio+=pulse*.10*np.sin(2*np.pi*55*t)
fade=np.minimum(t/1.3,1)*np.minimum((DUR-t)/1.8,1)
audio*=fade
# stereo width, normalize softly
st=np.column_stack([audio, audio*.92+0.025*np.sin(2*np.pi*220*t)])
st=np.clip(st/np.max(np.abs(st))*.75,-1,1)
with wave.open('score.wav','wb') as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr); w.writeframes((st*32767).astype('<i2').tobytes())
ff=imageio_ffmpeg.get_ffmpeg_exe()
subprocess.run([ff,'-y','-i',raw,'-i','score.wav','-c:v','copy','-c:a','aac','-b:a','192k','-shortest','-movflags','+faststart','-metadata','title=ЛЮБОВЬ НАХОДИТ ПУТЬ — кино-эдит','-metadata','comment=Оригинальный фан-эдит без фрагментов фильма','shorts_edit.mp4'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
os.remove(raw); os.remove('score.wav')
