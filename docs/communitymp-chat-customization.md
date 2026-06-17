# CommunityMP Chat Customization

CommunityMP uses the built-in chat window unless a local chat override is present. The preferred override folder is:

```text
resources\vfs\mygui\chat\
```

The active override files are:

- `communitymp_chat.layout`: chat window layout.
- `communitymp_chat.xml`: optional MyGUI resource list loaded before the layout.
- `communitymp_chat.skin.xml`: optional custom skins used by the layout.

Chat-only image textures should live beside the chat layout stack under a `textures` subfolder:

```text
resources\vfs\mygui\chat\textures\
```

This keeps chat layout XML, skins, and chat-only images together. Only shared images used by more than chat should go in the top-level texture folders.

For a local RelWithDebInfo test build, those folders are usually:

```text
<build>\RelWithDebInfo\resources\vfs\mygui\chat\
<build>\RelWithDebInfo\resources\vfs\mygui\chat\textures\
```

## Official Default

The official default layout loads from:

```text
resources\vfs\mygui\chat\communitymp_chat.layout
resources\vfs\mygui\chat\communitymp_chat.xml
resources\vfs\mygui\chat\communitymp_chat.skin.xml
```

A copy for modders to clone is included at:

```text
resources\vfs\mygui\chat\examples\default\
```

To start a custom skin from the official default, copy these three files from `examples\default` into `resources\vfs\mygui\chat` and edit the copies:

- `communitymp_chat.layout`
- `communitymp_chat.xml`
- `communitymp_chat.skin.xml`

The official default art lives under `resources\vfs\mygui\chat\textures`:

- `communitymp_chat_frame.png`: full chat frame, background panel, and input slot.
- `communitymp_chat_tab_atlas.png`: button states for inactive, hovered, pressed, and selected chat tabs.

The same source files are kept in the repository under:

```text
files\mygui\chat\
files\mygui\chat\examples\default\
files\mygui\chat\textures\
```

The example uses a frameless root widget, a full-frame image, and CommunityMP-named tab/input skins. This keeps the stock OpenMW window border out of the default chat skin.

## Required Widgets

Custom layouts can change positions, dimensions, skins, colors, fonts, and artwork, but they must keep these widget names:

- `_Main`
- `edit_Command`
- `list_History`
- `tab_All`
- `tab_Server`
- `tab_Global`
- `tab_Local`
- `tab_Private`

If a custom layout is malformed or misses required widgets, the client disables the override for that session and falls back to the built-in chat layout.

## Resource List

Use `communitymp_chat.xml` when the layout references custom skins or fonts:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<MyGUI>
    <MyGUI type="List">
        <List file="chat/communitymp_chat.skin.xml"/>
    </MyGUI>
</MyGUI>
```

Paths in MyGUI resource lists are relative to `resources\vfs\mygui`.

Texture properties such as `ImageTexture` are VFS-root relative. For chat-only artwork, use `mygui\chat\textures\...`.
Custom skin texture attributes should use the same VFS-root path style for chat subfolder textures:

```xml
<Resource type="ResourceSkin" name="MyChatTab" size="64 18" texture="mygui\chat\textures\my_chat_tabs.png">
  ...
</Resource>
```

## Tab Styling

The client updates channel tab captions when the selected chat channel changes. A custom layout can control those updates with `UserString` values on `_Main`, or override individual tabs by placing the same key on `tab_All`, `tab_Server`, `tab_Global`, `tab_Local`, or `tab_Private`.

Supported tab keys:

- `CommunityMP_TabFont`: font name used for tab captions.
- `CommunityMP_TabNormalColour`: text color for inactive tabs, such as `0.72 0.68 0.58`.
- `CommunityMP_TabSelectedColour`: text color for the active tab.
- `CommunityMP_TabSelectedPrefix`: text added before the active tab caption. The built-in default is `[`.
- `CommunityMP_TabSelectedSuffix`: text added after the active tab caption. The built-in default is `]`.
- `CommunityMP_TabUseSelectedState`: set to `true` to call the button skin's checked states for the active tab.
- `CommunityMP_TabApplyTextColour`: set to `false` when the custom button skin should control all text colors through its state definitions.
- `CommunityMP_TabCaption`: per-tab replacement caption.
- `CommunityMP_TabSelectedCaption`: per-tab replacement selected caption.

Example tab configuration:

```xml
<UserString key="CommunityMP_TabFont" value="Russo"/>
<UserString key="CommunityMP_TabNormalColour" value="0.72 0.68 0.58"/>
<UserString key="CommunityMP_TabSelectedColour" value="1 0.82 0.35"/>
<UserString key="CommunityMP_TabSelectedPrefix" value=""/>
<UserString key="CommunityMP_TabSelectedSuffix" value=""/>
<UserString key="CommunityMP_TabUseSelectedState" value="true"/>
<UserString key="CommunityMP_TabApplyTextColour" value="false"/>

<Widget type="Button" skin="CommunityMP_ChatTab" position="30 25 42 18" align="Left Top" name="tab_All">
  <UserString key="CommunityMP_TabCaption" value="All"/>
</Widget>
```

When a tab skin uses texture states, set `CommunityMP_TabApplyTextColour` to `false` and define all text colors in the skin states. That lets normal, hovered, pressed, selected, and selected-hovered states stay visually consistent with the tab artwork.

## Full Frame Images

Use a frameless root widget when the image already contains the complete chat frame. This avoids drawing the stock window border behind custom artwork:

```xml
<Widget type="Widget" skin="" position="0 0 500 335" layer="Windows" name="_Main">
  <Property key="Visible" value="false"/>

  <Widget type="ImageBox" skin="ImageBox" position_real="0 0 1 1" align="Stretch" name="chat_Background">
    <Property key="ImageTexture" value="mygui\chat\textures\my_chat_frame.png"/>
    <Property key="NeedMouse" value="false"/>
  </Widget>

  ...
</Widget>
```

Use PNG or DDS textures. PNG files should be truecolor RGB or RGBA, not palette-indexed PNGs. WebP is not a safe runtime texture format for this path.

The chat window size still comes from the client chat settings. Match the configured size to the artwork aspect ratio to avoid stretching:

```ini
[Chat]
w = 500
h = 335
```

If the chat area turns bright pink or magenta, the texture did not load. Check that the image is inside `resources\vfs\mygui\chat\textures`, the `ImageTexture` path starts with `mygui\chat\textures\`, and the texture format is supported.
