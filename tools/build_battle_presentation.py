from pathlib import Path
from PIL import Image,ImageFont,ImageDraw,ImageFilter
import numpy as np,json,hashlib
root=Path.cwd();out=root/'native_hd/assets/presentation';out.mkdir(exist_ok=True)
font=ImageFont.truetype(str(root/'remaster/assets/fonts/NotoSansSC.subset.ttf'),132)
for obsolete in ['command-22934.png','command-36867.png']:
 (out/obsolete).unlink(missing_ok=True)
for ch in '战技物收商破':
 mask=Image.new('L',(192,192));d=ImageDraw.Draw(mask);box=d.textbbox((0,0),ch,font=font);x=(192-(box[2]-box[0]))/2-box[0];y=(192-(box[3]-box[1]))/2-box[1];d.text((x,y),ch,font=font,fill=255)
 a=np.asarray(mask);dilate=np.asarray(mask.filter(ImageFilter.MaxFilter(11)));stroke=np.clip(dilate.astype(int)-a,0,255).astype('uint8')
 im=Image.new('RGBA',(192,192));shadow=Image.new('RGBA',im.size,(21,9,38,0));shadow.putalpha(mask.filter(ImageFilter.GaussianBlur(6)));im.alpha_composite(shadow,(3,5))
 ring=Image.new('RGBA',im.size,(25,17,36,255));ring.putalpha(Image.fromarray(stroke));im.alpha_composite(ring)
 yy=np.arange(192);rgb=np.zeros((192,192,4),dtype='uint8');stops=[(0,(255,249,210)),(.4,(255,238,165)),(.51,(183,97,29)),(.67,(255,213,91)),(1,(238,145,39))]
 for y in range(192):
  f=y/192
  for (p,c),(q,e) in zip(stops,stops[1:]):
   if p<=f<=q:rgb[y,:,:3]=np.rint(np.array(c)+(np.array(e)-c)*(f-p)/(q-p));break
 rgb[:,:,3]=a;im.alpha_composite(Image.fromarray(rgb));im.save(out/f'command-{ord(ch)}.png')
# A native geometric UI background: quiet blue arena with fine gold/cyan rings.
w,h=1440,1920;y,x=np.mgrid[:h,:w];r=np.sqrt(((x-w/2)/(w*.64))**2+((y-h*.45)/(h*.58))**2)
grad=np.clip(1-r,0,1);a=np.zeros((h,w,4),dtype='uint8');a[:,:,0]=14+grad*12;a[:,:,1]=23+grad*17;a[:,:,2]=43+grad*26;a[:,:,3]=255
arena=Image.fromarray(a);layer=Image.new('RGBA',arena.size);d=ImageDraw.Draw(layer)
for radius,alpha in [(300,15),(430,25),(550,16)]:d.ellipse((w/2-radius,h*.46-radius,w/2+radius,h*.46+radius),outline=(120,172,196,alpha),width=3)
for cx in [w*.26,w*.74]:
 for cy in [h*.27,h*.5,h*.73]:
  d.ellipse((cx-155,cy-26,cx+155,cy+26),outline=(164,144,91,25),width=2)
d.line((38,82,w-38,82),fill=(164,144,91,55),width=2);d.line((38,h-280,w-38,h-280),fill=(164,144,91,55),width=2)
arena.alpha_composite(layer);arena.save(out/'arena.png')
items=[{'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.glob('*.png'))]
(out/'manifest.json').write_text(json.dumps({'version':'0.7','origin':'Code-native geometric UI and Noto Sans SC vector glyph rendering (SIL OFL).','unit_scale':1.35,'effect_scale':1.55,'command_size_logical_px':26,'files':items},ensure_ascii=False,indent=2),encoding='utf8')
print('Built',len(items),'native UI assets')
