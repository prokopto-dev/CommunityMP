# CommunityMP Character Select Customization

CommunityMP uses a dedicated client-side character selector for account character slots. The server still sends a normal character-list dialog, but the client renders that dialog through the skinned selector under `mygui/characterselect`. Other list dialogs keep using the standard list popup.

While this selector is open, the client treats it as a pre-world lobby: the HUD and chat are hidden, the login slideshow/background effects continue behind the panel, and the login music can keep playing until the player chooses or creates a character. The `mygui/characterselect` layout controls the panel; the full-screen backdrop uses the same `loginBackground*`, `loginLogo`, and `loginMusic` settings documented in `communitymp-login-customization.md`.

## File Layout

Place overrides in the active resource tree using this structure:

```text
resources/vfs/mygui/characterselect/
  communitymp_character_select.layout
  communitymp_character_select.xml
  communitymp_character_select.skin.xml
  textures/
    communitymp_character_select_frame.png
    communitymp_character_portrait.png
    communitymp_character_portrait_frame.png
    communitymp_character_button_atlas.png
```

The default distribution ships the same structure under `files/mygui/characterselect`.

## Required Widgets

Custom layouts must keep these widget names and compatible types:

```xml
<Widget type="Widget" skin="" layer="Windows" position="0 0 940 500" name="_Main">
  <Widget type="EditBox" skin="MW_TextBoxEdit" name="Message"/>
  <Widget type="Button" skin="CommunityMP_CharacterSlot" name="Slot0">
    <Widget type="TextBox" skin="SandText" name="Slot0Text"/>
  </Widget>
  <Widget type="Button" skin="CommunityMP_CharacterSlot" name="Slot1">
    <Widget type="TextBox" skin="SandText" name="Slot1Text"/>
  </Widget>
  <Widget type="Button" skin="CommunityMP_CharacterSlot" name="Slot2">
    <Widget type="TextBox" skin="SandText" name="Slot2Text"/>
  </Widget>
  <Widget type="Button" skin="CommunityMP_CharacterSlot" name="Slot3">
    <Widget type="TextBox" skin="SandText" name="Slot3Text"/>
  </Widget>
  <Widget type="Button" skin="CommunityMP_CharacterSlot" name="Slot4">
    <Widget type="TextBox" skin="SandText" name="Slot4Text"/>
  </Widget>
  <Widget type="Button" skin="CommunityMP_CharacterSlot" name="Slot5">
    <Widget type="TextBox" skin="SandText" name="Slot5Text"/>
  </Widget>
  <Widget type="Button" skin="CommunityMP_CharacterButton" name="UpButton"/>
  <Widget type="Button" skin="CommunityMP_CharacterButton" name="DownButton"/>
  <Widget type="Button" skin="CommunityMP_CharacterButton" name="SelectButton"/>
  <Widget type="TextBox" skin="SandText" name="StatusText"/>
  <Widget type="ImageBox" skin="ImageBox" name="Portrait"/>
  <Widget type="ImageBox" skin="ImageBox" name="PortraitFrame"/>
  <Widget type="TextBox" skin="SandText" name="PortraitInitial"/>
  <Widget type="TextBox" skin="SandText" name="PortraitSubtitle"/>
  <Widget type="TextBox" skin="SandText" name="CharacterTitle"/>
  <Widget type="TextBox" skin="SandText" name="CharacterDetails"/>
</Widget>
```

The client will fall back to the standard compatibility list dialog if the custom character-select layout is missing or broken.

## Texture Rules

Texture paths should include the `mygui\characterselect\textures\...` prefix. A missing or wrong texture path will show as a magenta block. Use power-of-two source sizes for MyGUI textures to avoid runtime scaling warnings.

Recommended default sizes:

```text
communitymp_character_select_frame.png  1024x512
communitymp_character_portrait.png        256x256
communitymp_character_portrait_frame.png  256x256
communitymp_character_button_atlas.png    256x256
```

`Portrait` is the live render target for saved character faces when the server sends slot preview metadata. Use `communitymp_character_portrait.png` as the fallback background, and put any border or ornamentation in `PortraitFrame` so it remains visible over rendered faces. The center of `PortraitFrame` should be transparent; otherwise it can cover the rendered face.

The selector frame is separate from the chat frame. Do not bake chat tabs or the chat text input strip into `communitymp_character_select_frame.png`; keep tab-like and input-like art in the chat layout only.

## Button Skin Example

```xml
<Resource type="ResourceSkin" name="CommunityMP_CharacterSlot" size="256 32"
          texture="mygui\characterselect\textures\communitymp_character_button_atlas.png">
  <Property key="FontName" value="Russo"/>
  <Property key="TextAlign" value="Left VCenter"/>
  <BasisSkin type="MainSkin" offset="0 0 256 32" align="Stretch">
    <State name="normal" offset="0 32 256 32"/>
    <State name="highlighted" offset="0 64 256 32"/>
    <State name="normal_checked" offset="0 96 256 32"/>
  </BasisSkin>
</Resource>
```

Rows are normal buttons with separate text widgets (`Slot0Text` through `Slot5Text`) so modders can freely skin the row background while the client controls selected-row color and preview updates.

Saved character previews slowly rotate while the selector is open. New-character rows use the static portrait fallback until a character exists and the server can send preview metadata.
