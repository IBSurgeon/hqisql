#!/usr/bin/env python3
"""Builds the demo GIFs of hqisql. See README.md in this directory.

  demo.py data                     make the data files in work/data
  demo.py record FBDIR [SCENE...]  record the scenes (Linux: VHS, Windows: pywinpty)
  demo.py gif OS [SCENE...]        add the balloons, make docs/img/OS/LANG/*.gif
"""

import datetime
import decimal
import glob
import json
import os
import random
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
WORK = HERE / 'work'
IMG = REPO / 'docs' / 'img'
LANGS = ('en', 'pt-BR')
SPEED = 0.04        # Set TypingSpeed in HEADER
FPS = 12
BAND = 56           # pixels above the terminal for the balloons
BALLOON_MAX = 4.0   # seconds
MAX_SECONDS = 30
MAX_BYTES = 3 * 1024 * 1024

FONTS = (
    '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    'C:/Windows/Fonts/segoeuib.ttf',
)
MONO_FONTS = (
    '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',
    'C:/Windows/Fonts/consola.ttf',
)

# Typed into a hidden shell before each scene. The scene starts at SQL>.
HEADER = '''Output {out}
Set Shell bash
Set FontFamily "JetBrains Mono"
Set FontSize 18
Set Width 1000
Set Height 560
Set Padding 16
Set TypingSpeed 40ms
Set CursorBlink false
Set Framerate 20
Set Theme "Builtin Dark"

Hide
Type@1ms "export FIREBIRD=$PWD/fb HOME=$PWD/home PS1='$ '; clear" Enter
Type@1ms "fb/bin/hqisql -user SYSDBA db/employee.fdb" Enter
Sleep 2s
Ctrl+L
Show
'''


def run(cmd, **kw):
    print('+', ' '.join(str(c) for c in cmd[:12]), file=sys.stderr)
    subprocess.run(cmd, check=True, **kw)


def scenes(names):
    all_ = sorted(p.stem for p in (HERE / 'tapes').glob('*.tape'))
    for n in names:
        if n not in all_:
            sys.exit(f'unknown scene {n}; scenes: {" ".join(all_)}')
    return names or all_


def seconds(s):
    m = re.fullmatch(r'([\d.]+)(ms|s)?', s)
    return float(m.group(1)) / (1000 if m.group(2) == 'ms' else 1)


def simulate(lines):
    """Time of each '# @balloon key' mark and the total time of the
    visible part, from the Type speed and the Sleep commands."""
    t, hidden, marks = 0.0, False, []
    for line in lines:
        s = line.strip()
        m = re.match(r'#\s*@balloon\s+(\S+)', s)
        if m:
            marks.append([t, m.group(1)])
            continue
        if not s or s.startswith('#'):
            continue
        word = s.split()[0]
        if word in ('Hide', 'Show'):
            hidden = word == 'Hide'
            continue
        if hidden or word in ('Set', 'Output', 'Screenshot', 'Env'):
            continue
        if word == 'Sleep':
            t += seconds(s.split()[1])
            continue
        speed = SPEED
        m = re.match(r'\w+(?:\+\w+)*@([\d.]+m?s)', word)
        if m:
            speed = seconds(m.group(1))
        if word.startswith('Type'):
            text = s[s.index('"') + 1:s.rindex('"')]
            t += len(text) * speed + len(s[s.rindex('"') + 1:].split()) * SPEED
            continue
        parts = s.split()
        t += (int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 1) * speed
    return t, marks


def data():
    """Data files of scenes 05 and 06 (the same on each run)."""
    import pyarrow as pa
    import pyarrow.parquet as pq
    d = WORK / 'data'
    d.mkdir(parents=True, exist_ok=True)
    rnd = random.Random(1)
    names = ['Acme', 'Globex', 'Initech', 'Umbrella', 'Hooli', 'Stark',
             'Wayne', 'Tyrell', 'Cyberdyne', 'Soylent']
    day0 = datetime.date(2025, 1, 1)
    with open(d / 'orders.csv', 'w', newline='\n') as f:
        f.write('id,customer,amount,order_date\n')
        for i in range(1, 100001):
            f.write(f'{i},{rnd.choice(names)},{rnd.randrange(500, 500000) / 100:.2f},'
                    f'{day0 + datetime.timedelta(rnd.randrange(365))}\n')
    with open(d / 'bad.csv', 'w', newline='\n') as f:
        f.write('id,customer,amount\n')
        for i in range(100001, 101001):
            amount = 'n/a' if i % 1000 in (137, 512, 870) else f'{rnd.randrange(500, 500000) / 100:.2f}'
            f.write(f'{i},{rnd.choice(names).lower()},{amount}\n')
    n = 50000
    pq.write_table(pa.table({
        'id': pa.array(range(200001, 200001 + n), pa.int32()),
        'customer': pa.array([rnd.choice(names) for _ in range(n)]),
        'amount': pa.array([decimal.Decimal(rnd.randrange(500, 500000)) / 100 for _ in range(n)],
                           pa.decimal128(12, 2)),
        'order_date': pa.array([day0 + datetime.timedelta(rnd.randrange(365)) for _ in range(n)],
                               pa.date32()),
    }), d / 'sales.parquet')


def record(fbdir, names):
    """Each scene in a fresh database and a fresh HOME (empty history)."""
    fbdir = Path(fbdir).resolve()
    win = os.name == 'nt'
    hq = fbdir / 'hqisql.exe' if win else fbdir / 'bin' / 'hqisql'
    if not (WORK / 'data' / 'sales.parquet').exists():
        data()
    out = WORK / 'raw' / ('windows' if win else 'linux')
    out.mkdir(parents=True, exist_ok=True)
    if not win:
        fb = WORK / 'fb'
        if fb.is_symlink() or fb.exists():
            fb.unlink()
        fb.symlink_to(fbdir)
    shutil.copy(HERE / 'colors-demo.txt', WORK / 'colors-demo.txt')
    # LOCALAPPDATA: the history of hqisql on Windows.
    env = dict(os.environ, FIREBIRD=str(fbdir), VHS_NO_SANDBOX='true',
               LOCALAPPDATA=str(WORK / 'home'))
    env.pop('NO_COLOR', None)
    for name in scenes(names):
        for d in ('db', 'home'):
            shutil.rmtree(WORK / d, ignore_errors=True)
            (WORK / d).mkdir()
        shutil.copy(fbdir / 'examples' / 'empbuild' / 'employee.fdb', WORK / 'db')
        run([hq, '-q', '-user', 'SYSDBA', 'db/employee.fdb', '-i', HERE / 'setup.sql'],
            cwd=WORK, env=env)
        body = (HERE / 'tapes' / f'{name}.tape').read_text().splitlines()
        if win:
            total, marks = Player(hq, env).record(body, out / f'{name}.mp4')
        else:
            tape = WORK / f'{name}.tape'
            tape.write_text(HEADER.format(out=f'raw/linux/{name}.mp4') + '\n'.join(body) + '\n')
            run(['vhs', tape.name], cwd=WORK, env=env)
            total, marks = simulate(body)
        (out / f'{name}.json').write_text(json.dumps({'total': total, 'marks': marks}))


# Windows: VHS does not run Windows console programs. hqisql.exe runs in a
# pseudo console (pywinpty), the keys of the tape go to it in real time,
# and the terminal emulator pyte draws the frames in the look of Windows
# Terminal: Cascadia Mono and the Campbell colors.
COLS, ROWS = 89, 24
CELL_W, CELL_H, PAD, TITLE = 11, 22, 16, 40
WIN_FPS = 20
BG, FG = (12, 12, 12), (204, 204, 204)
CAMPBELL = {
    'black': BG, 'red': (197, 15, 31), 'green': (19, 161, 14), 'brown': (193, 156, 0),
    'blue': (0, 55, 218), 'magenta': (136, 23, 152), 'cyan': (58, 150, 221), 'white': FG,
    'brightblack': (118, 118, 118), 'brightred': (231, 72, 86), 'brightgreen': (22, 198, 12),
    'brightbrown': (249, 241, 165), 'brightblue': (59, 120, 255),
    'brightmagenta': (180, 0, 158), 'brightcyan': (97, 214, 214), 'brightwhite': (242, 242, 242),
}
KEYS = {'Enter': '\r', 'Tab': '\t', 'Backspace': '\x7f', 'Up': '\x1b[A', 'Down': '\x1b[B',
        'Right': '\x1b[C', 'Left': '\x1b[D'}
WIN_FONTS = 'C:/Windows/Fonts/'


class Player:
    """Plays a tape: keeps the output of hqisql.exe with the time it came,
    the time of each mark and the hidden parts, then draws the frames."""

    def __init__(self, hq, env):
        self.hq, self.env = hq, env
        self.out, self.marks, self.hidden = [], [], []

    def start(self, extra=None):
        from winpty import PtyProcess
        self.p = PtyProcess.spawn([str(self.hq), '-user', 'SYSDBA', r'db\employee.fdb'],
                                  cwd=str(WORK), env=dict(self.env, **(extra or {})),
                                  dimensions=(ROWS, COLS))
        threading.Thread(target=self.read, args=(self.p,), daemon=True).start()

    def read(self, p):
        while True:
            try:
                data = p.read(65536)
            except EOFError:
                return
            self.out.append((time.perf_counter(), data))

    def send(self, keys, pause):
        self.p.write(keys)
        time.sleep(pause)

    def stop(self):
        for _ in range(30):
            if not self.p.isalive():
                return
            time.sleep(0.1)
        self.p.terminate(force=True)

    def play(self, lines):
        # As the HEADER of the Linux tapes: the scene starts at SQL>.
        self.start()
        time.sleep(2)
        self.send('\x0c', 0.3)
        self.t0 = time.perf_counter()
        for line in lines:
            s = line.strip()
            m = re.match(r'#\s*@balloon\s+(\S+)', s)
            if m:
                self.marks.append((time.perf_counter(), m.group(1)))
                continue
            if not s or s.startswith('#'):
                continue
            word, _, rest = s.partition(' ')
            name, _, speed = word.partition('@')
            speed = seconds(speed) if speed else SPEED
            if name == 'Hide':
                hide = time.perf_counter()
            elif name == 'Show':
                self.hidden.append((hide, time.perf_counter()))
            elif name == 'Sleep':
                time.sleep(seconds(rest))
            elif name == 'Type':
                text = s[s.index('"') + 1:s.rindex('"')]
                m = re.fullmatch(r'((?:\w+=\S+ +)*)fb/bin/hqisql .*', text)
                if m:
                    # A new hqisql (scene 07): VAR=value in front of it.
                    self.stop()
                    self.start(dict(v.split('=', 1) for v in m.group(1).split()))
                else:
                    for c in text.replace("'data/", "'data\\"):
                        self.send(c, speed)
                for key in s[s.rindex('"') + 1:].split():
                    self.send(KEYS[key], SPEED)
            else:
                key = chr(ord(name[-1]) - 64) if name.startswith('Ctrl+') else KEYS[name]
                for _ in range(int(rest) if rest.isdigit() else 1):
                    self.send(key, speed)
        self.end = time.perf_counter()
        self.send('\x04', 0)
        self.stop()

    def video_time(self, t):
        return t - self.t0 - sum(min(b, t) - a for a, b in self.hidden if t > a)

    def real_time(self, v):
        t = self.t0 + v
        for a, b in self.hidden:
            if t >= a:
                t += b - a
        return t

    def record(self, lines, mp4):
        import pyte
        self.play(lines)
        total = self.video_time(self.end)
        marks = [[self.video_time(t), key] for t, key in self.marks]
        screen = pyte.Screen(COLS, ROWS)
        stream = pyte.Stream(screen)
        window = chrome()
        w, h = window.size
        ff = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                               '-s', f'{w}x{h}', '-r', str(WIN_FPS), '-i', '-',
                               '-c:v', 'libx264rgb', '-qp', '0', '-preset', 'veryfast', str(mp4)],
                              stdin=subprocess.PIPE)
        fonts = [font([WIN_FONTS + 'CascadiaMono.ttf'], 18) for _ in range(2)]
        fonts[1].set_variation_by_name('Bold')
        i, n, dirty = 0, 0, True
        while n / WIN_FPS < total:
            t = self.real_time(n / WIN_FPS)
            while i < len(self.out) and self.out[i][0] <= t:
                stream.feed(self.out[i][1])
                i, dirty = i + 1, True
            if dirty:
                frame, dirty = draw(screen, fonts, window), False
            ff.stdin.write(frame.tobytes())
            n += 1
        ff.stdin.close()
        if ff.wait():
            sys.exit('ffmpeg failed')
        return total, marks


def chrome():
    """An empty Windows Terminal window: the tab row with one tab."""
    from PIL import Image, ImageDraw, ImageFont
    w = COLS * CELL_W + 2 * PAD
    im = Image.new('RGB', (w + w % 2, TITLE + ROWS * CELL_H + 2 * PAD), BG)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, im.width, TITLE - 1], fill=(32, 32, 32))
    d.rounded_rectangle([8, 6, 230, TITLE + 8], radius=8, fill=BG)
    mid = 6 + (TITLE - 6) // 2
    icons = ImageFont.truetype(WIN_FONTS + 'SegoeIcons.ttf', 10)
    d.text((20, mid), '>_', font=font([WIN_FONTS + 'CascadiaMono.ttf'], 12), fill=FG, anchor='lm')
    d.text((44, mid), 'hqisql', font=ImageFont.truetype(WIN_FONTS + 'segoeui.ttf', 13),
           fill=(242, 242, 242), anchor='lm')
    d.text((212, mid), '\ue8bb', font=icons, fill=FG, anchor='mm')
    d.text((250, mid), '\ue710', font=icons, fill=FG, anchor='mm')
    d.text((282, mid), '\ue70d', font=icons, fill=FG, anchor='mm')
    for k, glyph in enumerate(('\ue921', '\ue922', '\ue8bb')):
        d.text((im.width - 23 - 46 * (2 - k), TITLE // 2), glyph, font=icons, fill=FG, anchor='mm')
    return im


def color(name, default, bold):
    if name == 'default':
        return default
    if bold and name in CAMPBELL and not name.startswith('bright') and name != 'black':
        name = 'bright' + name
    if name in CAMPBELL:
        return CAMPBELL[name]
    return tuple(int(name[k:k + 2], 16) for k in (0, 2, 4))


def draw(screen, fonts, window):
    from PIL import ImageDraw
    im = window.copy()
    d = ImageDraw.Draw(im)
    ascent, descent = fonts[0].getmetrics()
    base = (CELL_H - ascent - descent) // 2 + ascent
    for y in range(ROWS):
        row, x = screen.buffer[y], 0
        while x < COLS:
            c = row[x]
            style = (c.fg, c.bg, c.bold, c.reverse)
            end = x + 1
            while end < COLS and (row[end].fg, row[end].bg, row[end].bold, row[end].reverse) == style:
                end += 1
            fg, bg = color(c.fg, FG, c.bold), color(c.bg, BG, False)
            if c.reverse:
                fg, bg = bg, fg
            left, top = PAD + x * CELL_W, TITLE + PAD + y * CELL_H
            if bg != BG:
                d.rectangle([left, top, PAD + end * CELL_W - 1, top + CELL_H - 1], fill=bg)
            text = ''.join(row[k].data for k in range(x, end))
            if text.strip():
                d.text((left, top + base), text, font=fonts[c.bold], fill=fg, anchor='ls')
            x = end
    if not screen.cursor.hidden:
        left, top = PAD + screen.cursor.x * CELL_W, TITLE + PAD + screen.cursor.y * CELL_H
        d.rectangle([left, top + 1, left + 1, top + CELL_H - 2], fill=(242, 242, 242))
    return im


def font(fonts, size):
    from PIL import ImageFont
    for f in fonts:
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    sys.exit(f'no font: {fonts}')


def balloon(text, path):
    """A yellow balloon with a tail at the bottom left. Drawn at 3x and
    scaled down for smooth edges."""
    from PIL import Image, ImageDraw
    k = 3
    f = font(FONTS, 21 * k)
    l, t, r, b = ImageDraw.Draw(Image.new('RGBA', (1, 1))).textbbox((0, 0), text, font=f)
    px, py, tail = 14 * k, 8 * k, 9 * k
    w, h = r - l + 2 * px, b - t + 2 * py
    im = Image.new('RGBA', (w, h + tail), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    fill = (255, 213, 74, 255)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=10 * k, fill=fill)
    d.polygon([(18 * k, h - 2), (34 * k, h - 2), (20 * k, h + tail - 1)], fill=fill)
    d.text((px - l, py - t), text, font=f, fill=(26, 26, 26, 255))
    im.resize((w // k, (h + tail) // k), Image.LANCZOS).save(path)
    return (h + tail) // k


def band(width, color, path):
    """The strip above the terminal: the background color of the
    terminal and the name of the program on the left."""
    from PIL import Image, ImageDraw
    im = Image.new('RGB', (width, BAND), color)
    version = (REPO / 'VERSION').read_text().strip()
    ImageDraw.Draw(im).text((16, BAND // 2), f'hqisql {version}', font=font(MONO_FONTS, 15),
                            fill=(105, 105, 105), anchor='lm')
    im.save(path)


def probe(raw):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                          'stream=width:format=duration', '-of', 'json', raw],
                         check=True, capture_output=True, text=True).stdout
    j = json.loads(out)
    return j['streams'][0]['width'], float(j['format']['duration'])


def background(raw, tmp):
    from PIL import Image
    run(['ffmpeg', '-v', 'error', '-y', '-i', raw, '-frames:v', '1', tmp / 'bg.png'])
    return Image.open(tmp / 'bg.png').convert('RGB').getpixel((3, 3))


def captions(lang):
    caps = {}
    for line in (HERE / 'captions' / f'{lang}.txt').read_text(encoding='utf-8').splitlines():
        if line.strip() and not line.startswith('#'):
            key, text = line.split('=', 1)
            caps[key.strip()] = text.strip()
    return caps


def sheet(os_name, name, frames, times):
    """work/sheets/OS/NAME.png: a frame for each balloon and the last
    frame, one under the other, for a quick review."""
    from PIL import Image
    ims = [Image.open(frames[min(int(t * FPS), len(frames) - 1)]) for t in times]
    ims.append(Image.open(frames[-1]))
    w, h = ims[0].size
    out = Image.new('RGB', (w, h * len(ims)))
    for i, im in enumerate(ims):
        out.paste(im, (0, h * i))
    dest = WORK / 'sheets' / os_name / f'{name}.png'
    dest.parent.mkdir(parents=True, exist_ok=True)
    out.resize((w * 3 // 5, h * len(ims) * 3 // 5)).save(dest)


def gif(os_name, names):
    rows = []
    for name in scenes(names):
        raw = WORK / 'raw' / os_name / f'{name}.mp4'
        timing = json.loads((WORK / 'raw' / os_name / f'{name}.json').read_text())
        width, real = probe(raw)
        # The simulated times are a little off: scale them to the video.
        k = real / timing['total']
        marks = [(t * k + 0.1, key) for t, key in timing['marks']]
        for lang in LANGS:
            caps = captions(lang)
            tmp = WORK / 'tmp'
            shutil.rmtree(tmp, ignore_errors=True)
            tmp.mkdir()
            band(width, background(raw, tmp), tmp / 'band.png')
            cmd = ['ffmpeg', '-v', 'error', '-y', '-i', raw, '-loop', '1', '-i', tmp / 'band.png']
            graph = [f'[0:v]fps={FPS},pad=iw:ih+{BAND}:0:{BAND}[v0]',
                     '[v0][1:v]overlay=0:0:shortest=1[v1]']
            for i, (start, key) in enumerate(marks):
                nxt = marks[i + 1][0] if i + 1 < len(marks) else real
                end = min(nxt - 0.25, start + BALLOON_MAX, real - 0.1)
                dur = end - start
                png = tmp / f'b{i}.png'
                y = max(2, BAND - balloon(caps[key], png))
                cmd += ['-loop', '1', '-t', f'{dur:.2f}', '-i', png]
                graph += [f'[{i + 2}:v]format=rgba,fade=t=in:st=0:d=0.25:alpha=1,'
                          f'fade=t=out:st={dur - 0.3:.2f}:d=0.3:alpha=1,'
                          f'setpts=PTS-STARTPTS+{start:.2f}/TB[b{i}]',
                          f'[v{i + 1}][b{i}]overlay=x=W-w-16:y={y}:eof_action=pass:'
                          f"enable='between(t,{start:.2f},{end:.2f})'[v{i + 2}]"]
            cmd += ['-filter_complex', ';'.join(graph), '-map', f'[v{len(marks) + 1}]',
                    tmp / 'f%05d.png']
            run(cmd)
            frames = sorted(glob.glob(str(tmp / 'f*.png')))
            if lang == LANGS[0]:
                sheet(os_name, name, frames, [start + 1.2 for start, _ in marks])
            dest = IMG / os_name / lang / f'{name}.gif'
            dest.parent.mkdir(parents=True, exist_ok=True)
            run(['gifski', '-q', '--fps', str(FPS), '--quality', '90', '-o', dest] + frames)
            run(['gifsicle', '-b', '-O3', '--no-warnings', dest])
            rows.append((f'{os_name}/{lang}/{name}.gif', len(frames) / FPS, dest.stat().st_size))
    bad = False
    for path, sec, size in rows:
        flag = '' if sec <= MAX_SECONDS and size <= MAX_BYTES else '  TOO LONG OR TOO BIG'
        bad = bad or bool(flag)
        print(f'{path:40} {sec:5.1f} s {size / 1024:7.0f} KB{flag}')
    sys.exit(1 if bad else 0)


def main(argv):
    if argv[:1] == ['data']:
        data()
    elif argv[:1] == ['record'] and len(argv) >= 2:
        record(argv[1], argv[2:])
    elif argv[:1] == ['gif'] and len(argv) >= 2:
        gif(argv[1], argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
