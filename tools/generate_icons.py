"""承認済みA案を各プラットフォームのアイコン形式へ書き出す。Pillowを使用。"""
from pathlib import Path
from PIL import Image, ImageDraw

root=Path(__file__).resolve().parents[1]
assets=root/'assets/icon'
source=Image.open(assets/'source-a.png').convert('RGBA')
master=source.resize((1024,1024),Image.Resampling.LANCZOS)
master.save(assets/'nesdq.png')
master.save(assets/'nesdq.ico',sizes=[(n,n) for n in (16,24,32,48,64,128,256)])
foreground=Image.open(assets/'foreground-source.png').convert('RGBA')
bbox=foreground.getchannel('A').point(lambda a:255 if a>16 else 0).getbbox()
foreground=foreground.crop(bbox)
res=root/'android/app/src/main/res'
for density,scale in [('mdpi',1),('hdpi',1.5),('xhdpi',2),('xxhdpi',3),('xxxhdpi',4)]:
    folder=res/f'mipmap-{density}';folder.mkdir(parents=True,exist_ok=True)
    master.resize((round(48*scale),)*2,Image.Resampling.LANCZOS).save(folder/'ic_launcher.png')
    size=round(108*scale)
    layer=Image.new('RGBA',(size,size))
    item=foreground.copy();item.thumbnail((round(50*scale),round(50*scale)),Image.Resampling.LANCZOS)
    layer.alpha_composite(item,((size-item.width)//2,(size-item.height)//2))
    layer.save(folder/'ic_launcher_foreground.png')
preview=Image.new('RGB',(800,340),'#edf0f5')
preview.paste(master.resize((256,256),Image.Resampling.LANCZOS),(32,24))
fg=Image.open(res/'mipmap-xxxhdpi/ic_launcher_foreground.png')
adaptive=Image.new('RGBA',fg.size,'#071c3d');adaptive.alpha_composite(fg)
adaptive=adaptive.crop((72,72,360,360)).resize((256,256),Image.Resampling.LANCZOS)
mask=Image.new('L',(256,256));ImageDraw.Draw(mask).ellipse((0,0,255,255),fill=255)
preview.paste(adaptive,(320,24),mask)
preview.paste(master.resize((64,64),Image.Resampling.LANCZOS),(624,60))
preview.paste(master.resize((32,32),Image.Resampling.LANCZOS),(640,160))
d=ImageDraw.Draw(preview);d.text((80,300),'Windows / legacy',fill='#10213a');d.text((370,300),'Android adaptive',fill='#10213a');d.text((610,250),'64 / 32 pixels',fill='#10213a')
preview.save(assets/'preview.png')
print('Exported ICO, PNG, Android icons and preview')
