package vwcn;

import de.vw.mib.bap.mqbab2.common.api.APIFactory;
import de.vw.mib.bap.mqbab2.common.api.carplay.CarPlayService;
import java.lang.reflect.Field;

/** Replaces APIFactory.carPlayService (private static) with NoRGCarPlay once stock has created it. */
public final class RGGate implements Runnable {
    static volatile boolean stop;
    public static void start() {
        Thread t = new Thread(new RGGate(), "vwcn-rggate");
        t.setDaemon(true);
        t.start();
    }
    public void run() {
        try {
            Thread.sleep(30000);
            Field f = APIFactory.class.getDeclaredField("carPlayService");
            f.setAccessible(true);
            for (int i = 0; i < 120 && !stop; i++) {
                Object cur = f.get(null);
                if (cur instanceof NoRGCarPlay) { Log.log("rggate: already installed"); return; }
                if (cur instanceof CarPlayService) {
                    f.set(null, new NoRGCarPlay((CarPlayService) cur));
                    Log.log("rggate: installed wrapper around " + cur.getClass().getName());
                    return;
                }
                Thread.sleep(5000);
            }
            Log.log("rggate: carPlayService never created, not installed");
        } catch (Throwable t) {
            Log.log("rggate error: " + t);
        }
    }
}
