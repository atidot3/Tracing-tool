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
        normEnd = std::min(1.0, normStart + 1e-15);
    }
    double span = normEnd - normStart;
    // Autorise un zoom quasi "infini" : min span normalis trs faible => zoom max ~ 1e12
    constexpr double kMinSpanN = 1e-12;
    if (span < kMinSpanN)
    {
        const double mid = (normStart + normEnd) * 0.5;
        normStart = std::max(0.0, mid - kMinSpanN * 0.5);
        normEnd = std::min(1.0, mid + kMinSpanN * 0.5);
        span = normEnd - normStart;
    }
    // Set up animation targets based on current view
    _startZoom = currentZoom;
    _startOffset = currentOffset;
    _targetZoom = float(1.0 / std::max(span, 1e-15));
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
