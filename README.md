# Okular – Universal Document Viewer

> **This is a personal fork of [Okular](https://github.com/KDE/okular).**
> Upstream is developed by KDE at https://invent.kde.org/graphics/okular; this fork lives at
> https://github.com/chen42/okular.
>
> It adds a comment system modelled on the review comments of Google Docs and Microsoft Word:
> select text, add a comment, and the comment appears in a panel next to the page with its author,
> date and the passage it is attached to. Comments can be edited, resolved (hidden from the list)
> and reopened later. The fork is based on upstream master at release 26.11.70; everything outside
> the comment feature is unmodified Okular.

Okular can view and annotate documents of various formats, including PDF, Postscript, Comic Book, and various image formats.
It supports native PDF annotations.

## Comments

Comments are anchored to a text passage on the page and listed in a panel to the right of the page,
similar to the review comments of a word processor.

### Adding a comment

Either:

* **Area Selection** (`Tools > Area Selection`, `Ctrl+3`): drag a rectangle over the text, release the
  mouse and choose **Add Comment** in the menu that appears, or
* **Text Selection** (`Tools > Text Selection`): select the text, then right-click the selection and
  choose **Add Comment**.

The comment panel opens with an empty editor. Write the comment and press **Save**; cancelling an
untouched comment discards it. The marked passage is shown with a light, translucent yellow
highlight.

The selection menus only offer **Add Comment** when the drag actually covers text, so a scanned page
without a text layer, or a rectangle drawn over empty space, has nothing to attach a comment to.

### Working with comments

Each card in the panel shows the author, the date, the comment and, below it, the quoted passage.
The buttons on a card allow to:

* change the comment text (pencil),
* mark it as resolved (check mark),
* reopen a resolved comment (arrow), after enabling **Show resolved comments**,
* delete it (trash),
* jump to the page it is attached to (`Page N`).

Resolved comments disappear from the list so that only the comments that still need attention are
shown. They stay in the document and can be shown again with the **Show resolved comments** box at
the top of the panel.

The panel is toggled with `Settings > Show Comments Panel` or `Ctrl+Alt+C`, it opens automatically
when a comment is created, and its width is remembered between sessions.

### How comments are stored

A comment is a highlight annotation. Its text, author, creation date and modification date are
annotation fields Okular already stores, the marked passage is recorded by the highlight geometry,
and a new `Annotation::CommentState` (open or resolved) is saved as a `commentState` attribute of the
annotation. Comments therefore survive closing and reopening a document, and they take part in undo
and redo.

Saving to an Okular document archive (`.okular`) keeps everything. Saving directly into a PDF turns
the comment into a normal PDF text markup annotation, which keeps the highlight, the comment text,
the author and the dates, but the resolved state exists only in Okular.

## Installation

### Prerequisites

* CMake 3.22 or newer
* Qt 6.8 or newer
* KDE Frameworks 6.13 or newer
* `extra-cmake-modules`
* Optional: Poppler Qt6 bindings for PDF support; TIFF, DjVu, LibSpectre, EPub and Discount for the
  corresponding generators

Install the development packages of your distribution. On Arch Linux the relevant packages are
`base-devel cmake extra-cmake-modules qt6-base qt6-svg poppler-qt6` plus the `kf6-*` packages listed
in the top-level `CMakeLists.txt`.

### Build and install

```bash
git clone https://github.com/chen42/okular.git
cd okular
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOKULAR_UI=desktop -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

If the configure step stops on a package you do not want, disable it, for example
`-DCMAKE_DISABLE_FIND_PACKAGE_EPub=ON` to skip the EPub generator when its headers are missing.

### Make the part and generator plugins visible

This step is needed whenever the plugin directory is not one Qt searches, which is the case for the
`/usr/local` prefix used above: CMake installs the part plugin (`kf6/parts/okularpart`) and the
generators (`okular_generators`) to `/usr/local/lib/plugins`, while KDE resolves those plugin ids
against Qt's plugin paths (`/usr/lib/qt6/plugins` plus whatever `QT_PLUGIN_PATH` contains). Without
the path below, Okular starts and then reports
`Unable to find the Okular component: Could not find plugin kf6/parts/okularpart`.

Two ways to handle it, pick one:

**Environment variable**, simplest if you always start Okular from a shell:

```bash
export QT_PLUGIN_PATH=/usr/local/lib/plugins   # put this in your shell profile
```

**Launcher script**, so the menu entry and file associations work without any environment setup:

```bash
sudo mkdir -p /usr/local/lib/okular
sudo mv /usr/local/bin/okular /usr/local/lib/okular/okular
sudo tee /usr/local/bin/okular >/dev/null <<'EOF'
#!/bin/sh
export QT_PLUGIN_PATH=/usr/local/lib/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
export XDG_DATA_DIRS=/usr/local/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}
exec /usr/local/lib/okular/okular "$@"
EOF
sudo chmod 755 /usr/local/bin/okular
sudo sed -i 's|^Exec=okular|Exec=/usr/local/bin/okular|' \
    /usr/local/share/applications/org.kde.okular.desktop
sudo update-desktop-database /usr/local/share/applications
```

### Run

```bash
okular            # installed
/usr/local/bin/okular some-file.pdf
```

If you only want to try the build without installing it, run it from the build tree:
`./build/bin/okular some-file.pdf`.

### Uninstallation

Remove exactly the files that were installed, using the manifest CMake writes during installation:

```bash
sudo xargs -a build/install_manifest.txt rm -f
sudo rm -rf /usr/local/lib/okular            # only if you used the launcher script
sudo update-desktop-database /usr/local/share/applications
```

Keep the build directory until you uninstall, since `install_manifest.txt` lives there. Your settings
and comments are user data in `~/.config/okularpartrc` and `~/.local/share/okular`; remove those
separately if you want a clean slate.

## Upstream resources

### Downloads

For download and installation instructions, see https://okular.kde.org/download.php

### User manual

https://docs.kde.org/?application=okular&branch=stable5

### Bugs

Upstream bugs: https://bugs.kde.org/buglist.cgi?product=okular

Please report upstream bugs on Bugzilla (https://bugs.kde.org/enter_bug.cgi?product=okular), and not on
the KDE GitLab instance (https://invent.kde.org). Problems specific to the comment feature of this
fork belong in this fork's issue tracker: https://github.com/chen42/okular/issues

### Mailing list

https://mail.kde.org/mailman/listinfo/okular-devel

### Source code

This fork: https://github.com/chen42/okular

Upstream: https://invent.kde.org/graphics/okular.git

The Okular repository contains the source code for:
 * the `okular` desktop application (the “shell”),
 * the `okularpart` KParts plugin,
 * the `okularkirigami` mobile application,
 * several `okularGenerator_xyz` plugins, which provide backends for different document types.

In this fork the comment feature is implemented in `part/commentpanel.{h,cpp}`, wired into the shell in
`part/part.cpp` and into the page context menus in `part/pageview.cpp`, and backed by
`Annotation::CommentState` in `core/annotations.{h,cpp}`. `autotests/commentpaneltest.cpp` covers the
comment lifecycle, both context menus and the persisted state.

### Apidox

https://api.kde.org/okular/html/index.html

## Contributing

Okular uses the merge request workflow.
Merge requests are required to run pre-commit CI jobs; please don’t push to the master branch directly.
See https://community.kde.org/Infrastructure/GitLab for an introduction.

### Build instructions

Okular can be built like many other applications developed by KDE.
See https://community.kde.org/Get_Involved/development for an introduction.

If your build environment is set up correctly, you can also build Okular using CMake:

```bash
git clone https://invent.kde.org/graphics/okular.git
cd okular
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir ..
make
make install
```

Okular also builds tests in the build tree. To run them, you have to run `make install` first.

If you install Okular in a different path than your system install directory it is possible that you need to run

```bash
source prefix.sh
```

so that the correct Okular instance and libraries are picked up.
Afterwards one can run `okular` inside the shell instance.
The source command is also required to run the tests manually.

As stated above, Okular has various build targets.
Two of them are executables.
You can choose which executable to build by passing a flag to CMake:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir -DOKULAR_UI=desktop ..
```
Available options are `desktop`, `mobile`, and `both`.

### clang-format

The Okular project uses clang-format to enforce source code formatting.
See [README.clang_format](https://invent.kde.org/graphics/okular/-/blob/master/README.clang-format) for more information.
