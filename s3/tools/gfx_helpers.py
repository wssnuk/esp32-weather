import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
K=4  # supersample
W,H=320*K,240*K
import os
FR=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tools','fonts','NotoThai400.ttf')
FBD=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tools','fonts','NotoThai700.ttf')
def F(px,b=True): return ImageFont.truetype(FBD if b else FR, int(px*K))
def grad(w,h,c1,c2,horiz=False):
    g=Image.new('RGB',(w,h)); d=ImageDraw.Draw(g)
    n=w if horiz else h
    for i in range(n):
        t=i/max(1,n-1); c=tuple(int(c1[k]+(c2[k]-c1[k])*t) for k in range(3))
        d.line([(i,0),(i,h)] if horiz else [(0,i),(w,i)],fill=c)
    return g
def bg(c1=(16,22,52),c2=(52,24,84)):
    im=grad(W,H,c1,c2).convert('RGBA')
    # soft color blobs
    for (x,y,r,c) in [(0.85,0.1,0.45,(80,120,255,90)),(0.1,0.95,0.5,(255,90,160,60))]:
        b=Image.new('RGBA',(W,H),(0,0,0,0)); ImageDraw.Draw(b).ellipse([W*x-W*r,H*y-W*r,W*x+W*r,H*y+W*r],fill=c)
        im=Image.alpha_composite(im,b.filter(ImageFilter.GaussianBlur(60*K)))
    return im
def glass(im,box,r=14,fill=(255,255,255,26),line=(255,255,255,60)):
    l=Image.new('RGBA',im.size,(0,0,0,0)); d=ImageDraw.Draw(l)
    x0,y0,x1,y1=[v*K for v in box]; d.rounded_rectangle([x0,y0,x1,y1],r*K,fill=fill,outline=line,width=K)
    return Image.alpha_composite(im,l)
def text(im,xy,s,px,col,anchor='la',b=True,glow=None,gradcols=None):
    x,y=xy; f=F(px,b)
    if gradcols:
        m=Image.new('L',im.size,0); ImageDraw.Draw(m).text((x*K,y*K),s,font=f,fill=255,anchor=anchor)
        bb=m.getbbox(); g=grad(bb[2]-bb[0],bb[3]-bb[1],gradcols[0],gradcols[1]).convert('RGBA')
        L=Image.new('RGBA',im.size,(0,0,0,0)); L.paste(g,(bb[0],bb[1])); L.putalpha(m)
        if glow:
            gl=Image.new('RGBA',im.size,glow+(0,)); gl.putalpha(m.filter(ImageFilter.GaussianBlur(6*K)).point(lambda a:int(a*0.8)))
            im=Image.alpha_composite(im,gl)
        return Image.alpha_composite(im,L)
    L=Image.new('RGBA',im.size,(0,0,0,0)); ImageDraw.Draw(L).text((x*K,y*K),s,font=f,fill=col,anchor=anchor)
    if glow:
        gl=L.filter(ImageFilter.GaussianBlur(5*K)); im=Image.alpha_composite(im,gl)
    return Image.alpha_composite(im,L)
def layer(im): return Image.new('RGBA',im.size,(0,0,0,0))
def sunicon(im,cx,cy,r):
    l=layer(im); d=ImageDraw.Draw(l)
    d.ellipse([(cx-r*1.9)*K,(cy-r*1.9)*K,(cx+r*1.9)*K,(cy+r*1.9)*K],fill=(255,190,60,90)); l=l.filter(ImageFilter.GaussianBlur(8*K))
    im=Image.alpha_composite(im,l)
    g=grad(int(2*r*K),int(2*r*K),(255,226,90),(255,140,40)).convert('RGBA'); m=Image.new('L',g.size,0); ImageDraw.Draw(m).ellipse([0,0,g.size[0]-1,g.size[1]-1],fill=255)
    L=layer(im); L.paste(g,(int((cx-r)*K),int((cy-r)*K)),m); return Image.alpha_composite(im,L)
def cloudicon(im,cx,cy,s,c1=(255,255,255),c2=(170,190,230),dark=False):
    if dark: c1,c2=(150,160,190),(90,100,140)
    m=Image.new('L',im.size,0); d=ImageDraw.Draw(m)
    for (x,y,r) in [(-18,4,14),(2,-8,20),(22,4,14)]: d.ellipse([(cx+x*s-r*s)*K,(cy+y*s-r*s)*K,(cx+x*s+r*s)*K,(cy+y*s+r*s)*K],fill=255)
    d.rounded_rectangle([(cx-32*s)*K,(cy+2*s)*K,(cx+36*s)*K,(cy+20*s)*K],int(9*s*K),fill=255)
    bb=m.getbbox(); sh=Image.new('RGBA',im.size,(0,0,30,0)); sh.putalpha(m.filter(ImageFilter.GaussianBlur(4*K)).point(lambda a:a//3))
    im=Image.alpha_composite(im,ImageChops.offset(sh,0,3*K))
    g=grad(bb[2]-bb[0],bb[3]-bb[1],c1,c2).convert('RGBA'); L=layer(im); L.paste(g,(bb[0],bb[1])); L.putalpha(m)
    return Image.alpha_composite(im,L)
def rain(im,cx,cy,s,n=3,col=(90,200,255,255)):
    l=layer(im); d=ImageDraw.Draw(l)
    for i in range(n):
        x=cx-14*s+i*14*s; d.line([((x+3*s)*K,(cy)*K),((x-2*s)*K,(cy+10*s)*K)],fill=col,width=int(2.5*s*K))
    return Image.alpha_composite(im,l)
def bolt(im,cx,cy,s):
    l=layer(im); d=ImageDraw.Draw(l)
    pts=[(cx+2*s,cy),(cx-8*s,cy+16*s),(cx,cy+16*s),(cx-5*s,cy+30*s),(cx+10*s,cy+11*s),(cx+2*s,cy+11*s),(cx+7*s,cy)]
    d.polygon([(x*K,y*K) for x,y in pts],fill=(255,220,60,255)); g=l.filter(ImageFilter.GaussianBlur(4*K))
    return Image.alpha_composite(Image.alpha_composite(im,g),l)
def arc(im,box,a0,a1,w,c1,c2=None,bgc=(255,255,255,30)):
    x0,y0,x1,y1=[v*K for v in box]
    l=layer(im); d=ImageDraw.Draw(l); d.arc([x0,y0,x1,y1],0,360,fill=bgc,width=int(w*K)); im=Image.alpha_composite(im,l)
    m=Image.new('L',im.size,0); ImageDraw.Draw(m).arc([x0,y0,x1,y1],a0,a1,fill=255,width=int(w*K))
    g=grad(int(x1-x0),int(y1-y0),c1,c2 or c1,horiz=True).convert('RGBA'); L=layer(im); L.paste(g,(int(x0),int(y0))); L.putalpha(m)
    gl=L.filter(ImageFilter.GaussianBlur(4*K)); im=Image.alpha_composite(im,gl)
    return Image.alpha_composite(im,L)
def pill(im,box,c,txt,tc=(255,255,255),px=11):
    l=layer(im); d=ImageDraw.Draw(l); x0,y0,x1,y1=box; d.rounded_rectangle([x0*K,y0*K,x1*K,y1*K],(y1-y0)/2*K,fill=c)
    im=Image.alpha_composite(im,l); return text(im,((x0+x1)/2,(y0+y1)/2),txt,px,tc,'mm')
def statusbar(im,title,t='14:27'):
    im=text(im,(12,13),title,11,(200,210,255,255),'lm')
    im=text(im,(308,13),t,11,(255,255,255,255),'rm')
    l=layer(im); d=ImageDraw.Draw(l)
    for i in range(3): d.rounded_rectangle([(250+i*5)*K,(16-3-i*2)*K,(253+i*5)*K,16*K],K,fill=(255,255,255,200))  # wifi bars
    return Image.alpha_composite(im,l)
def finish(im): return im.resize((320,240),Image.LANCZOS).convert('RGB')
