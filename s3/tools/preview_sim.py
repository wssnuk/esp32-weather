import sys, os, math
T='/home/user/repo-esp32-weather/s3/tools'; sys.path.insert(0,T)
import gen_assets as ga
from PIL import Image, ImageDraw, ImageFont
O=T+'/out'
FB=ga.FBD
def bg(n): return Image.open(f'{O}/{n}.png').convert('RGBA')
def png(im,name,x,y): p=Image.open(f'{O}/{name}.png').convert('RGBA'); im.alpha_composite(p,(int(x),int(y)))
def glyphs(im,prefix,s,x,top,pad=8):
    f=ImageFont.truetype(FB,{'gold':56,'sec':26}[prefix])
    for ch in s:
        png(im,f'{prefix}_{ord(ch)}',x-pad,top-pad); x+=int(ImageFont.truetype(FB,{'gold':56,'sec':26}[prefix]*4).getlength(ch))//4
    return x
ANCH={'lm':'lm','rm':'rm','mm':'mm','lt':'lt','mt':'mt','mb':'mb','la':'la'}
def txt(im,s,x,y,px,col,a='lt'):
    d=ImageDraw.Draw(im); d.text((x,y),s,font=ImageFont.truetype(FB,px),fill=col,anchor=a)
def tw(s,px): return ImageFontTruetype(px).getlength(s)
def W(s,px): return ImageFont.truetype(FB,px).getlength(s)
def thai(im,t,x,y,col,align=0,px=10,b=True):
    m=ga.thai_alpha(t,px,b)
    if align==1: x-=m.width//2
    elif align==2: x-=m.width
    y-=m.height//2
    L=Image.new('RGBA',m.size,col); L.putalpha(m); im.alpha_composite(L,(int(x),int(y)))
    return m.width
WH=(255,255,255,255); GOLD=(232,196,120,255); MUT=(170,180,200,255); TEAL=(90,230,210,255); RAIN=(120,190,255,255)
# HOME
im=bg('bg_home'); d=ImageDraw.Draw(im)
txt(im,'Bangkok',14,12,11,GOLD,'lm')
for i in range(3): d.rounded_rectangle([290+i*5,16-3-i*3,293+i*5,16+i*0],1,fill=TEAL)
x=glyphs(im,'gold','14:27',18,26); glyphs(im,'sec',':08',x+2,26+59-27-2)
w=int(280*8.4/60)
for r in range(3):
    for k in range(w): d.point((20+k,100+r),fill=tuple(int(TEAL[c]+(GOLD[c]-TEAL[c])*k/max(1,w)) for c in range(3)))
d.ellipse([20+w-3,98,20+w+3,104],fill=WH)
dx=20; dx+=thai(im,'วันเสาร์ที่',dx,116,WH,px=13)+4; txt(im,'8',dx,116,14,WH,'lm'); dx+=W('8',14)+5
dx+=thai(im,'พฤศจิกายน',dx,116,WH,px=13)+5; txt(im,'2569',dx,116,14,WH,'lm'); txt(im,'UTC+7',300,116,9,MUT,'rm')
png(im,'icon_M_partly_day',14,145); txt(im,'31°',74,167,24,WH,'lm')
for i,(v,c) in enumerate([('36°',(255,170,120)),('40%',(120,190,255)),('28',(130,240,160)),('9',(255,140,210))]): txt(im,v,150+i*40,158,14,c,'mm')
d.ellipse([22,212,30,220],fill=RAIN); x=38; x+=thai(im,'ฝนอาจตกช่วง',x,216,(210,230,255),px=10)+5; txt(im,'17:00',x,216,11,WH,'lm'); x+=W('17:00',11)+8
x+=thai(im,'โอกาส',x,216,MUT,px=10,b=False)+4; txt(im,'70%',x,216,11,RAIN,'lm')
home=im
# NOW
im=bg('bg_now'); d=ImageDraw.Draw(im); txt(im,'14:27',308,12,11,WH,'rm')
png(im,'icon_L_partly_day',12,26); x=glyphs(im,'gold','31°',138,24); txt(im,'C',x+2,40,18,WH)
thai(im,'มีเมฆบางส่วน',140,94,(225,230,240),px=12,b=False); fw=thai(im,'รู้สึกเหมือน',140,110,MUT,px=12,b=False); txt(im,'36°',140+fw+6,110,11,(255,170,120),'lm')
for i,(v,f,c) in enumerate([('80%',0.8,(90,200,255)),('3',0.08,(120,255,190)),('28',0.37,(130,240,160)),('9',0.82,(255,160,90))]):
    cx=12+(i%2)*150; cy=122+(i//2)*54
    d.arc([cx+26-19,cy+24-19,cx+26+19,cy+24+19],135,135+270*f,fill=c,width=4)
    txt(im,v,cx+54,cy+24,18,WH)
now=im
# FC
im=bg('bg_fc'); d=ImageDraw.Draw(im); txt(im,'14:27',308,12,11,WH,'rm')
temps=[31,32,33,33,32,30,29,28,28,27,27,26,26]; rains=[10,20,40,70,80,60,30,20,10,10,5,5,5]
n=13; x0,x1,y0,y1=16,304,34,92; lo,hi=min(temps),max(temps)
X=lambda i: x0+i*(x1-x0)/(n-1); Y=lambda v: y0+12+(hi-v)/(hi-lo)*(y1-y0-18)
ov=Image.new('RGBA',im.size,(0,0,0,0)); od=ImageDraw.Draw(ov)
for px in range(x0,x1+1):
    fi=(px-x0)*(n-1)/(x1-x0); i=min(int(fi),n-2); yy=Y(temps[i]+(temps[i+1]-temps[i])*(fi-i))
    for py in range(int(yy),y1,3): a=int(120-(py-yy)*110/max(1,y1-yy)); od.rectangle([px,py,px,py+2],fill=(232,196,120,a))
im.alpha_composite(ov); d=ImageDraw.Draw(im)
d.line([(X(i),Y(t)) for i,t in enumerate(temps)],fill=(240,205,130),width=3)
for i,p in enumerate(rains):
    bx=X(i); h=p*22//100
    if h: d.rounded_rectangle([bx-3,y1+28-h,bx+3,y1+28],1,fill=(90,180,255) if p>=60 else (50,90,130))
    if i%3==0: txt(im,f'{temps[i]}°',bx,Y(temps[i])-3,9,WH,'mb'); txt(im,f'{(14+i)%24:02d}:00',bx,y1+33,9,MUT,'mt')
rows=[('วันนี้','partly_day',26,35,40),('พรุ่งนี้','rain',25,33,70),('จันทร์','rain',25,32,80),('อังคาร','partly_day',26,34,30)]
for i,(nm,ic,l,h,r) in enumerate(rows):
    y=140+i*24; thai(im,nm,18,y+9,GOLD if i==0 else WH,px=11)
    png(im,f'icon_S_{ic}',89,y-3); txt(im,f'{r}%',148,y+9,9,RAIN,'lm')
    bx0=198+int((l-25)/(35-25)*70); bx1=198+int((h-25)/10*70)
    for rr in range(4): d.line([(bx0,y+7+rr),(bx1,y+7+rr)],fill=(160,190,200))
    txt(im,f'{l}°',192,y+9,9,MUT,'rm'); txt(im,f'{h}°',304,y+9,11,WH,'rm')
fc=im
# PORTAL
im=bg('bg_plain')
thai(im,'ตั้งค่า Wi-Fi',160,26,GOLD,1,px=20); thai(im,'ทางที่ 1: ที่หน้าเว็บติดตั้ง กด Connect to Wi-Fi',16,70,WH,px=12,b=False)
thai(im,'ทางที่ 2: ใช้มือถือเชื่อมต่อ Wi-Fi ชื่อ',16,104,WH,px=12,b=False); txt(im,'ESP32-Weather-S3',160,134,18,GOLD,'mm')
w_=thai(im,'แล้วเปิด',16,170,WH,px=12,b=False); txt(im,'http://192.168.4.1',16+w_+8,170,14,TEAL,'lm')
portal=im
sheet=Image.new('RGB',(640*2+20,480*2+20),'white')
for i,p in enumerate([home,now,fc,portal]): sheet.paste(p.convert('RGB').resize((640,480),Image.LANCZOS),((i%2)*660,(i//2)*500))
sheet.save(sys.argv[1])
