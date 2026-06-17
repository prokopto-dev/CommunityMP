CommunityMP
===========

Copyright (c) 2026 Alex Cooper and OpenACAI Inc. for CommunityMP-original
material authored by or assigned to them.

TES3MP-origin GPL source preserves the upstream notice:

    Copyright (c) 2016-2022, David Cernat & Stanislav Zhukov

See [UPSTREAM_NOTICES.md](UPSTREAM_NOTICES.md) for scope and additional notices.

CommunityMP is an unofficial community-maintained multiplayer fork for current
OpenMW. It combines code derived from [OpenMW](https://github.com/OpenMW/openmw)
and [TES3MP 0.8.1](https://github.com/TES3MP/TES3MP/tree/0.8.1), plus new
compatibility, networking, deployment, documentation, and test work in this
repository.

OpenMW is an open-source open-world RPG game engine that supports playing
Morrowind by Bethesda Softworks. TES3MP adds multiplayer functionality on top
of OpenMW. You need to own Morrowind to use this project to play Morrowind.

This project is not the official TES3MP project and is not endorsed by the
TES3MP, OpenMW, Bethesda Softworks, or ZeniMax teams. The name "TES3MP" is used
only to identify upstream compatibility and historical origin; it is not used as
this project's name or brand. This repository does not claim any trademark
rights in any upstream name, logo, or related mark.

Historical references to this fork may use TES3MP-derived naming, including
TES3MP Refresh. The current project-facing name is CommunityMP.

Credits
-------

CommunityMP includes work from OpenMW, TES3MP, CommunityMP contributors, and
bundled third-party projects.

* OpenMW-origin material is credited to the OpenMW authors and contributors.
* TES3MP-origin multiplayer systems, scripting systems, synchronization,
  server browser, master server, and related source are credited to David
  Cernat and Stanislav Zhukov (Koncord).
* TES3MP deployment scripts are credited to Grim Kriegor. The TES3MP logo is
  credited to Texafornian.
* CommunityMP-original material authored by or assigned to Alex Cooper and
  OpenACAI Inc. is credited to Alex Cooper and OpenACAI Inc. Other CommunityMP
  contributors retain credit and copyright in their own copyrightable
  contributions unless a file-level notice or written assignment says
  otherwise.

See [UPSTREAM_NOTICES.md](UPSTREAM_NOTICES.md) for preserved copyright,
license, attribution, and third-party notices.

* OpenMW base: current OpenMW master lineage
* TES3MP base: official TES3MP 0.8.1 lineage
* CommunityMP code: new compatibility, networking, GameNetworkingSockets
  transport, serializer, deployment, documentation, and test work in this
  repository
* License: OpenMW-origin and TES3MP-origin code remains GPLv3-family code under
  its upstream notices. CommunityMP original code is AGPLv3-or-later, with
  additional attribution and origin-marking terms. See [LICENSE](LICENSE) and
  [LICENSES/AGPL-3.0.txt](LICENSES/AGPL-3.0.txt). Preserved upstream copyright
  and attribution notices are summarized in [UPSTREAM_NOTICES.md](UPSTREAM_NOTICES.md).
  Maintainer-facing rationale is summarized in
  [docs/communitymp-licensing-notes.md](docs/communitymp-licensing-notes.md).

Licensing notes
---------------

This repository is a combined work. OpenMW-origin material remains copyrighted
by the OpenMW authors and contributors and remains under its upstream GPLv3
license notices unless a file-level notice says otherwise. TES3MP-origin
material remains copyrighted by the TES3MP authors and contributors and remains
GPLv3-covered, with the additional terms published with TES3MP 0.8.1 to the
extent those terms apply and are valid under GPLv3 section 7. Preserved TES3MP
copyright and contributor notices are listed in [UPSTREAM_NOTICES.md](UPSTREAM_NOTICES.md).

CommunityMP original material authored by or assigned to Alex Cooper and
OpenACAI Inc. is Copyright (c) 2026 Alex Cooper and OpenACAI Inc. and is
licensed under the GNU Affero General Public License, version 3 or later. Other
CommunityMP contributors retain copyright in their copyrightable contributions
unless a file-level notice or written assignment says otherwise. The combined
work is documented under GPLv3 section 13, which permits linking or combining
GPLv3-covered work with AGPLv3-covered work while keeping the GPLv3 terms on the
GPL-covered portions and applying the AGPLv3 network source-code obligation to
the AGPL-covered material and combined work to the extent required by those
licenses.

The CommunityMP AGPLv3 section 7 terms in [LICENSE](LICENSE) are intended to
be clearer and narrower than the inherited project notices. They preserve legal
notices and attribution, require modified versions and hosted network services
to identify themselves accurately, prohibit false endorsement or publicity use,
clarify that no trademark rights are granted, disclaim warranty and liability,
and cover indemnity only when a distributor or service operator separately
assumes liability to a recipient or user.

Private server Lua code can stay private when it is independently authored and
only uses the normal TES3MP/OpenMW-MP server scripting interfaces. The AGPLv3
network source-code obligation applies to covered CommunityMP code and
derivatives of covered code; it is not intended to force disclosure of separate
server scripts, configuration, gameplay rules, quests, moderation tools, economy
systems, or administration code merely because a server loads them at runtime.
If a script copies, modifies, embeds, or is otherwise derived from covered
OpenMW, TES3MP, CommunityMP, or third-party source code, the license for that
covered source still applies to the derived material.

Although the upstream TES3MP wording says that GPLv3 sections 15 and 16 are
replaced for TES3MP source code, that is treated here as a GPLv3 section 7
additional-term notice for TES3MP-origin material, not as a modification of the
GNU GPLv3 license document itself. If any part of the upstream notice is not a
valid GPLv3 section 7 term, this repository does not assert it beyond what
GPLv3 permits.

The project-facing name is CommunityMP. OpenMW and TES3MP names are used only
for accurate attribution, compatibility descriptions, and preserved legal
notices. CommunityMP must not present itself as the official OpenMW project, the
official TES3MP project, or an endorsed Bethesda/ZeniMax project.

Bundled components can carry their own licenses. For example, the bundled
TES3MP server scripts under `files/tes3mp/server`, `extern/Lua-io2`, and
`extern/lua-cjson` include their own license files. A broader summary is kept
in [UPSTREAM_NOTICES.md](UPSTREAM_NOTICES.md). Font licenses are kept in:

* `files/data/fonts/DejaVuFontLicense.txt`
* `files/data/fonts/DemonicLettersFontLicense.txt`
* `files/data/fonts/MysticCardsFontLicense.txt`

Current Status
--------------

The main quests in Morrowind, Tribunal and Bloodmoon are all completable. Some issues with side quests are to be expected (but rare). Check the [bug tracker](https://gitlab.com/OpenMW/openmw/-/issues/?milestone_title=openmw-1.0) for a list of issues we need to resolve before the "1.0" release. Even before the "1.0" release, however, OpenMW boasts some new [features](https://wiki.openmw.org/index.php?title=Features), such as improved graphics and user interfaces.

Pre-existing modifications created for the original Morrowind engine can be hit-and-miss. The OpenMW script compiler performs more thorough error-checking than Morrowind does, meaning that a mod created for Morrowind may not necessarily run in OpenMW. Some mods also rely on quirky behaviour or engine bugs in order to work. We are considering such compatibility issues on a case-by-case basis - in some cases adding a workaround to OpenMW may be feasible, in other cases fixing the mod will be the only option. If you know of any mods that work or don't work, feel free to add them to the [Mod status](https://wiki.openmw.org/index.php?title=Mod_status) wiki page.

Getting Started
---------------

* [Official forums](https://forum.openmw.org/)
* [Installation instructions](https://openmw.readthedocs.io/en/latest/manuals/installation/index.html)
* [Build from source](https://wiki.openmw.org/index.php?title=Development_Environment_Setup)
* [Testing the game](https://wiki.openmw.org/index.php?title=Testing)
* [How to contribute](https://wiki.openmw.org/index.php?title=Contribution_Wanted)
* [Report a bug](https://gitlab.com/OpenMW/openmw/issues) - read the [guidelines](https://wiki.openmw.org/index.php?title=Bug_Reporting_Guidelines) before submitting your first bug!
* [Known issues](https://gitlab.com/OpenMW/openmw/issues?label_name%5B%5D=Bug)

The data path
-------------

The data path tells OpenMW where to find your Morrowind files. If you run the launcher, OpenMW should be able to pick up the location of these files on its own, if both Morrowind and OpenMW are installed properly (installing Morrowind under WINE is considered a proper install).

Command line options
--------------------

    Syntax: openmw <options>
    Allowed options:
      --config arg                          additional config directories
      --replace arg                         settings where the values from the
                                            current source should replace those
                                            from lower-priority sources instead of
                                            being appended
      --user-data arg                       set user data directory (used for
                                            saves, screenshots, etc)
      --resources arg (=resources)          set resources directory
      --help                                print help message
      --version                             print version information and quit
      --data arg (=data)                    set data directories (later directories
                                            have higher priority)
      --data-local arg                      set local data directory (highest
                                            priority)
      --fallback-archive arg (=fallback-archive)
                                            set fallback BSA archives (later
                                            archives have higher priority)
      --start arg                           set initial cell
      --content arg                         content file(s): esm/esp, or
                                            omwgame/omwaddon/omwscripts
      --groundcover arg                     groundcover content file(s): esm/esp,
                                            or omwgame/omwaddon
      --no-sound [=arg(=1)] (=0)            disable all sounds
      --script-all [=arg(=1)] (=0)          compile all scripts (excluding dialogue
                                            scripts) at startup
      --script-all-dialogue [=arg(=1)] (=0) compile all dialogue scripts at startup
      --script-console [=arg(=1)] (=0)      enable console-only script
                                            functionality
      --script-run arg                      select a file containing a list of
                                            console commands that is executed on
                                            startup
      --script-warn [=arg(=1)] (=1)         handling of warnings when compiling
                                            scripts
                                            0 - ignore warnings
                                            1 - show warnings but consider script as
                                            correctly compiled anyway
                                            2 - treat warnings as errors
      --load-savegame arg                   load a save game file on game startup
                                            (specify an absolute filename or a
                                            filename relative to the current
                                            working directory)
      --skip-menu [=arg(=1)] (=0)           skip main menu on game startup
      --new-game [=arg(=1)] (=0)            run new game sequence (ignored if
                                            skip-menu=0)
      --encoding arg (=win1252)             Character encoding used in OpenMW game
                                            messages:

                                            win1250 - Central and Eastern European
                                            such as Polish, Czech, Slovak,
                                            Hungarian, Slovene, Bosnian, Croatian,
                                            Serbian (Latin script), Romanian and
                                            Albanian languages

                                            win1251 - Cyrillic alphabet such as
                                            Russian, Bulgarian, Serbian Cyrillic
                                            and other languages

                                            win1252 - Western European (Latin)
                                            alphabet, used by default
      --fallback arg                        fallback values
      --no-grab [=arg(=1)] (=0)             Don't grab mouse cursor
      --export-fonts [=arg(=1)] (=0)        Export Morrowind .fnt fonts to PNG
                                            image and XML file in current directory
      --activate-dist arg (=-1)             activation distance override
      --random-seed arg (=<impl defined>)   seed value for random number generator
