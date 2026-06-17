# CommunityMP Login Customization

CommunityMP uses a dedicated client-side login panel before connecting to a server. The panel is loaded from `mygui/login` when those resources are present, and it falls back to the older compatibility layout if the custom layout is missing or broken.

## File Layout

Place overrides in the active resource tree using this structure:

```text
resources/vfs/mygui/login/
  communitymp_login.layout
  communitymp_login.skin.xml
  communitymp_login.xml
  textures/
    communitymp-ashlands-hero.jpg
    communitymp-causeway.jpg
    communitymp-gathering.jpg
    communitymp-logo.png
    communitymp-server-hall.jpg
    communitymp_login_atmosphere.png
    communitymp_login_button_atlas.png
    communitymp_login_panel.png
```

The default distribution ships the same structure under `files/mygui/login`.

## Background Image

The full-screen login background is controlled by the client setting:

```ini
loginBackground = mygui\login\textures\communitymp-causeway.jpg
```

The full-screen background can also run as a slow slideshow:

```ini
loginBackgroundSlides = mygui\login\textures\communitymp-causeway.jpg;mygui\login\textures\communitymp-gathering.jpg;mygui\login\textures\communitymp-server-hall.jpg;mygui\login\textures\communitymp-ashlands-hero.jpg
loginSlideSeconds = 6
```

To replace it, copy supported images into the resource tree and update `loginBackgroundSlides` in `communitymp-client.cfg` or the default config used for a packaged build. Keep paths relative to the VFS texture root style used by MyGUI, for example:

```ini
loginBackgroundSlides = mygui\login\textures\my-server-login-a.jpg;mygui\login\textures\my-server-login-b.jpg
```

If `loginBackgroundSlides` is missing or empty, CommunityMP uses the bundled slideshow so older configs still get the animated front-end. Set `loginBackgroundSlides = single`, `static`, or `off` to use only `loginBackground`. Wide images work best because the login screen stretches behind the whole viewport. If a texture fails to load, the client logs a warning and keeps the login panel usable without the background.

## Screen Effects

The default login menu uses an animation pass shared with the pre-character lobby: slow pan/zoom, visible crossfades between slides, and a translucent atmosphere overlay inspired by the CommunityMP website hero shade.

```ini
loginBackgroundEffects = true
loginAtmosphereOverlay = mygui\login\textures\communitymp_login_atmosphere.png
```

Set `loginBackgroundEffects = false` for a static background. Set `loginAtmosphereOverlay` to another VFS texture path to reskin the vignette/scanline treatment, or leave it empty to use the slide animation without the overlay.

## Login Logo

The login screen can display a top-center logo over the background:

```ini
loginLogo = mygui\login\textures\communitymp-logo.png
```

Set `loginLogo` to another VFS texture path to replace it, or leave it empty to hide the banner logo. PNG is preferred for packaged builds because the standard runtime includes PNG texture support.

## Login Skin

The login menu has its own skin file at `mygui/login/communitymp_login.skin.xml`. Keep login-specific button and toggle skins there instead of reusing the character-selection skins. The default layout uses:

```xml
<Widget type="Button" skin="CommunityMP_LoginToggle" name="ButtonRemember"/>
<Widget type="Button" skin="CommunityMP_LoginButton" name="ButtonCancel"/>
<Widget type="Button" skin="CommunityMP_LoginButton" name="ButtonConnect"/>
```

The default panel is intentionally translucent so the full-screen background still reads through the login surface.

## Login Music

The login screen can play a dedicated music track while the account prompt is open:

```ini
loginMusic = music/communitymp/nightinthedesertmix.ogg
```

Set `loginMusic` to another VFS music path to replace it, or leave the value empty to keep the login prompt silent. On successful sign-in, the track can carry into the character-selection lobby; the lobby stops it when the player chooses or creates a character so world and server-directed music can take over normally.

## Required Widgets

Custom layouts must keep these widget names and compatible types:

```xml
<Widget type="Widget" skin="" layer="Windows" name="_Main">
  <Widget type="TextBox" name="ServerText"/>
  <Widget type="EditBox" name="EditLogin"/>
  <Widget type="EditBox" name="EditPassword"/>
  <Widget type="EditBox" name="EditServerPassword"/>
  <Widget type="Button" name="ButtonRemember"/>
  <Widget type="TextBox" name="ErrorText"/>
  <Widget type="Button" name="ButtonCancel"/>
  <Widget type="Button" name="ButtonConnect"/>
</Widget>
```

`EditLogin` is the CommunityMP account username. `EditPassword` is the account password field. `EditServerPassword` is only for server join passwords. Keep those separate so account authentication and server access continue to work correctly.

## Remember Me

The remember-me checkbox stores the account name and the derived account password hash when enabled:

```ini
rememberAccount = true
accountPasswordHash = ...
```

The raw account password is not written to the client config. Turning remember-me off clears the stored hash the next time login settings are saved.
