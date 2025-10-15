#pragma once
#include <algorithm>
#include <cmath>

/// @brief ViewportAnim — class/struct documentation.
class ViewportAnim
{
public:
    // --
    ViewportAnim();

    // Begin a smooth zoom/pan animation to focus on the range [normStart, normEnd]
    // normStart and normEnd are in [0,1] (normalized timeline positions)
    // currentZoom and currentOffset are the current view settings
    void begin(double normStart, double normEnd, float currentZoom, double currentOffset);

    // Update the zoom and offset towards the target range based on elapsed time (dt)
    void tick(double dt, float& zoom, double& offset);

    // Check if an animation is in progress
    bool isActive() const { return _active; }

private:
    double _duration = 0.25;        // animation duration in seconds
    double _t = 0.0;                // current interpolation time [0,1]
    bool   _active = false;         // whether the animation is active
    float  _startZoom = 1.0f;       // zoom at animation start
    double _startOffset = 0.0;      // offset at animation start
    float  _targetZoom = 1.0f;      // target zoom at animation end
    double _targetOffset = 0.0;     // target offset at animation end
};
