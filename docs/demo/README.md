# Demo GIFs

The scripts in this directory make the GIFs in `docs/img/<os>/<lang>/`.
Each GIF shows one feature, is shorter than 30 s and has the feature name
in a balloon (English and Portuguese).

## Files

- `tapes/NN-name.tape`: the input of a scene for
  [VHS](https://github.com/charmbracelet/vhs). A line `# @balloon key` shows
  the balloon `key` from this point.
- `captions/en.txt`, `captions/pt-BR.txt`: the balloon texts (`key = text`).
- `setup.sql`: run on a copy of `employee.fdb` before each scene.
- `colors-demo.txt`: the color scheme of scene 07.
- `demo.py`: makes the data files, records the scenes and makes the GIFs.

## Linux

Needs: `vhs`, `ttyd`, `ffmpeg`, `gifski`, `gifsicle`, Python 3 with Pillow
and pyarrow, the fonts JetBrains Mono and DejaVu Sans. `FBDIR` is a Firebird
5 directory with `bin/hqisql` and `examples/empbuild/employee.fdb`.

```
python3 demo.py record FBDIR      # work/raw/linux/*.mp4
python3 demo.py gif linux         # docs/img/linux/{en,pt-BR}/*.gif
```

Give scene names to make only these scenes (`demo.py record FBDIR
01-editor`). `demo.py gif` prints the length and the size of each GIF and
fails if a GIF is longer than 30 s or bigger than 3 MB. Check
`work/sheets/<os>/*.png`: one frame for each balloon and the last frame.

## Windows

VHS does not run Windows console programs. On Windows, `demo.py record`
runs `hqisql.exe` in a pseudo console (pywinpty), sends the keys of the
tape in real time and draws the frames with the terminal emulator pyte in
the look of Windows Terminal (Cascadia Mono, Campbell colors). The screen
is not used: the recording works on a locked computer too.

Needs: Python 3 with pywinpty, pyte, Pillow and pyarrow, `ffmpeg`. `FBDIR`
is a Firebird 5 directory with `hqisql.exe` and
`examples\empbuild\employee.fdb`.

```
python demo.py record FBDIR      # work/raw/windows/*.mp4
```

Then copy `work/raw/windows` to a Linux computer and run
`python3 demo.py gif windows` there (`gifski` and `gifsicle`).

## Notes


- Do not use `Escape` in a tape: replxx reads Escape and the next key as
  Alt+key. Leave Ctrl-R search with Ctrl-G or Enter.
- The balloon times come from the `Type` speed and the `Sleep` commands,
  scaled to the length of the video.
