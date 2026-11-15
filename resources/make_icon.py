from PIL import Image, ImageDraw, ImageFilter
import math, os

S = 1024
img = Image.new('RGBA', (S, S), (0,0,0,0))
d = ImageDraw.Draw(img)

# background rounded square with vertical gradient
def lerp(a,b,t): return tuple(int(a[i]+(b[i]-a[i])*t) for i in range(3))
top=(29,91,255); bottom=(17,198,190)
grad=Image.new('RGBA',(S,S))
gd=ImageDraw.Draw(grad)
for y in range(S):
    t=y/(S-1)
    c=lerp(top,bottom,t)
    gd.line([(0,y),(S,y)],fill=c+(255,))
mask=Image.new('L',(S,S),0)
md=ImageDraw.Draw(mask)
md.rounded_rectangle([28,28,S-28,S-28], radius=225, fill=255)
img.paste(grad,(0,0),mask)

# aurora glow blobs
glow=Image.new('RGBA',(S,S),(0,0,0,0))
gg=ImageDraw.Draw(glow)
gg.ellipse([-120, 380, 620, 1030], fill=(94,255,230,120))
gg.ellipse([480, -160, 1180, 520], fill=(255,226,130,100))
gg.ellipse([560, 470, 1200, 1100], fill=(132,107,255,95))
glow=glow.filter(ImageFilter.GaussianBlur(115))
img=Image.alpha_composite(img, Image.composite(glow, Image.new('RGBA',(S,S),(0,0,0,0)), mask))

# inner top highlight
hi=Image.new('RGBA',(S,S),(0,0,0,0))
hd=ImageDraw.Draw(hi)
hd.rounded_rectangle([54,54,S-54,S-54], radius=205, outline=(255,255,255,58), width=6)
hd.arc([70,44,S-70,560], 190, 350, fill=(255,255,255,105), width=9)
img=Image.alpha_composite(img,hi)

# sun
sun=Image.new('RGBA',(S,S),(0,0,0,0))
sd=ImageDraw.Draw(sun)
cx,cy,r=690,330,130
for i in range(40,0,-1):
    a=int(6+ (40-i)*0.5)
    sd.ellipse([cx-r-i*2,cy-r-i*2,cx+r+i*2,cy+r+i*2], fill=(255,224,130,a))
sd.ellipse([cx-r,cy-r,cx+r,cy+r], fill=(255,206,84,255))
sd.ellipse([cx-r*0.55,cy-r*0.65,cx+r*0.15,cy-r*0.05], fill=(255,244,186,150))
sun=sun.filter(ImageFilter.GaussianBlur(2))
img=Image.alpha_composite(img,sun)

# cloud shadow
shadow=Image.new('RGBA',(S,S),(0,0,0,0))
shd=ImageDraw.Draw(shadow)
shd.ellipse([180,540,870,800],fill=(0,40,80,95))
shadow=shadow.filter(ImageFilter.GaussianBlur(46))
img=Image.alpha_composite(img, Image.composite(shadow, Image.new('RGBA',(S,S),(0,0,0,0)), mask))

# cloud shape mask
cm=Image.new('L',(S,S),0)
cd=ImageDraw.Draw(cm)
cd.ellipse([150,500,500,800], fill=255)
cd.ellipse([330,400,700,760], fill=255)
cd.ellipse([560,490,880,790], fill=255)
cd.rounded_rectangle([200,600,850,810], radius=105, fill=255)
# gradient cloud
cg=Image.new('RGBA',(S,S))
cgd=ImageDraw.Draw(cg)
for y in range(380,830):
    t=(y-380)/450
    c=lerp((255,255,255),(205,238,255),t)
    cgd.line([(0,y),(S,y)],fill=c+(255,))
img.paste(cg,(0,0),cm)

# cloud inner highlight
ch=Image.new('RGBA',(S,S),(0,0,0,0))
chd=ImageDraw.Draw(ch)
chd.arc([190,520,500,760],200,330,fill=(255,255,255,190),width=14)
chd.arc([355,430,680,710],205,315,fill=(255,255,255,150),width=12)
img=Image.alpha_composite(img, Image.composite(ch, Image.new('RGBA',(S,S),(0,0,0,0)), cm))

# small rain drops
rd=ImageDraw.Draw(img)
for x,y,l in [(320,790,60),(430,805,76),(540,790,58)]:
    rd.rounded_rectangle([x,y,x+18,y+l],radius=9,fill=(191,244,255,235))
    rd.ellipse([x-3,y-8,x+21,y+16],fill=(191,244,255,235))

# subtle bottom shading on icon
shade=Image.new('RGBA',(S,S),(0,0,0,0))
sh=ImageDraw.Draw(shade)
sh.ellipse([-200,720,1200,1250],fill=(0,30,80,70))
shade=shade.filter(ImageFilter.GaussianBlur(80))
img=Image.alpha_composite(img, Image.composite(shade, Image.new('RGBA',(S,S),(0,0,0,0)), mask))

# outer border
bd=ImageDraw.Draw(img)
bd.rounded_rectangle([28,28,S-28,S-28], radius=225, outline=(255,255,255,80), width=5)

img.save('resources/icon_1024.png')
sizes=[(256,256),(128,128),(64,64),(48,48),(32,32),(24,24),(16,16)]
img.save('resources/icon.ico', sizes=sizes)
print('saved', os.path.getsize('resources/icon.ico'))
