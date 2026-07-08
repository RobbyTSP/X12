# X12: Experimental Legacy-Free Display Server

This is an experimental, trimmed-down display server project ("X12") based on the classic X11 (Xorg) codebase. 

The goal of this project is to eliminate decades of legacy bloat, obsolete APIs, and insecure subsystems, modernizing the server to act as a lightweight local-only windowing engine.

⚠️ **WARNING & DISCLAIMER:** 
This project is **highly experimental** and a work in progress! Since it is an active experiment, **severe bugs, crashes, loader issues, and unexpected errors will definitely occur**. Use it at your own risk.

## Codebase Size & Analysis (Reduction Metrics)

🐾 **X12 Server Footprint (xserver):**
* **Source Files (.c):** 513 files (reduced from ~1,850 in legacy X11)
* **Header Files (.h):** 305 files (reduced from ~1,420 in legacy X11)
* **C Code Volume:** 300,979 source lines of code
* **Header Volume:** 50,031 header lines of code

🐾 **X12 Client Library (libX11):**
* **Source Files (.c):** 370 files
* **Header Files (.h):** 89 files
* **C Code Volume:** 79,795 source lines of code
* **Header Volume:** 49,379 header lines of code

🐾 **X12 Protocols (xorgproto):**
* **Header Files (.h):** 156 files
* **Header Volume:** 42,377 header lines of code

## Legacy Compatibility: XOTXN

To keep the X12 codebase clean and modern, all backward-compatibility layers for legacy client protocols and old hardware devices have been split out into a separate, modular project.

* **Modular Compatibility:** [XOTXN](https://github.com/RobbyTSP/XOTXN) (Xorg Old translate Xorg New) - An out-of-process translation bridge written in Rust.

## Author & Credits

This project was developed entirely by **Robby** (sole creator/developer).

## What "Garbage" Was Removed? (Legacy Cleanup)

To transform X11 into X12, we systematically stripped out and blocked the following obsolete, legacy subsystems:

### 1. Legacy 2D Core Drawing APIs (Obsolete Rendering)
All core server-side 2D drawing calls have been routed to `ProcBadRequest`. Modern clients render using client-side libraries (like Cairo, Pango, or Skia) and draw using GPU acceleration (OpenGL/Vulkan). The following drawing requests are completely blocked:
* `PolyPoint`, `PolyLine`, `PolySegment`, `PolyRectangle`, `PolyArc`
* `FillPoly`, `PolyFillRectangle`, `PolyFillArc`
* `PutImage`, `GetImage`, `CopyArea`, `CopyPlane`, `ClearToBackground`

### 2. Legacy Server-Side Font Rendering (Core Fonts)
Historically, the X-server loaded and rendered raster/vector fonts. Today, fonts are rendered by client-side libraries. All server-side font operations are disabled:
* `OpenFont`, `CloseFont`, `QueryFont`, `QueryTextExtents`
* `ListFonts`, `ListFontsWithInfo`, `SetFontPath`, `GetFontPath`

### 3. Legacy Colormaps
Hardware colormaps (used for 8-bit / indexed color displays) are obsolete. TrueColor (24-bit/32-bit) is now standard. All old palette allocation calls are blocked:
* `AllocColor`, `FreeColors`, `StoreColors`, etc.

### 4. Bloated Subsystems & Nested Servers
The following directories and modules were physically deleted or disabled in the build configuration:
* **GLX:** Disabled (`-Dglx=false`) and the source directory deleted.
* **Xwayland:** Completely removed (`-Dxwayland=false`) and source directory deleted.
* **Nested X Servers:** `Xnest`, `Xvfb`, and `Xephyr` directories have been physically deleted.
* **Input Test Driver:** `inputtest` driver disabled and deleted.
* **Platform-Specific Ports:** `xquartz` (macOS) and `xwin` (Windows) directories have been physically deleted, and their build options removed.

### 5. Network TCP/IPv6 Connections
To prevent remote exploits, network TCP listeners are completely disabled (`TCPCONN` and `IPv6` compiled out). The server now communicates exclusively via secure, local Unix domain sockets.

### 6. Remote Login & Network Protocols (XDMCP / XDM-AUTH-1)
All support for remote logins via the X Display Manager Control Protocol (XDMCP) and XDM-AUTH-1 authentication has been completely stripped. No remote authentication modules or network handlers are compiled, restricting X12 purely to secure local access.

### 7. Client-Side & Protocol De-bloating (Phase 3)
* **xorgproto:** Completely stripped pkg-config and header configurations for obsolete extension protocols, including `applewmproto`, `dmxproto`, `glproto`, `xf86bigfontproto`, `xf86dgaproto`, `xf86driproto`, `xf86vidmodeproto`, and `xwaylandproto`.
* **libX11:** Disabled legacy subsystems client-side by defaulting their compile options to disabled:
  * **XCMS** (Color Management System)
  * **XLOCALE** (Legacy input method / local internationalization)
  * **XF86BigFont** (Legacy font scaling extension helper)

### 8. Extension Purging & Logging Modernization (Phase 4)
* **Extension Purge:** Completely removed the following obsolete/insecure extensions:
  * `MIT-SCREEN-SAVER` (Legacy screen saver protocol)
  * `XSecurity` / `XSELINUX` (Insecure legacy server-side access control models)
  * `XVideo` / `XvMC` (Obsolete hardware video overlays and motion compensation)
  * `XF86BigFont` (Obsolete huge font scaling protocol)
  * `XF86VidMode` (Obsolete display mode manipulation)
* **Logging Modernization:** Configured the X server to log directly to `stderr` by default instead of writing files to `/var/log/` or creating runtime log files.
* **Dependency Auditing:** Audited and verified that legacy Xmu/Xaw dependencies are fully pruned/stubbed and not linked.

### 9. OS Support Consolidation, Acceleration Pruning, and Physical Subsystem Purge (Phase 5)
* **OS-Support Consolidation (Linux-Only):** Consolidated OS support in `xserver` to Linux-only, completely purging BSD (`bsd/`), Solaris (`solaris/`), and stub (`stub/`) directories.
* **Acceleration Pruning:** Pruned legacy DDX acceleration architectures from `xfree86` DDX, physically deleting `exa/` and `shadowfb/` directories, making `glamor` / `modesetting` the sole driver acceleration system.
* **libX11 Subsystem Purge:** Physically deleted obsolete and disabled subsystems in client libraries, removing `xcms/`, `xlibi18n/`, `modules/`, `nls/`, and `specs/` directories from `libx11/`.
* **Meson Build Port:** Replaced the entire legacy Autotools build system of `libx11` with a modern, fast Meson build configuration, physically deleting all old Makefile.am/in, configure.ac, and build helper scripts.

## Simplified Configuration File (`x12.conf`)

X12 introduces a simple, human-readable configuration file format to manage display features directly (such as V-Sync, TearFree, and Variable Refresh Rate / G-Sync / FreeSync), avoiding the need for bloated legacy `xorg.conf` files.

The server loads configuration from `~/.config/x12.conf` (user-specific) with a fallback to `/etc/x12.conf` (system-wide). It also supports specifying a custom config path via the `X12_CONFIG_PATH` environment variable (useful for testing).

Example `x12.conf`:
```ini
# Enable or disable display synchronization and features
VSync = True             # Maps to modesetting PageFlip
TearFree = True          # Double-buffered synchronization
GSync = True             # Maps to modesetting VariableRefresh (VRR)
DoubleShadow = True
AccelMethod = glamor     # Acceleration backend
```

Supported Option Aliases:
* `VSync` -> `PageFlip`
* `GSync` / `FreeSync` / `VRR` -> `VariableRefresh`

## License

This project is licensed under the **GNU Affero General Public License v3 (AGPLv3)**. See the `LICENSE` file for the full license text.
