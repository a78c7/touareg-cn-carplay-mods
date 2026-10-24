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
    /** v19.2: wrap the BAP NavigationService too (steering-wheel zoom for the CarPlay cluster map), see NavZoom */
    static void installZoom() {
        try {
            if (new java.io.File("/mnt/app/eso/bin/apps/cluster/zoom_off").exists()) { Log.log("rggate: zoom wrapper off"); return; }
            Field f = APIFactory.class.getDeclaredField("navigationService");
            f.setAccessible(true);
            Object cur = APIFactory.getAPIFactory().getNavigationService();   // creates it if stock has not yet
            if (cur instanceof NavZoom) return;
            if (cur instanceof de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService && f.get(null) == cur) {
                f.set(null, new NavZoom((de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService) cur));
                Log.log("rggate: zoom wrapper installed around " + cur.getClass().getName());
            } else Log.log("rggate: navigationService not replaceable: " + cur);
        } catch (Throwable t) {
            Log.log("rggate zoom error: " + t);
        }
    }

    public void run() {
        try {
            Thread.sleep(30000);
            Field f = APIFactory.class.getDeclaredField("carPlayService");
            f.setAccessible(true);
            for (int i = 0; i < 120 && !stop; i++) {
                Object cur = f.get(null);
                if (cur instanceof NoRGCarPlay) { Log.log("rggate: already installed"); installZoom(); return; }
                if (cur instanceof CarPlayService) {
                    f.set(null, new NoRGCarPlay((CarPlayService) cur));
                    Log.log("rggate: installed wrapper around " + cur.getClass().getName());
                    installZoom();
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
