package vwcn;

/**
 * v18: 0-100 km/h timing and session maxima for the cluster info box.
 *
 * Fed from the vehicle-states high-frequency callback (8-10 per second). A run is armed by standing still
 * (speed below 0.5 km/h), starts when the speed rises through 0.5 km/h and ends when it rises through
 * 100 km/h; both crossings are interpolated between callbacks, so the result is not limited to the
 * callback interval. A run is abandoned when the speed drops 3 km/h below its peak (lifted / braked) or after
 * 30 s. Maxima (power, torque, total G) cover the time since the head unit started.
 * Written by the DSI callback thread; the volatile results are read by the Dash writer thread.
 */
final class Accel {
    static final float START = 0.5f, TARGET = 100f, GIVE_UP_DROP = 3f;
    static final long GIVE_UP_MS = 30000L;

    volatile float last = Float.NaN;      // last completed 0-100 in seconds
    volatile float best = Float.NaN;      // best since the head unit started
    volatile float running = Float.NaN;   // elapsed seconds while a run is in progress
    volatile float pMax = Float.NaN, gMax = Float.NaN;
    volatile int tqMax = Integer.MIN_VALUE;

    private boolean armed, run;
    private long tStart, tPrev = -1, tStill, tFirst;
    private float vPrev, vPeak, vFirst;
    private int nMoving;

    void speed(long t, float v) {
        if (Float.isNaN(v) || v < 0f) return;
        if (v < START) {
            armed = true;
            if (run) { run = false; running = Float.NaN; }
        } else if (armed && !run && tPrev >= 0 && vPrev < START && v > vPrev) {
            // the wheel-speed signal reads 0 below a few km/h: provisional start = the 0.5 km/h crossing,
            // refined on the next sample by extrapolating the first two moving samples back to 0 km/h
            tStart = tPrev + (long) ((START - vPrev) / (v - vPrev) * (t - tPrev));
            tStill = tPrev;
            tFirst = t;
            vFirst = v;
            nMoving = 1;
            run = true;
            armed = false;
            vPeak = v;
        }
        if (run) {
            if (v >= TARGET && vPrev < TARGET && v > vPrev) {
                long tc = tPrev + (long) ((TARGET - vPrev) / (v - vPrev) * (t - tPrev));
                float r = (tc - tStart) / 1000f;
                last = r;
                if (Float.isNaN(best) || r < best) best = r;
                run = false;
                running = Float.NaN;
                Log.log("dash: 0-100 " + r + " s (best " + best + ")");
            } else if (v < vPeak - GIVE_UP_DROP || t - tStart > GIVE_UP_MS) {
                run = false;
                running = Float.NaN;
            } else {
                if (t > tFirst && nMoving++ == 1 && v > vFirst) {
                    // the true start may lie before the last 0 km/h sample (signal floor): allow up to 1 s earlier
                    long est = tFirst - (long) (vFirst * (t - tFirst) / (v - vFirst));
                    tStart = est < tStill - 1000L ? tStill - 1000L : est > tFirst ? tFirst : est;
                }
                if (v > vPeak) vPeak = v;
                running = (t - tStart) / 1000f;
            }
        }
        tPrev = t;
        vPrev = v;
    }

    /** plausibility limits keep a single bad sample from sticking as the maximum */
    void power(float kw) {
        if (!Float.isNaN(kw) && kw >= 0f && kw < 600f && (Float.isNaN(pMax) || kw > pMax)) pMax = kw;
    }

    void torque(int nm) {
        if (nm > 0 && nm < 1500 && nm > tqMax) tqMax = nm;
    }

    void g(float gx, float gy) {
        if (Float.isNaN(gx) || Float.isNaN(gy)) return;
        float g = (float) Math.sqrt(gx * gx + gy * gy);
        if (g < 2.5f && (Float.isNaN(gMax) || g > gMax)) gMax = g;
    }
}
