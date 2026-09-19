# 把 preview_2048.cpp 输出的 PPM 帧拼成 GIF 和连拍图（右侧信息栏文字在这儿补上）
#   python3 test/make_preview.py /tmp/cp2048_preview
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cp2048_preview"
rows = [line.split() for line in open(os.path.join(d, "frames.txt"))]
scores = [int(r[1]) for r in rows]
move_ids = [int(r[2]) for r in rows]
merge_counts = [int(r[3]) for r in rows]
imgs = [Image.open(f).convert("RGB") for f in sorted(glob.glob(os.path.join(d, "frame_*.ppm")))]


def font(size):
    for name in ("DejaVuSans-Bold.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


big, small = font(15), font(9)
BAT = 82     # 预览里假装电量
BAT_CHG = 1  # 预览里假装在充电，位置/尺寸和固件 drawBattery() 一致
for i, im in enumerate(imgs):
    dr = ImageDraw.Draw(im)
    cx = 188
    dr.text((cx, 26), "2048", font=big, fill=(237, 194, 46), anchor="mm")
    dr.text((cx, 46), "SCORE", font=small, fill=(153, 153, 153), anchor="mm")
    dr.text((cx, 60), str(scores[i]), font=big, fill=(255, 255, 255), anchor="mm")
    dr.text((cx, 78), "BEST", font=small, fill=(153, 153, 153), anchor="mm")
    dr.text((cx, 90), str(max(scores)), font=big, fill=(255, 255, 255), anchor="mm")
    bx = 236 - (20 + 7 + 54)
    col = (111, 207, 95)
    dr.rectangle([bx, 5, bx + 19, 14], outline=col)
    dr.rectangle([bx + 20, 8, bx + 22, 11], fill=col)
    dr.rectangle([bx + 2, 7, bx + 2 + (20 - 4) * BAT // 100 - 1, 12], fill=col)
    txt = f" {BAT}%" + (" CHG" if BAT_CHG else "")
    dr.text((bx + 27, 10), txt, font=small, fill=col, anchor="lm")
    dr.text((cx, 102), "arrows: ; , . /", font=small, fill=(153, 153, 153), anchor="mm")
    dr.text((cx, 114), "DEL undo  R new", font=small, fill=(153, 153, 153), anchor="mm")
    dr.text((cx, 126), "-/+ light", font=small, fill=(153, 153, 153), anchor="mm")


gif = os.path.join(d, "preview.gif")
imgs[0].save(gif, save_all=True, append_images=imgs[1:], duration=28, loop=0, optimize=False)

# 挑最后“有合并”的一手做连拍图（没有就退回最后一手）
last = max(move_ids)
pick = next((mv for mv in sorted(set(move_ids), reverse=True) if merge_counts[move_ids.index(mv)] > 0), last)
strip = [im for im, mv in zip(imgs, move_ids) if mv == pick]
w, h = strip[0].size
s = 2
sheet = Image.new("RGB", (w * s * len(strip), h * s))
for i, im in enumerate(strip):
    sheet.paste(im.resize((w * s, h * s), Image.NEAREST), (i * w * s, 0))
sheet.save(os.path.join(d, "move_strip.png"))

print(f"{len(imgs)} frames -> {gif}, move_strip.png")
