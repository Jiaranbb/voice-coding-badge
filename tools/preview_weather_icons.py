from PIL import Image, ImageFont, ImageDraw

EMOJI  = ["☀️","⛅","☁️","\U0001F327️","❄️","⛈️","\U0001F32B️","\U0001F319"]
LABELS = ["0 clear/sun","1 partly","2 cloudy","3 rain","4 snow","5 storm","6 fog","clear/night"]
STRIKE = 96
TARGET = 84
font = ImageFont.truetype("/System/Library/Fonts/Apple Color Emoji.ttc", STRIKE)

def render(em):
    img = Image.new("RGBA", (STRIKE+20, STRIKE+20), (0,0,0,0))
    d = ImageDraw.Draw(img)
    d.text((10,10), em, font=font, embedded_color=True)
    bb = img.getbbox()
    img = img.crop(bb)
    # fit into TARGET square
    s = TARGET / max(img.width, img.height)
    img = img.resize((round(img.width*s), round(img.height*s)), Image.LANCZOS)
    return img

cell_w, cell_h = 130, 150
cols = 4; rows = 2
W, H = cols*cell_w, rows*cell_h
canvas = Image.new("RGB", (W, H), (0,0,0))
draw = ImageDraw.Draw(canvas)
try:
    lab = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 18)
except Exception:
    lab = ImageFont.load_default()

for i,(em,name) in enumerate(zip(EMOJI,LABELS)):
    cx = (i%cols)*cell_w + cell_w//2
    cy = (i//cols)*cell_h + 60
    ic = render(em)
    canvas.paste(ic, (cx-ic.width//2, cy-ic.height//2), ic)
    w = draw.textlength(name, font=lab)
    draw.text((cx-w/2, cy+55), name, fill=(200,200,200), font=lab)

canvas.save("/tmp/weather_icons_preview.png")
print("saved /tmp/weather_icons_preview.png", canvas.size)
