package vwcn;

import java.io.File;
import org.osgi.framework.BundleActivator;
import org.osgi.framework.BundleContext;

public class Activator implements BundleActivator {
    private static boolean isSafeMode() {
        return new File("/mnt/app/eso/bin/apps/cluster/safe_mode").exists()
            || new File("/tmp/vw_hmi_safe_mode").exists();
    }

    public void start(BundleContext ctx) throws Exception {
        if (isSafeMode()) {
            Log.log("VWClusterNav v22.01: safe_mode active, bypassing all HMI hooks");
            return;
        }
        Log.log("VWClusterNav v22.01 Activator.start called");
        try {
            Bridge.startThread();
            Log.log("bridge thread started");
            RGGate.start();
            Log.log("rggate thread started");
            Rebind.start(ctx);
        } catch (Throwable t) {
            Log.log("Activator.start error: " + t);
        }
        try {
            Dash.start();
            Log.log("dash thread started");
        } catch (Throwable t) {
            Log.log("dash start error: " + t);
        }
        try {
            KeyProbe.start();
            Log.log("keyprobe thread started");
        } catch (Throwable t) {
            Log.log("keyprobe start error: " + t);
        }
        try {
            SiriShort.start();
            Log.log("siri short-press thread started");
        } catch (Throwable t) {
            Log.log("siri short-press start error: " + t);
        }
    }

    public void stop(BundleContext ctx) throws Exception {
        try {
            RGGate.stop = true;
            Dash.stop = true;
            Rebind.stop = true;
            KeyProbe.stop = true;
            SiriShort.stop = true;
            Bridge.stopThread();
        } catch (Throwable t) {
            Log.log("Activator.stop error: " + t);
        }
    }
}
