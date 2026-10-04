// Easing curves for on-air motion. Pure maths: no libobs, no Qt.
// Every function maps 0..1 to 0..1 (some overshoot slightly) and clamps its input.
#pragma once

namespace laycue::titles {

double clamp01(double t);
double lerp(double a, double b, double t);

double easeOutCubic(double t);
double easeInCubic(double t);
double easeInOutCubic(double t);
// Fast start, long soft landing: the broadcast "arrive" curve (cubic-bezier .16,1,.3,1 look).
double easeOutExpo(double t);
double easeInExpo(double t);
// Overshoots by about 8% and settles: used for pop-in.
double easeOutBack(double t);

// The part of a longer timeline that runs from `start` to `start + length` (both 0..1 of the
// whole), as its own 0..1. Used to stagger elements inside one animation.
double stage(double t, double start, double length);

} // namespace laycue::titles
