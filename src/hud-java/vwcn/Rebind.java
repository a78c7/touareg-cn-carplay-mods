package vwcn;

import de.vw.mib.asl.framework.api.dsiproxy.DSIProxy;
import de.vw.mib.asl.framework.api.dsiproxy.DSIProxyFactory;
import de.vw.mib.graphics.display.DisplayManager;
import java.io.File;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.dsi.ifc.base.DSIListener;
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
    /** v19.8 / v21: 保存当前活跃 CarPlay 流的 displayable 列表（如 [58,33]），
     *  即使用户通过方向盘 OK 双击临时切回原车地图，也保留以便随时一键切回 CarPlay 地图。 */
    static volatile int[] streamHold;
    static volatile boolean userNative;
    static volatile boolean toggleRequested;
    static volatile de.vw.mib.display.DisplayManagementService holdSvc;
    static final int HOLD_FALLBACK_MS = 5000;
    static final File TOGGLE_FILE = new File("/tmp/vwcn_toggle");
    static final File NATIVE_FLAG = new File("/tmp/vwcn_user_native");
    private static void setNativeFlag(boolean on) {
        try {
            if (on) { java.io.FileWriter fw = new java.io.FileWriter(NATIVE_FLAG); fw.write("1\n"); fw.close(); }
            else if (NATIVE_FLAG.exists()) NATIVE_FLAG.delete();
        } catch (Throwable ignore) {}
    }
    /* v21.1: 事件驱动监听 DSIDisplayManagementListener（display 4 的 showDisplayablesResult / activeContext），
     * 平时 [58,33] 在位时 0 次多余 showDisplayables 盲刷（消除 3 秒盲刷引起的 0.1s 透底闪原车地图），
     * 一旦原车把仪表屏切走，<=100ms 立刻拉回；若特定固件挂不上监听器，自动退回 5 秒兜底（零 NPE）。 */
    static volatile boolean dsiWatchInstalled;
    static volatile boolean dsiWatchConfirmed;
    static volatile boolean restoreNeeded;
    static volatile String lastSeenCluster;
    static volatile long lastAssertAt;
    static int ctxLogCnt;

    /** 判断目标顶层窗口列表（如 [58,33]）在 Display 4 上是否仍然在位：
     *  在 P2869 等固件上，当底层原车地图 33 处于不可见状态时，showDisplayablesResult 会仅回报可见前缀 [58]。
     *  此时顶层窗口 58 实际稳居第一顺位并未被抢屏；若按整数组严格相等比对会引发 500ms (2Hz) 无限重刷闪屏（#14）。
     *  只要实际列表非空、长度不超过目标且所有元素与目标前缀逐位一致（即 [58,33] 或 [58]），即视为在位正常；
     *  一旦顶层被切为 [33]、[]、[33,58] 或其他窗口则立即返回 false 触发 <=100ms 恢复。 */
    private static boolean isActivePrefix(int[] actual, int[] target) {
        if (target == null || target.length == 0) return actual == null || actual.length == 0;
        if (actual == null || actual.length == 0 || actual.length > target.length) return false;
        for (int i = 0; i < actual.length; i++) {
            if (actual[i] != target[i]) return false;
        }
        return true;
    }

    private static synchronized void ensureDsiWatcher() {
        if (dsiWatchInstalled) return;
        dsiWatchInstalled = true;
        try {
            DSIProxy p = DSIProxyFactory.getDSIProxyAPI().getDSIProxy();
            if (p == null) { Log.log("rebind: DSIProxy null, using fallback hold"); return; }
            Class lsnCls = Class.forName("org.dsi.ifc.displaymanagement.DSIDisplayManagementListener");
            Class targetCls = Class.forName("de.vw.mib.genericevents.Target");
            ClassLoader cl = lsnCls.getClassLoader();
            if (cl == null) cl = Thread.currentThread().getContextClassLoader();
            if (cl == null) cl = ClassLoader.getSystemClassLoader();
            DSIListener dsiLsn = (DSIListener) Proxy.newProxyInstance(cl, new Class[] { lsnCls }, new InvocationHandler() {
                public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
                    String name = m.getName();
                    if ("equals".equals(name)) return (args != null && args.length == 1 && proxy == args[0]) ? Boolean.TRUE : Boolean.FALSE;
                    if ("hashCode".equals(name)) return new Integer(System.identityHashCode(proxy));
                    if ("toString".equals(name)) return "vwcn.Rebind.DMListener";
                    try {
                        if ("showDisplayablesResult".equals(name) && args != null && args.length >= 2
                            && args[0] instanceof Number && args[1] instanceof int[]) {
                            int did = ((Number) args[0]).intValue();
                            int[] ids = (int[]) args[1];
                            if (did == CLUSTER_DISPLAY) {
                                if (!dsiWatchConfirmed) {
                                    dsiWatchConfirmed = true;
                                    Log.log("rebind: DSI display 4 watcher confirmed active " + list(ids));
                                }
                                int[] h = hold;
                                if (h != null && !isActivePrefix(ids, h)) {
                                    lastSeenCluster = list(ids);
                                    restoreNeeded = true;
                                }
                            }
                        } else if ("activeContext".equals(name) && args != null && args.length >= 2
                            && args[0] instanceof Number && args[1] instanceof Number) {
                            int did = ((Number) args[1]).intValue();
                            if (did == CLUSTER_DISPLAY && hold != null
                                && (System.currentTimeMillis() - lastAssertAt) >= 600L) {
                                if (ctxLogCnt < 10) {
                                    ctxLogCnt++;
                                    Log.log("rebind: display 4 activeContext=" + args[0] + " -> restore");
                                }
                                lastSeenCluster = "ctx=" + args[0];
                                restoreNeeded = true;
                            }
                        }
                    } catch (Throwable t) {
                    }
                    Class rt = m.getReturnType();
                    if (rt == Boolean.TYPE) return Boolean.FALSE;
                    if (rt == Integer.TYPE) return new Integer(0);
                    if (rt == Long.TYPE) return new Long(0L);
                    return null;
                }
            });
            Method mAdd = p.getClass().getMethod("addResponseListener", new Class[] { targetCls, Class.class, DSIListener.class });
            try {
                mAdd.invoke(p, new Object[] { null, lsnCls, dsiLsn });
                Log.log("rebind: DSIDisplayManagementListener registered (null target)");
            } catch (Throwable t1) {
                Object dummyTarget = Proxy.newProxyInstance(cl, new Class[] { targetCls }, new InvocationHandler() {
                    public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
                        String n = m.getName();
                        if ("equals".equals(n)) return (args != null && args.length == 1 && proxy == args[0]) ? Boolean.TRUE : Boolean.FALSE;
                        if ("hashCode".equals(n)) return new Integer(0x7777);
                        if ("toString".equals(n)) return "vwcn.Rebind.Target";
                        Class rt = m.getReturnType();
                        if (rt == Integer.TYPE) return new Integer(0x7777);
                        if (rt == Boolean.TYPE) return Boolean.FALSE;
                        if (rt == Long.TYPE) return new Long(0L);
                        return null;
                    }
                });
                mAdd.invoke(p, new Object[] { dummyTarget, lsnCls, dsiLsn });
                Log.log("rebind: DSIDisplayManagementListener registered (proxy target)");
            }
        } catch (Throwable t) {
            Log.log("rebind: DSI watcher unavailable (" + t + "), using fallback hold");
        }
    }

    /** 异步请求在 CarPlay 仪表地图与原车仪表地图之间切换（避免在 BAP 回调线程阻塞）。 */
    static void requestToggle(String reason) {
        if (streamHold == null) {
            Log.log("switch: ignored (" + reason + "), no active CarPlay stream");
            return;
        }
        Log.log("switch: requested (" + reason + ") cur=" + (hold != null ? "carplay" : "native"));
        toggleRequested = true;
    }

    /** 新开启 CarPlay 路线指引时，若用户此前手动停留在原车地图，自动切回 CarPlay 仪表地图。 */
    static void restoreCarPlayOnNavStart() {
        if (userNative && hold == null && streamHold != null) {
            Log.log("switch: new CarPlay guidance started -> auto restore CarPlay map");
            toggleRequested = true;
        }
    }

    private static String csv(int[] a) {
        StringBuffer b = new StringBuffer();
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(','); b.append(a[i]); }
        return b.toString();
    }

    private static void applyToggle() {
        int[] sh = streamHold;
        if (sh == null) {
            Log.log("switch: aborted, stream ended");
            return;
        }
        if (hold != null) {
            userNative = true;
            setNativeFlag(true);
            hold = null;
            Log.log("switch: CarPlay -> native map");
            rebind("33;vis1;rate10;st");
        } else {
            userNative = false;
            setNativeFlag(false);
            Log.log("switch: native -> CarPlay map " + list(sh));
            rebind("hold" + csv(sh) + ";rate10;st");
        }
    }
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
                    ensureDsiWatcher();
                    int[] parsed = parse(g.substring(4));
                    streamHold = parsed;
                    userNative = false;
                    setNativeFlag(false);
                    hold = parsed;
                    holdSvc = s;
                    restoreNeeded = false;
                    lastAssertAt = System.currentTimeMillis();
                    s.showDisplayables(CLUSTER_DISPLAY, hold);
                    Log.log("rebind: hold display " + CLUSTER_DISPLAY + " -> " + list(hold));
                    Thread.sleep(400);
                    continue;
                }
                if (g.equals("unhold")) { hold = null; streamHold = null; userNative = false; setNativeFlag(false); restoreNeeded = false; Log.log("rebind: unhold"); continue; }
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
        long lastPoll = 0;
        long lastHold = 0;
        String failedQ = null;   // v16: a failed request is retried every 2 s and logged once (was: every 1 s, 2 lines each)
        long nextTry = 0;
        while (!stop) {
            try {
                Thread.sleep(100);
                long now = System.currentTimeMillis();
                if (TOGGLE_FILE.exists()) {
                    TOGGLE_FILE.delete();
                    requestToggle("file");
                }
                if (toggleRequested) {
                    toggleRequested = false;
                    applyToggle();
                    lastHold = System.currentTimeMillis();
                    continue;
                }
                if (now - lastPoll >= 1000) {
                    lastPoll = now;
                    String q = f.exists() ? readRaw(f) : "";
                    if (q.length() > 0 && !q.equals(applied)) {
                        boolean first = !q.equals(failedQ);
                        if (first || now >= nextTry) {
                            if (first) Log.log("rebind requested seq=" + q);
                            if (rebind(q.equals("1") ? DEFAULT_SEQ : q)) { applied = q; lastHold = now; failedQ = null; }
                            else {
                                if (first) Log.log("rebind: not applied yet, will retry every 2 s");
                                failedQ = q;
                                nextTry = now + 2000;
                            }
                        }
                    }
                }
                int[] h = hold;
                if (h != null && holdSvc != null) {
                    if (restoreNeeded && (now - lastAssertAt) >= 500L) {
                        restoreNeeded = false;
                        lastAssertAt = now;
                        lastHold = now;
                        String prev = lastSeenCluster;
                        lastSeenCluster = null;
                        try {
                            holdSvc.showDisplayables(CLUSTER_DISPLAY, h);
                            Log.log("rebind: restored display " + CLUSTER_DISPLAY + " -> " + list(h) + (prev != null ? " (was " + prev + ")" : ""));
                        } catch (Throwable t) { Log.log("hold restore error: " + t); }
                    } else if (!dsiWatchConfirmed && (now - lastHold) >= HOLD_FALLBACK_MS) {
                        lastHold = now;
                        lastAssertAt = now;
                        try { holdSvc.showDisplayables(CLUSTER_DISPLAY, h); } catch (Throwable t) { Log.log("hold error: " + t); }
                    }
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
