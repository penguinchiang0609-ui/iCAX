# Orocos KDL

Pinned upstream: https://github.com/orocos/orocos_kinematics_dynamics
Tag v1.5.1, commit db25b7e480e068df068232064f2443b8d52a83c7.
Source ZIP SHA256: A6C265B796F543B5548E0089059A1D9C987EE6CD01EA56CBCDA408637F793462.
License: LGPL-2.1-or-later; upstream COPYING is included.

Upstream algorithm sources are unchanged. The local CMake wrapper builds a replaceable Windows DLL using automatic symbol export, with the same CRT configuration as the caller. A generated consumer header marks the public epsilon data symbol as dllimport; upstream source files remain unchanged. Eigen 3.4.0 is reused from ../coal. No ROS or Python dependency.

Build: ./prepare-kdl.ps1 -Configuration Debug (or Release).
The source archive and this wrapper must be distributed with the DLL notices.
