# CommunityMP licensing notes

This file is a project-maintainer summary, not legal advice.

## Project identity

The project-facing name is CommunityMP. The names OpenMW and TES3MP are used
only for historical attribution, compatibility descriptions, preserved legal
notices, and API/protocol compatibility where changing names would break users
or hide source provenance.

Older development history and artifacts may refer to this fork with
TES3MP-derived naming, including TES3MP Refresh. Those names are historical
references only; the current project-facing name is CommunityMP.

A quick public web search on June 12, 2026 did not show an obvious software or
game project using the exact name "CommunityMP". This is not a trademark
clearance opinion. Anyone preparing a formal release should still check relevant
trademark databases and distribution platforms.

## Combined licensing structure

CommunityMP is a combined work with several copyright origins:

* OpenMW-origin material remains under its existing GPLv3-family notices.
* TES3MP-origin material remains under GPLv3 with the TES3MP section 7
  additional terms preserved to the extent they are applicable and valid.
* CommunityMP original material authored by or assigned to Alex Cooper and
  OpenACAI Inc. is Copyright (c) 2026 Alex Cooper and OpenACAI Inc. and is
  licensed under AGPLv3-or-later. Other CommunityMP contributors retain
  copyright in their copyrightable contributions unless a file-level notice or
  written assignment says otherwise.

The repository does not modify the text of the GNU GPLv3 or GNU AGPLv3 license
documents. It records project-specific notices after the GPLv3 text and includes
the official AGPLv3 text in `LICENSES/AGPL-3.0.txt`.
Preserved upstream copyright and attribution notices are summarized in
`UPSTREAM_NOTICES.md`.

## GPLv3 and AGPLv3 compatibility

The Free Software Foundation FAQ says GPLv3-covered modules and AGPLv3-covered
modules may be linked or combined with each other. The FSF AGPLv3 guidance also
summarizes GPLv3 section 13 as allowing a GPLv3 covered work to be combined with
an AGPLv3-covered work, while GPLv3 continues to apply to the GPL-covered part
and AGPLv3 section 13 applies to the combined work.

Relevant sources:

* <https://www.gnu.org/licenses/gpl-faq.html#AGPLGPL>
* <https://www.fsf.org/bulletin/2021/fall/the-fundamentals-of-the-agplv3>
* <https://www.gnu.org/licenses/license-list.html#AGPLv3.0>

The practical result is:

* We cannot unilaterally relicense upstream OpenMW or TES3MP code as AGPLv3.
* We can license CommunityMP-owned new code as AGPLv3-or-later.
* We can convey a combined GPLv3/AGPLv3 work under the compatibility bridge in
  GPLv3 section 13 and AGPLv3 section 13.
* Only the relevant copyright holders can add exceptions or additional
  permissions to material they own.

## Private server Lua

CommunityMP grants an additional permission for independently authored server
Lua scripts, configuration, gameplay rules, quests, moderation tools, economy
systems, administration code, and similar server-side content that merely use
the documented TES3MP/OpenMW-MP server scripting interfaces. Those separate
server materials are not treated as CommunityMP Corresponding Source solely
because a CommunityMP server loads them at runtime.

This permission does not cover material that copies, modifies, translates,
embeds, or is otherwise derived from OpenMW-origin, TES3MP-origin, CommunityMP
original, or third-party covered source code.

## Bundled media attribution

CommunityMP bundles `music/communitymp/nightinthedesertmix.ogg` as login-screen
music. The track is "Night in the desert remixed (Tausdei vs Hitctrl)" from
OpenGameArt, uploaded by glitchart under CC-BY 3.0. The packaged VFS includes
`music/communitymp/nightinthedesertmix.CREDITS.txt` with the source URL and
component-track credits for Tausdei and Hitctrl.
