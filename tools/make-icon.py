"""Draws the WinDrop mark of docs/assets/windrop.svg into app/winawdl.ico and
docs/assets/windrop.png. Needs Pillow. The shapes are drawn four times as
large and scaled down, which is all the smoothing they get."""
import os

from PIL import Image, ImageDraw

SCALE = 4
SIZE = 256 * SCALE
TOP, BOTTOM, WHITE = (0x5A, 0xC1, 0xFF), (0x0B, 0x6B, 0xFF), (255, 255, 255)


def at(*numbers):
    return [number * SCALE for number in numbers]


def bezier(start, first, second, end, steps=60):
    points = []
    for step in range(steps + 1):
        t = step / steps
        points.append(tuple(
            (1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t ** 2 * c + t ** 3 * d
            for a, b, c, d in zip(start, first, second, end)))
    return points


def mark():
    gradient = Image.new('RGB', (1, SIZE))
    for y in range(SIZE):
        gradient.putpixel((0, y), tuple(
            round(top + (bottom - top) * y / (SIZE - 1)) for top, bottom in zip(TOP, BOTTOM)))
    gradient = gradient.resize((SIZE, SIZE))
    plate = Image.new('L', (SIZE, SIZE), 0)
    ImageDraw.Draw(plate).rounded_rectangle(at(8, 8, 248, 248), radius=56 * SCALE, fill=255)
    image = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    image.paste(gradient, (0, 0), plate)

    draw = ImageDraw.Draw(image)
    right = bezier(at(128, 40), at(150, 78), at(190, 104), at(190, 148))
    left = bezier(at(66, 148), at(66, 104), at(106, 78), at(128, 40))
    draw.polygon(right + left, fill=WHITE)
    draw.pieslice(at(66, 86, 190, 210), 0, 180, fill=WHITE)

    width = 11 * SCALE
    for (cx, cy, radius) in ((128, 150, 34), (128, 152, 17)):
        draw.arc(at(cx - radius, cy - radius, cx + radius, cy + radius), 180, 360, fill=BOTTOM, width=width)
        for x in (cx - radius, cx + radius):  # round ends
            end_x = x + (width / SCALE / 2 if x < cx else -width / SCALE / 2)
            draw.ellipse(at(end_x - 5.5, cy - 5.5, end_x + 5.5, cy + 5.5), fill=BOTTOM)
    draw.ellipse(at(119, 163, 137, 181), fill=BOTTOM)
    return image.resize((256, 256), Image.LANCZOS)


if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    image = mark()
    image.save(os.path.join(root, 'docs', 'assets', 'windrop.png'))
    image.save(os.path.join(root, 'app', 'winawdl.ico'),
               sizes=[(size, size) for size in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
    print('icon written')
