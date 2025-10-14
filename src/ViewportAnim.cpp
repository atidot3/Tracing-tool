#include "ViewportAnim.hpp"

ViewportAnim::ViewportAnim()
{

}

void ViewportAnim::begin(double normStart, double normEnd, float currentZoom, double currentOffset)
{
    // Clamp input range to [0,1]
    normStart = std::clamp(normStart, 0.0, 1.0);
    normEnd = std::clamp(normEnd, 0.0, 1.0);
    if (normEnd <= normStart)
    {
        // ensure a minimal range (avoid zero span)
        normEnd = normStart + 1e-6;
    }
    double span = normEnd - normStart;
    const double minSpan = 0.00005;
    if (span < minSpan)
    {
        // If range is extremely small, enforce a minimum span to avoid excessive zoom
        double mid = (normStart + normEnd) * 0.5;
        normStart = std::max(0.0, mid - minSpan * 0.5);
        normEnd = std::min(1.0, mid + minSpan * 0.5);
        span = normEnd - normStart;
    }
    // Set up animation targets based on current view
    _startZoom = currentZoom;
    _startOffset = currentOffset;
    _targetZoom = float(1.0 / span);
    _targetOffset = normStart;
    _t = 0.0;
    _active = true;
}

void ViewportAnim::tick(double dt, float& zoom, double& offset)
{
    if (!_active) return;

    // Advance time normalized to [0,1] based on duration
    _t = std::min(1.0, _t + dt / _duration);
    // Ease-out cubic interpolation (slow at start, fast at end)
    double u = 1.0 - _t;
    double w = 1.0 - u * u * u;
    // Interpolate zoom and offset towards targets
    zoom = float(_startZoom + (_targetZoom - _startZoom) * w);
    offset = _startOffset + (_targetOffset - _startOffset) * w;
    // Clamp offset to valid range [0, 1 - 1/zoom] to avoid overscroll
    offset = std::clamp(offset, 0.0, std::max(0.0, 1.0 - 1.0 / double(zoom)));

    // End animation when done
    if (_t >= 1.0)
        _active = false;
}
