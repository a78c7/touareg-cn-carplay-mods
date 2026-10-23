package vwcn;

import de.vw.mib.graphics.display.DisplayManager;
import java.io.File;
import org.osgi.framework.BundleContext;
import org.osgi.framework.ServiceReference;

/** When /tmp/vwcn_rebind changes, re-attach displayable 33 (same effect as pressing VIEW on the cluster):
 *  the DisplayManager otherwise keeps showing the last frame of the CarPlay cluster window after it is gone. */
public final class Rebind implements Runnable {
    static volatile boolean stop;
    static BundleContext bctx;
    static final int CLUSTER_DISPLAY = 4;
    static final int DISP = 33;
    public static void start(BundleContext c) {
        bctx = c;
        Thread t = new Thread(new Rebind(), "vwcn-rebind");
        t.setDaemon(true);
        t.start();
    }
    static DisplayManager dm() { return de.vw.mib.widgets.internal.ServiceManager.displayManager; }
    static String list(int[] a) {
        if (a == null) return "null";
        StringBuffer b = new StringBuffer("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(','); b.append(a[i]); }
        return b.append(']').toString();
    }
    /** sequence from /tmp/vwcn_rebind: groups separated by ';', ids by ',' (empty group = []).
     *  Anything without digits/';' (e.g. "1\n" from clusterd) uses the default DEFAULT_SEQ. */
    static String DEFAULT_SEQ = "33;vis1;rate10;st";
    static de.vw.mib.asl.api.navigation.map.instrumentcluster.MapViewerClusterService mvc;
    /** while set (CarPlay cluster stream live): re-assert this displayable list on the cluster every HOLD_MS,
     *  so native context switches (VIEW key, junction view) cannot hide the CarPlay layer. */
    static volatile int[] hold;
    static volatile de.vw.mib.display.DisplayManagementService holdSvc;
    static final int HOLD_MS = 3000;
    static String readSeq(File f) {
        try {
            java.io.FileInputStream in = new java.io.FileInputStream(f);
            byte[] b = new byte[128]; int n = in.read(b); in.close();
            String s = n > 0 ? new String(b, 0, n).trim() : "";
            if (s.length() == 0 || s.equals("1")) return DEFAULT_SEQ;
            return s;
        } catch (Throwable t) { return DEFAULT_SEQ; }
    }
    static int[] parse(String g) {
        g = g.trim();
        if (g.length() == 0) return new int[0];
        java.util.StringTokenizer t = new java.util.StringTokenizer(g, ",");
        int[] r = new int[t.countTokens()];
        for (int i = 0; i < r.length; i++) r[i] = Integer.parseInt(t.nextToken().trim());
        return r;
    }
    static boolean rebind(String seq) {
        try {
            if (bctx == null) { Log.log("rebind: no BundleContext"); return false; }
            ServiceReference r = bctx.getServiceReference("de.vw.mib.display.DisplayManagementService");
            if (r == null) { Log.log("rebind: DisplayManagementService not found"); return false; }
            de.vw.mib.display.DisplayManagementService s = (de.vw.mib.display.DisplayManagementService) bctx.getService(r);
            java.util.Vector groups = new java.util.Vector();
            int p = 0;
            while (true) { int q = seq.indexOf(';', p); if (q < 0) { groups.addElement(seq.substring(p)); break; } groups.addElement(seq.substring(p, q)); p = q + 1; }
            for (int i = 0; i < groups.size(); i++) {
                String g = ((String) groups.elementAt(i)).trim();
                if (g.startsWith("#")) continue;
                if (g.startsWith("vis")) {
                    try {
                        if (mvc == null) mvc = de.vw.mib.asl.api.navigation.ASLNavigationFactory.getNavigationApi()
                            .getMapViewerClusterService(new de.vw.mib.asl.api.navigation.map.instrumentcluster.MapViewerClusterListenerAdapter() { });
                        if (g.equals("vis1")) mvc.setMapViewerClusterVisible(); else mvc.setMapViewerClusterInvisible();
                        Log.log("rebind: " + g + " ok, visibility now " + mvc.getMapViewerClusterVisibility());
                    } catch (Throwable t) { Log.log("rebind " + g + " error: " + t); }
                    Thread.sleep(600);
                    continue;
                }
                if (g.equals("st")) {
                    try {
                        if (mvc == null) mvc = de.vw.mib.asl.api.navigation.ASLNavigationFactory.getNavigationApi()
                            .getMapViewerClusterService(new de.vw.mib.asl.api.navigation.map.instrumentcluster.MapViewerClusterListenerAdapter() { });
                        Log.log("rebind st: vis=" + mvc.getMapViewerClusterVisibility() + " inst=" + mvc.getMapViewerClusterInstanceStatus()
                            + " zoom=" + mvc.getZoomLevel() + " az=" + mvc.isAutozoomEnabled()
                            + " hold=" + list(hold));
                    } catch (Throwable t) { Log.log("rebind st error: " + t); }
                    continue;
                }
                if (g.startsWith("rate")) {
                    final int rate = Integer.parseInt(g.substring(4).trim());
                    s.setUpdateRate(CLUSTER_DISPLAY, rate, new de.vw.mib.display.DisplayManagementResponse() {
                        public void finished(int r) { Log.log("rebind: rate " + rate + " finished " + r); }
                        public void setResult(int[] a) { }
                        public void setResult(int a) { }
                    });
                    Log.log("rebind: setUpdateRate(4," + rate + ") sent");
                    Thread.sleep(400);
                    continue;
                }
                if (g.startsWith("hold")) {
                    hold = parse(g.substring(4));
                    holdSvc = s;
                    s.showDisplayables(CLUSTER_DISPLAY, hold);
                    Log.log("rebind: hold display " + CLUSTER_DISPLAY + " -> " + list(hold));
                    Thread.sleep(400);
                    continue;
                }
                if (g.equals("unhold")) { hold = null; Log.log("rebind: unhold"); continue; }
                if (g.startsWith("sleep")) { Thread.sleep(Integer.parseInt(g.substring(5).trim())); continue; }
                int[] ids = parse(g);
                s.showDisplayables(CLUSTER_DISPLAY, ids);
                Log.log("rebind: display " + CLUSTER_DISPLAY + " -> " + list(ids));
                Thread.sleep(400);
            }
            return true;
        } catch (Throwable t) { Log.log("rebind error: " + t); return false; }
    }
    /** v15: content-based trigger. The old mtime check missed (a) a request written before this thread
     *  started (CarPlay connected within ~1.5 min of boot: clusterd's hold request was taken as "already seen")
     *  and (b) two requests in the same second (QNX mtime has 1 s resolution). clusterd now appends a
     *  ";#<counter>" group so every request is a different string; groups starting with '#' are ignored. */
    static String applied = "";
    public void run() {
        File f = new File("/tmp/vwcn_rebind");
        try { Thread.sleep(5000); rebindLogOnly(); } catch (Throwable t) { }
        int tick = 0;
        String failedQ = null;   // v16: a failed request is retried every 2 s and logged once (was: every 1 s, 2 lines each)
        long nextTry = 0;
        while (!stop) {
            try {
                Thread.sleep(1000);
                String q = f.exists() ? readRaw(f) : "";
                if (q.length() > 0 && !q.equals(applied)) {
                    long now = System.currentTimeMillis();
                    boolean first = !q.equals(failedQ);
                    if (first || now >= nextTry) {
                        if (first) Log.log("rebind requested seq=" + q);
                        if (rebind(q.equals("1") ? DEFAULT_SEQ : q)) { applied = q; tick = 0; failedQ = null; }
                        else {
                            if (first) Log.log("rebind: not applied yet, will retry every 2 s");
                            failedQ = q;
                            nextTry = now + 2000;
                        }
                    }
                }
                int[] h = hold;
                if (h != null && holdSvc != null && (++tick * 1000) >= HOLD_MS) {
                    tick = 0;
                    try { holdSvc.showDisplayables(CLUSTER_DISPLAY, h); } catch (Throwable t) { Log.log("hold error: " + t); }
                }
            } catch (Throwable t) { Log.log("rebind loop error: " + t); }
        }
    }
    static String readRaw(File f) {
        try {
            java.io.FileInputStream in = new java.io.FileInputStream(f);
            byte[] b = new byte[160]; int n = in.read(b); in.close();
            return n > 0 ? new String(b, 0, n).trim() : "";
        } catch (Throwable t) { return ""; }
    }
    static void rebindLogOnly() {
        DisplayManager m = dm();
        if (m == null) { Log.log("rebind: DisplayManager not available yet"); return; }
        for (int d = 0; d < 6; d++) {
            try { Log.log("rebind: boot display " + d + " active=" + list(m.getActiveDisplayables(d))); } catch (Throwable t) { }
        }
    }
}
