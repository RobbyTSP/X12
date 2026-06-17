# X12: Experimental Legacy-Free Display Server

This is an experimental, trimmed-down display server project ("X12") based on the classic X11 (Xorg) codebase. 

The goal of this project is to eliminate decades of legacy bloat, obsolete APIs, and insecure subsystems, modernizing the server to act as a lightweight local-only windowing engine.

⚠️ **WARNING & DISCLAIMER:** 
This project is **highly experimental** and a work in progress! Since it is an active experiment, **severe bugs, crashes, loader issues, and unexpected errors will definitely occur**. Use it at your own risk.

## Codebase Size & Analysis (Reduction Metrics)

🐾 **X12 Footprint Analysis:**
* **Source Files (.c):** 548 files (reduced from ~1,850 in legacy X11)
* **Header Files (.h):** 320 files (reduced from ~1,420 in legacy X11)
* **C Code Volume:** 317,170 source lines of code
* **Header Volume:** 51,802 header lines of code

## Legacy Compatibility: XOTXN

To keep the X12 codebase clean and modern, all backward-compatibility layers for legacy client protocols and old hardware devices have been split out into a separate, modular project.

* **Coming Soon:** [XOTXN](https://github.com/RobbyTSP/XOTXN) (Xorg Old translate Xorg New) - An out-of-process translation bridge written in Rust and C.

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

### 4. XVideo Extension (Xv / Video Scaling)
obsolete hardware-overlay video scaling has been disabled. The server configuration disables Xv (`-Dxv=false`), and we have commented out the `xf86XVScreenInit` calls inside the `modesetting` driver to prevent runtime loading crashes.

### 5. Bloated Subsystems & Nested Servers
The following directories and modules were physically deleted or disabled in the build configuration:
* **GLX:** Disabled (`-Dglx=false`) and the source directory deleted.
* **Xwayland:** Completely removed (`-Dxwayland=false`) and source directory deleted.
* **Nested X Servers:** `Xnest`, `Xvfb`, and `Xephyr` directories have been physically deleted.
* **Input Test Driver:** `inputtest` driver disabled and deleted.
* **Platform-Specific Ports:** `xquartz` (macOS) and `xwin` (Windows) directories have been physically deleted, and their build options removed.

### 6. Network TCP/IPv6 Connections
To prevent remote exploits, network TCP listeners are completely disabled (`TCPCONN` and `IPv6` compiled out). The server now communicates exclusively via secure, local Unix domain sockets.

### 7. Remote Login & Network Protocols (XDMCP / XDM-AUTH-1)
All support for remote logins via the X Display Manager Control Protocol (XDMCP) and XDM-AUTH-1 authentication has been completely stripped. No remote authentication modules or network handlers are compiled, restricting X12 purely to secure local access.

## License

This project is licensed under the **GNU Affero General Public License v3 (AGPLv3)**. See the `LICENSE` file for the full license text.
