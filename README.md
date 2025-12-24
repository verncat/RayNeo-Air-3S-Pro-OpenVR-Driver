Just been copied from [OpenVR driver example](../thirdparties/openvr/samples/drivers/drivers/simplehmd/README.md)

SDK: https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR

### Input and Bindings

This driver exposes multiple HMD input components so you can bind RayNeo glasses buttons to SteamVR dashboard actions:

- system/click: RayNeo system button (power/system). Toggles Dashboard by default.
- trigger/click: RayNeo Volume Up mapped to a trigger click (use for left-click in Dashboard).
- grip/click: RayNeo Volume Down mapped to a grip click (use for Back).
- application_menu/click: RayNeo Brightness mapped to application menu click (use for Home).

Pointer pose for mouse-like interaction comes from /user/head/pose/raw.

Example binding for Generic HMD (place in SteamVR resources or import via Controller Bindings UI): see resources/bindings/rayneo_generic_hmd.json.