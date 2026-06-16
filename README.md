# X12: Experimental Legacy-Free Display Server

This is an experimental, trimmed-down display server project ("X12") based on the classic X11 (Xorg) codebase. 

The goal of this project is to eliminate legacy subsystems and bloated X11 extension layers (such as 2D core drawing APIs, legacy colormaps, core fonts, and Xwayland) while maintaining compatibility for modern clients running via Unix sockets.

⚠️ **WARNING & DISCLAIMER:** 
This project is **highly experimental** and a work in progress! Since it is an active experiment, **severe bugs, crashes, loader issues, and unexpected errors will definitely occur**. Use it at your own risk.

## Author & Credits

This project was developed entirely by **Robby** (sole creator/developer).

## Features & Changes

- **Legacy API Removal:** All core 2D drawing (`PolyLine`, `FillPoly`, `CopyArea`), core font (`OpenFont`), and colormap requests are routed directly to `ProcBadRequest` or safe stubs.
- **Extreme Reduction:** GLX, Xwayland, Xnest, Xvfb, Xephyr, XVideo (Xv), screensavers, and input-testing drivers have been completely compiled out and deleted.
- **Local Only:** Network TCP listening has been completely removed. Communication is restricted exclusively to local Unix domain sockets.

## License

This project is licensed under the **GNU Affero General Public License v3 (AGPLv3)**. See the `LICENSE` file for the full license text.
