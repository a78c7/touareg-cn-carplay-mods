package vwcn;

import org.osgi.framework.BundleActivator;
import org.osgi.framework.BundleContext;

public class Activator implements BundleActivator {
    public void start(BundleContext ctx) throws Exception {
        Log.log("VWClusterNav v16 Activator.start called");
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
            Perf.start();
            Log.log("perf thread started");
        } catch (Throwable t) {
            Log.log("perf start error: " + t);
        }
    }

    public void stop(BundleContext ctx) throws Exception {
        try {
            RGGate.stop = true;
            Perf.stop = true;
            Dash.stop = true;
            Rebind.stop = true;
            Bridge.stopThread();
        } catch (Throwable t) {
            Log.log("Activator.stop error: " + t);
        }
    }
}
