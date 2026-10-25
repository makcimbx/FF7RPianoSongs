# FF7RPianoSongs menu themes: user guide

Themes change the images and colours of the piano song selection menu. Making
a theme needs no PAK mods, no Unreal Editor and no changes to game files.

This guide applies to FF7RPianoSongs versions with theme support. Check the
README from the same archive for which game builds your mod version supports.

## 1. Installing and choosing a theme

1. Close the game completely.
2. Put the theme folder into `Themes` next to the installed mod DLL. In a normal
   installation that is `FINAL FANTASY VII REBIRTH/End/Binaries/Win64/Themes/`.
3. Check the nesting: `Themes/theme-name/theme.ini`, not
   `Themes/theme-name/theme-name/theme.ini`.
4. Start the game and open the piano song menu.

The archive includes two complete example themes: `steins-gate` and
`evangelion`. They replace almost every image and colour in the menu and show
which PNG does what and at what size to draw it, which makes them a good
starting point for your own theme. **Original**, the game's own look, is
selected by default.

| Device | Action |
| --- | --- |
| Keyboard | **Ctrl + F6**: next theme |
| Gamepad | Hold **L3**, press **L2 / LT**: previous theme |
| Gamepad | Hold **L3**, press **R2 / RT**: next theme |

L3 is a left-stick click. Release the key or trigger between switches: holding
it does not keep cycling themes. Pressing both triggers together does not
switch; release them before trying again. The game window must have focus.

Original is part of the same cycle; custom themes are ordered by their ID. A
successfully applied choice is remembered. A theme that was not ready or was
rejected does not count as applied. You do not need to hold the shortcut while
a theme is being prepared.

**You can switch between already loaded themes right in the menu. Adding a
theme, editing `theme.ini` or replacing a PNG needs a full game restart.**

### Changing the keyboard shortcut

In `FF7RPianoSongs.ini` next to the DLL:

```ini
[Theme]
Selected=original
KeyboardFunctionKey=6
```

`KeyboardFunctionKey=1..12` selects Ctrl + F1..F12; `0` turns off only the
keyboard shortcut, not the gamepad. An invalid number falls back to F6. Do not
pick a key already used by preview or autoplay: a conflicting theme shortcut is
turned off. Other INI sections need no changes.

## 2. Creating your own theme

Copy one of the examples under a new name, or create:

```text
Themes/
  my-theme/
    theme.ini
    panel.png
    artwork.png
```

The folder name is the **theme ID**: 1–64 Latin letters, digits, `_` or `-`,
no spaces. The name `original` is reserved, so do not create a folder with it.
Lowercase letters and unique IDs are the easiest to work with.

A minimal working theme can change just a colour:

```ini
[Theme]
Version=1

[Colors]
MusicList/Txt_Title_1=#E8EDFFFF
```

An example with images and row states:

```ini
[Theme]
Version=1

[Images]
MusicList/Bg_1=panel.png
MusicList/BaseImg=artwork.png

[Colors]
MusicList/BaseImg=#FFFFFFFF
MusicList/Txt_Title_1=#E8EDFFFF
MusicList/Arrow_Left=#E8EDFFFF
MusicList/Arrow_Right=#E8EDFFFF
MusicListCell/Txt_Title@normal=#D5DCE8FF
MusicListCell/Txt_Title@selected=#FFD46AFF
MusicListCell/Txt_Title.OutlineColorAndOpacity=#00000000
MusicListCell/Txt_Title.ShadowColorAndOpacity=#00000000
```

Save `theme.ini` as **UTF-8**, not as `theme.ini.txt`. A BOM is fine. Section
and element names are case-sensitive. Blank lines and whole-line comments
starting with `;` are allowed. Do not put a comment after a value. The same
setting cannot be declared twice, including its short and full forms.

PNG paths are relative to the theme folder, without quotes. Subfolders are fine,
for example `images/panel.png`. Absolute paths, `..` and links outside the theme
folder are not allowed.

**You do not need to list the whole interface.** Properties you leave out keep
the game's look; when you switch from another theme, its extra changes are
reset, not inherited. An unknown element or an incompatible property is a theme
error, not an instruction to skip that line and apply the rest.

## 3. Images: format, size and transparency

- Use **PNG**, preferably plain **RGB/RGBA, 8 bits per channel, sRGB**. For
  transparency, export RGBA with straight, not premultiplied, alpha. JPEG, DDS,
  SVG and video are not theme image formats.
- Width and height: **1 to 4096 pixels each**. Square images and power-of-two
  sizes are not required: 18×11, for example, is fine.
- 16-bit PNGs are read but reduced to 8 bits per channel, so a deeper source
  rarely makes sense for a theme.
- A larger PNG **does not make a menu element bigger**. The element keeps its
  size, position, stretching mode and rotation. Extra resolution costs memory,
  and empty margins around the drawing shrink its visible part.
- Elements come in two kinds, and they need to be drawn differently:
  - **Whole image** (`Bg_Pettern`, `BaseImg`, `UnderLine…`, `SpeedIcon`) is
    stretched over the element's area as a whole. Aspect ratio and transparent
    margins matter; you can draw at 2–4× resolution for sharpness.
  - **Nine-slice** (`Bg_1`, `Bg_2`, `Base1_4`, `Base1`, `Shadow`, `Shadow_3`,
    `Deco`, row and button backgrounds): corners are drawn without
    distortion, while the edges and centre stretch. On-screen corner size is
    **PNG pixels × the element's margin** (usually 0.5). A 512×512 frame PNG
    gives 256-unit corners, four times larger than the original ones, so the
    corner art becomes huge. Draw these images at **1 pixel per menu unit**
    (sizes in the table below); 1.5× deliberately enlarges the corners. The
    centre strips stretch, so keep them uniform: a pattern there turns into
    stripes.
- The image colour is also multiplied by the element colour and its tint. To
  show your own colours untinted, set the matching colours to white
  (`#FFFFFFFF`). The game's own fades still work.
- Some elements are quite transparent in the original menu, and theme alpha can
  only lower that further: the `Bg_Pettern` watermark ≈ 0.3, the `BaseImg`
  artwork ≈ 0.31, the `ListBg` strips inside rows ≈ 0.2, the `Shadow` window
  shadow ≈ 0.8, the row button background ≈ 0.5. To make such an element
  visible, draw it brighter and more opaque than you want it to look.

### Choosing the resolution and drawing each element

The sizes below are the original brushes in menu units; for nine-slice elements
this is also the recommended PNG size (margins 0.5 unless stated otherwise).

| Element | What it is and how to draw it |
| --- | --- |
| `MusicList/Bg_1` | Fill for the whole window, nine-slice **14×14**. The corners are only 7 units, so use a flat or slightly rounded fill, not a pattern. Add window texture through `Bg_Pettern`. |
| `MusicList/Bg_2` | Outer window frame, nine-slice **128×128** (64-unit corners). Ornaments go in the corners, lines along the edges, and the centre stays transparent. |
| `MusicList/Shadow` | Shadow or glow around the window, nine-slice **129×100**, dark in the original with alpha ≈ 0.8. The element colour can turn it into a coloured halo. |
| `MusicList/Bg_Pettern` | Large watermark behind the song list, whole image **375×335**, original alpha ≈ 0.3. You can draw it at 3× (1125×1005). Bright details under the rows make titles hard to read. |
| `MusicList/ListBg` | Despite the name, not the list background but a title-height strip **inside every row**: nine-slice **16×16**, alpha ≈ 0.2. The centre pixel of the PNG stretches over the whole strip, so a pattern never shows, and a dot in the texture's centre tints every row. The examples hide it with `MusicList/ListBg=#FFFFFF00`; if you use it, give it only a flat solid fill. |
| `MusicList/Base1_4` | Plate behind the window title, nine-slice **32×32**, almost black in the original. |
| `MusicList/Base1` | Plate behind the rank and required score, the same **32×32** brush. It can use the same image or a separate one. A light plate needs dark `Txt_Rank`/`Txt_TargetScore` text (or `Txt_Title_1` for the title). |
| `MusicList/Shadow_3` | Frame of the details panel with a strip on the left, nine-slice **18×19** with uneven margins (left ≈ 0.76, right 0.3, top/bottom 0.5): the left strip is the leftmost 14 pixels of the PNG. |
| `MusicList/UnderLine1_1`, `UnderLine1_2` | Row separators in the details panel; a whole image stretched into a thin strip. A horizontal line fading towards its ends works well. |
| `MusicList/UnderLine1_3` … `UnderLine1_6` | Rank table separators. |
| `MusicList/UnderLine1`, `UnderLine2` | Lines around the high score, orange in the original. |
| `MusicList/BaseImg` | Artwork under the rank table, whole image **160×165**, alpha ≈ 0.31. Use a transparent background; you can draw it at 4× (640×660). Replacing `Bg_1` does not replace it. |
| `MusicList/Deco` | High-score decoration, nine-slice **55×60**. Leave transparent strips around the vertical and horizontal middle and draw only in the corners. An opaque middle stretches into a line through the high score. |
| `MusicListCell/EndImage` | Inner row layer, nine-slice **16×16**. It was not visible in the observed menu: `Pnl_Light` hides it on normal rows, and the game itself hides it on the selected row. The row background comes from `MainMenu_Btn`. |
| `MusicListCell/EndImage_172` | Selected row background. It stretches over the whole row; a 512×64 horizontal gradient looks better than 16×16. |
| `MusicListCell/MainMenu_Btn.Normal` … `.Disabled` | Background of the whole row (title and notes) in the matching state, nine-slice **16×16**. The game draws it at alpha ≈ 0.5, and at 0 on the selected row. `.Normal` is a normal row: a transparent `.Normal` leaves rows without a background. `.Hovered` is under the cursor or focus. |
| `MusicListCell/Icon_New` | New-song badge; a small whole image, draw it at about 16×16. |
| `SpeedIcon/SpeedIcon.0`, `.1`, `.2` | Original area **18×11**. For crisp small marks, draw at exactly that size without large transparent margins. |
| `MusicList/Arrow_Left`, `Arrow_Right` | The game already draws their shape. For a simple recolour, use only `[Colors]` without replacing the image. A pre-cut arrow image can get cropped a second time. |

**`MusicListCell/Pnl_Light.ContentColorAndOpacity` tints the selected row
background.** It is golden in the original; with your own `EndImage_172` image,
set it to white (`#FFFFFFFF`), or your image picks up the golden tint.

These sizes describe the observed elements. **They are not a required size for
every PNG and do not promise to change the menu layout.** The theme format does
not move elements or change fonts, control buttons or game logic.

## 4. Colours and text layers

Colours are written strictly as **`#RRGGBBAA`**, including the last two alpha
digits:

| Value | Result |
| --- | --- |
| `#FFFFFFFF` | White, opaque |
| `#FFD46AFF` | Gold, opaque |
| `#00000000` | Fully transparent |
| `#FFFFFF80` | White, about half transparent |

RGB is ordinary sRGB; there is no need to convert it to linear values by hand.
Theme alpha is multiplied by the game's alpha, so a theme does not cancel the
game's fade-ins, fade-outs or hidden elements. `FF` keeps the original opacity
and fades; it does not force a hidden element to appear.

The short key `MusicList/Txt_HighScore` changes the main colour. Text can have
**separate outline, shadow and bottom colours**, and the main colour does not
recolour them:

```ini
[Colors]
MusicList/Txt_HighScore=#FFFFFFFF
MusicList/Txt_HighScore.OutlineColorAndOpacity=#00000000
MusicList/Txt_HighScore.ShadowColorAndOpacity=#00000000
MusicList/Txt_HighScoreTitle=#FFFFFFFF
MusicList/Txt_HighScoreTitle.OutlineColorAndOpacity=#00000000
MusicList/Txt_HighScoreTitle.ShadowColorAndOpacity=#00000000
```

`.BottomColorAndOpacity` sets the bottom colour or gradient when you really need
it. Do not add it to every text by default: it can turn on a bottom colour where
the original had none. To change the tint or hide the shadow, the main colour,
`.OutlineColorAndOpacity` and `.ShadowColorAndOpacity` are enough.

## 5. Element names and states

A key looks like `area/element-name.property@state`.
Suffixes depend on the element; not every property works on every element.

| Area | What it changes |
| --- | --- |
| `MusicList/` | Main window, song details, high score, frames and decoration |
| `MusicListCell/` | Template for every current song row |
| `SpeedIcon/` | All current speed indicators |
| `Cursor/` | Selected-row pointer and its own glow layers |

`@normal` and `@selected` are allowed **only for `MusicListCell`**. Without a
suffix the property applies to both. This is the selected-row state, not mouse
hover.

### Commonly used names

| Group | Names after `/` |
| --- | --- |
| Main window background and frames | `Bg`, `Bg_1`, `Bg_2`, `Bg_Pettern`, `ListBg`, `Shadow`, `Shadow_3`, `Base1_4`, `Base1` |
| Separators | `UnderLine1_1` … `UnderLine1_6`, `UnderLine1`, `UnderLine2` |
| Artwork and arrows | `BaseImg`, `Deco`, `Arrow_Left`, `Arrow_Right` |
| Title and details | `Txt_Title_1`, `Txt_Info1`, `Txt_Info2`, `Txt_Info3`, `Txt_Time`, `Txt_Volume`, `Txt_Alert` |
| High score and target | `Txt_HighScore`, `Txt_HighScoreTitle`, `Txt_Rank`, `Txt_TargetScore` |
| Ranks and thresholds | `Txt_RankS`, `Txt_RankA`, `Txt_RankB`, `Txt_RankC`, `Txt_ScoreS`, `Txt_ScoreA`, `Txt_ScoreB`, `Txt_ScoreC` |
| Main window panels | `Pnl_All`, `Pnl_ListWindow`, `Pnl_Title`, `Pnl_Info`, `Pnl_SpeedList`, `Pnl_HighScore`, `Pnl_TargetScore` |
| Song row | `EndImage_172`, `EndImage`, `Icon_New`, `MainMenu_Btn`, `Txt_Title`, `Txt_Rank`, `Txt_Difficulty`, `Pnl_Light`, `Pnl_Content` |
| Speed indicator | `SpeedIcon` |
| Pointer and glow (`Cursor/`) | `Cursor_Left`, `Img_BtnLight`, `HoverBase` |

The table is not exhaustive: a theme finds a menu element by its name, and any
name from these areas works if the property suits the element. Image elements
are described in section 3.

`Bg_Pettern` is the game's own spelling, not a typo in this guide. The
high-score decoration only shows for a song that has a high score. `Txt_Rank`
in a row and `Txt_RankS` in the main panel are different elements with separate
colour layers.

### Properties by purpose

| Purpose | Key |
| --- | --- |
| Main image in `[Images]` | Name without a suffix, or `.Brush` |
| Main image tint in `[Colors]` | `.BrushTint` |
| Element main colour | No suffix or `.ColorAndOpacity`; both are the same setting |
| Panel content colour | `.ContentColorAndOpacity` |
| Button background | `.BackgroundColor` |
| Text layers | `.OutlineColorAndOpacity`, `.ShadowColorAndOpacity`, and `.BottomColorAndOpacity` when needed |
| Button image states | `.Normal`, `.Hovered`, `.Pressed`, `.Disabled` |
| Speed indicator images | `.0`, `.1`, `.2` |

Button states apply to `MainMenu_Btn`: for example,
`MusicListCell/MainMenu_Btn.Hovered=hover.png` in `[Images]`, while the same key
with a colour in `[Colors]` sets that image's tint. Do not confuse `.Hovered`
with `@selected`: the first is a button image state, the second is the selected
row.

### Three speed images

```ini
[Images]
SpeedIcon/SpeedIcon.0=empty.png
SpeedIcon/SpeedIcon.1=configured.png
SpeedIcon/SpeedIcon.2=recorded.png

[Colors]
SpeedIcon/SpeedIcon=#FFD46AFF
SpeedIcon/SpeedIcon.0=#FFFFFFFF
SpeedIcon/SpeedIcon.1=#FFFFFFFF
SpeedIcon/SpeedIcon.2=#FFFFFFFF
```

`.2` is shown for marks up to the speed recorded in the result; otherwise `.1`
up to the currently configured speed; otherwise `.0`, an empty mark. It does not
mean "normal or selected song". Make all three images easy to tell apart. White
per-image tints matter: otherwise the original black or blue tint can darken or
recolour your PNG. The indicator's main colour also takes part in the tint.

### Selected-row pointer and glow

This is the separate `Cursor/` area. It is not the speed arrows
`MusicList/Arrow_Left` and `Arrow_Right`, and not the row background
`MusicListCell/Pnl_Light`.

```ini
[Images]
Cursor/Cursor_Left=cursor.png
Cursor/Img_BtnLight=glow.png

[Colors]
Cursor/Cursor_Left=#FFFFFFFF
Cursor/Img_BtnLight=#77D7E8FF
Cursor/HoverBase=#77D7E880
```

You can change only the colours without adding `[Images]`. `Cursor_Left` is the
pointer itself, `Img_BtnLight` is the glow image, and `HoverBase` is an extra
highlight backing. In the observed menu their original sizes are **48×37**,
**132×132** and **16×16** respectively. PNG size does not change element
geometry; for the glow, use a transparent background and soft alpha.
`HoverBase` keeps its stretchable frame geometry, so the nine-slice rules apply
to it too.

The game still controls the pointer movement and the glow fade: theme alpha is
multiplied by the current animation. The `@normal`/`@selected` suffixes are not
needed or supported here. The Original theme restores the original images and
colours.

### Audio preview palette

The preview window can use theme colours too. These are separate keys in the
same `[Colors]` section, not main-menu element names:

```ini
[Colors]
Preview/Background=#101827F0
Preview/Border=#D9AF64FF
Preview/Accent=#77D7E8FF
Preview/Track=#27384AFF
Preview/Text=#F2F5FFFF
```

| Key | What it colours |
| --- | --- |
| `Preview/Background` | Window background |
| `Preview/Border` | Frame and the excerpt region on the song timeline |
| `Preview/Accent` | Spectrum bars |
| `Preview/Track` | Background track of the timeline |
| `Preview/Text` | Label, stop icon, bar tops and position marker |

You can set only the colours you need: missing ones return to the defaults
rather than being inherited from the previous theme. The colour format is the
same `#RRGGBBAA`, and the window's normal fade is kept. These settings change
the look, **not the sound, the chosen excerpt or the preview controls**. PNG
replacement for `Preview/` and `@normal`/`@selected` states for these keys are
not supported.

## 6. File and memory limits

| Resource | Checked limit |
| --- | --- |
| `theme.ini` | Up to **64 KiB** |
| One PNG file | Up to **32 MiB** |
| One PNG's dimensions | From **1×1** to **4096×4096** inclusive |
| Total PNG file size across theme entries | Up to **128 MiB** |
| Estimated decoded image size across theme entries | Up to **128 MiB** |
| Total accounting of prepared images and style variants in the game | **128 MiB**, including accounted supporting data |

These are data and memory limits, **not CPU or GPU load limits**. There is no
separate quota on the number of themes, colour lines, brushes or menu openings.
Allocation failures and incompatible elements can still reject a theme. The
total budget is not a measurement of the process's whole video memory, so do not
plan to fill it with images to the brim.

A plain RGBA image takes about `width × height × 4` bytes: 1024×1024 is 4 MiB
and 4096×4096 is 64 MiB, even if the PNG compresses well on disk. A 16-bit
source is estimated at 8 bytes per pixel. Referencing the same file in several
entries also counts towards the total input size. Small icons do not need
screen-sized textures.

## 7. If something looks wrong

| Problem | What to check |
| --- | --- |
| The theme does not appear | Correct nesting, `theme.ini`, UTF-8, `Version=1`, a unique ID. Did you fully restart the game after editing? |
| The shortcut does not work | The menu is open, the window has focus, and the key is not disabled or clashing with preview or autoplay. Press and release; do not hold it to "load". |
| A theme is rejected | Typos in keys, duplicates, an unsuitable property, a missing or invalid PNG, sizes and memory. Check the log. |
| An image is black or wrongly tinted | The element colour and the image tint are multiplied. Check both, especially the three SpeedIcon tints. The selected row background is also tinted by `MusicListCell/Pnl_Light.ContentColorAndOpacity`. |
| Rows have no background, notes sit on the panel background | The normal row background is `MusicListCell/MainMenu_Btn.Normal`, not `EndImage`. Give it an opaque image; the game still shows it at no more than ≈ 50 %. |
| Frame or plate corners are huge, with blown-up art | The nine-slice PNG is drawn at an enlarged scale. Corner size = PNG pixels × margin; shrink the PNG to the original size from the table in section 3. |
| An element is barely visible | The element's original alpha is low (`Bg_Pettern`, `BaseImg`), and a theme can only lower it. Make the PNG itself brighter and more opaque. |
| A coloured rectangle under the title inside rows | That is `MusicList/ListBg`, the stretched centre of its PNG. Hide it (`MusicList/ListBg=#FFFFFF00`) or give it a flat fill. |
| An arrow turned into a fragment | Remove its PNG override; start by recolouring the original arrow through `[Colors]`. |
| A stripe runs through the high score | The `Deco` art reaches the stretched centre. Make both middle strips transparent, with some room for filtering. |
| Text still has yellow edges or a shadow | Recolour or hide the layers of the right `MusicList/Txt_...` element, not the same-named `MusicListCell` one. |
| The indicator is too small or blurry | Check the actual 18×11 size, transparent margins and that the three states differ; a bigger PNG does not get more space on screen. |
| Changes do not show | Files are read when the game starts, not on every Ctrl+F6. Make sure you are editing the selected folder. |

The log is `FF7RPianoSongs.log` next to the DLL. Look for the theme ID and
messages with `menu_theme_runtime`: a read or preparation error, a rejected
element or a failed apply is not a successful switch. For a report, save the log
right after quitting the game, along with `theme.ini`, the problem PNGs and a
short video. For artefacts during fast motion, also say whether Frame
Generation is on.

### Going back to the original menu

Select Original by switching as usual. If that is inconvenient, close the game
and set `Selected=original` in the `[Theme]` section of `FF7RPianoSongs.ini`.
To turn off third-party themes, move their folders **out of `Themes`** and start
the game again. Do not delete your settings, songs or saves.

Save your own edits before updating the examples. Do not unpack a new version
over your own theme of the same name without a backup.
