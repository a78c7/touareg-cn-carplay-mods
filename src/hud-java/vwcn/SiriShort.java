package vwcn;

import de.vw.mib.asl.framework.api.dsiproxy.DSIProxy;
import de.vw.mib.asl.framework.api.dsiproxy.DSIProxyFactory;
import java.io.File;
import java.io.FileWriter;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Hashtable;
import org.dsi.ifc.base.DSIListener;

/**
 * v22.0: 方向盘语音键短按直接唤起 Siri（可选功能，默认关闭）。
 *
 * 启用开关：/mnt/app/eso/bin/apps/cluster/siri_short 或 /tmp/vwcn_siri_short
 * 安全机制：
 * 1. 纯 Java 1.4 (class version 48)，全反射 + Proxy 调用，不引入任何新编译期桩类；
 * 2. 仅在 CarPlay 已连接且语音键发生短按（< 550ms，未触发长按）时触发；
 * 3. 首选反射调用原厂 org.dsi.ifc.carplay.DSICarPlay.requestSIRIAction（ButtonDown -> ButtonUp
 *    走车机原厂音频/麦克风路由），并同步发送 /tmp/cl_alt_cmd "siri" 作为兜底；
 * 4. 命中安全模式或开关不存在时自动退订，零开销不干扰原厂语音。
 */
public final class SiriShort implements Runnable, InvocationHandler {
    static volatile boolean stop;

    private static final String FLAG_APP = "/mnt/app/eso/bin/apps/cluster/siri_short";
    private static final String FLAG_TMP = "/tmp/vwcn_siri_short";
    private static final String SAFE_APP = "/mnt/app/eso/bin/apps/cluster/safe_mode";
    private static final String SAFE_TMP = "/tmp/vw_hmi_safe_mode";
    private static final String CMD_FILE = "/tmp/cl_alt_cmd";
    private static final long SHORT_MAX_MS = 550L;
    private static final long COOLDOWN_MS = 1200L;

    private Class kpCls;
    private Object kp;
    private Method mSetNotif;
    private Method mClearNotif;
    private DSIListener listener;
    private int[] attrs;
    private boolean subscribed;

    private Hashtable voiceKeys;
    private Hashtable kstNames;
    private final Hashtable downAt = new Hashtable();
    private final Hashtable longSeen = new Hashtable();
    private long lastTriggerAt;
    private int cmdSeq;

    // 反射缓存的 DSICarPlay 服务与方法
    private boolean cpResolved;
    private Object cpSvc;
    private Method mSiriAction;
    private int actDown = 1;
    private int actUp = 2;

    public static void start() {
        Thread t = new Thread(new SiriShort(), "vwcn-siri");
        t.setDaemon(true);
        t.start();
    }

    private static boolean enabled() {
        if (new File(SAFE_APP).exists() || new File(SAFE_TMP).exists()) return false;
        return new File(FLAG_APP).exists() || new File(FLAG_TMP).exists();
    }

    private static boolean carplayOnline() {
        if (Rebind.streamHold != null) return true;
        File st = new File("/tmp/cl_status");
        if (st.exists() && st.length() > 10L) {
            long age = System.currentTimeMillis() - st.lastModified();
            if (age >= -5000L && age < 15000L) return true;
        }
        return false;
    }

    public void run() {
        try {
            Thread.sleep(9000L);
            while (!stop) {
                boolean want = enabled();
                if (want && !subscribed) subscribe();
                else if (!want && subscribed) unsubscribe();
                Thread.sleep(2000L);
            }
            if (subscribed) unsubscribe();
        } catch (Throwable t) {
            Log.log("siri loop error: " + t);
        }
    }

    private void loadVoiceConsts() {
        if (voiceKeys != null) return;
        voiceKeys = new Hashtable();
        kstNames = new Hashtable();
        String[] clsNames = new String[] {
            "org.dsi.ifc.keypanel.Constants",
            "org.dsi.ifc.keypanel.DSIKeyPanel"
        };
        for (int c = 0; c < clsNames.length; c++) {
            try {
                Class cls = Class.forName(clsNames[c]);
                Field[] fs = cls.getFields();
                for (int i = 0; i < fs.length; i++) {
                    Field f = fs[i];
                    if (f.getType() != Integer.TYPE) continue;
                    String n = f.getName();
                    int v = f.getInt(null);
                    Integer key = new Integer(v);
                    if (n.startsWith("KST_") && !kstNames.containsKey(key)) {
                        kstNames.put(key, n);
                    } else if (n.startsWith("KEY_")) {
                        String u = n.toUpperCase();
                        if (u.indexOf("PTT") >= 0 || u.indexOf("VOICE") >= 0 ||
                            u.indexOf("SPEECH") >= 0 || u.indexOf("SDS") >= 0) {
                            if (!voiceKeys.containsKey(key)) voiceKeys.put(key, n);
                        }
                    }
                }
            } catch (Throwable t) {
                Log.log("siri consts " + clsNames[c] + ": " + t);
            }
        }
        Log.log("siri: loaded voiceKeys=" + voiceKeys.size() + " kst=" + kstNames.size());
    }

    private void subscribe() {
        try {
            loadVoiceConsts();
            if (voiceKeys.size() == 0) {
                Log.log("siri: no KEY_PTT/VOICE constants found, skipping subscribe");
                return;
            }
            if (kpCls == null) kpCls = Class.forName("org.dsi.ifc.keypanel.DSIKeyPanel");
            Class kplCls = Class.forName("org.dsi.ifc.keypanel.DSIKeyPanelListener");
            if (attrs == null) {
                int aKey = kpCls.getField("ATTR_KEY2").getInt(null);
                attrs = new int[] { aKey };
            }
            if (kp == null) {
                DSIProxy p = DSIProxyFactory.getDSIProxyAPI().getDSIProxy();
                kp = p.getService(null, kpCls);
            }
            if (kp == null) return;
            if (mSetNotif == null) {
                Class[] sig = new Class[] { int[].class, DSIListener.class };
                mSetNotif = kpCls.getMethod("setNotification", sig);
                mClearNotif = kpCls.getMethod("clearNotification", sig);
            }
            if (listener == null) {
                ClassLoader cl = kplCls.getClassLoader();
                if (cl == null) cl = Thread.currentThread().getContextClassLoader();
                if (cl == null) cl = ClassLoader.getSystemClassLoader();
                listener = (DSIListener) Proxy.newProxyInstance(cl, new Class[] { kplCls }, this);
            }
            mSetNotif.invoke(kp, new Object[] { attrs, listener });
            subscribed = true;
            Log.log("siri: subscribed to DSIKeyPanel ATTR_KEY2");
        } catch (Throwable t) {
            Log.log("siri subscribe failed: " + t);
        }
    }

    private void unsubscribe() {
        try {
            if (kp != null && listener != null && attrs != null && mClearNotif != null) {
                mClearNotif.invoke(kp, new Object[] { attrs, listener });
            }
        } catch (Throwable t) {
            Log.log("siri clear failed: " + t);
        }
        subscribed = false;
        downAt.clear();
        longSeen.clear();
        Log.log("siri: unsubscribed");
    }

    private void resolveCarPlayService() {
        if (cpResolved) return;
        cpResolved = true;
        String[] svcNames = new String[] {
            "org.dsi.ifc.carplay.DSICarPlay",
            "org.dsi.ifc.carplay.DSICarplay"
        };
        for (int i = 0; i < svcNames.length; i++) {
            try {
                Class c = Class.forName(svcNames[i]);
                DSIProxy p = DSIProxyFactory.getDSIProxyAPI().getDSIProxy();
                Object svc = p.getService(null, c);
                if (svc == null) continue;
                Method[] ms = c.getMethods();
                for (int j = 0; j < ms.length; j++) {
                    String mn = ms[j].getName();
                    Class[] pt = ms[j].getParameterTypes();
                    if ((mn.equalsIgnoreCase("requestSIRIAction") || mn.equalsIgnoreCase("requestSiriAction"))
                        && pt.length == 1 && pt[0] == Integer.TYPE) {
                        cpSvc = svc;
                        mSiriAction = ms[j];
                        Log.log("siri: bound " + svcNames[i] + "." + mn + "(int)");
                        break;
                    }
                }
                // 读取标准 SIRIACTION 常量（若存在）
                try {
                    Class cc = Class.forName("org.dsi.ifc.carplay.Constants");
                    Field[] fs = cc.getFields();
                    for (int k = 0; k < fs.length; k++) {
                        if (fs[k].getType() != Integer.TYPE) continue;
                        String fn = fs[k].getName().toUpperCase();
                        if (fn.indexOf("SIRIACTION") >= 0) {
                            int v = fs[k].getInt(null);
                            if (fn.indexOf("BUTTONDOWN") >= 0 || fn.indexOf("START") >= 0) actDown = v;
                            else if (fn.indexOf("BUTTONUP") >= 0 || fn.indexOf("STOP") >= 0) actUp = v;
                        }
                    }
                } catch (Throwable ignore) {}
                if (mSiriAction != null) break;
            } catch (Throwable ignore) {}
        }
        Log.log("siri: dsi ready=" + (mSiriAction != null) + " down=" + actDown + " up=" + actUp);
    }

    private void triggerSiri(final String keyName) {
        long now = System.currentTimeMillis();
        if (now - lastTriggerAt < COOLDOWN_MS) return;
        lastTriggerAt = now;
        Log.log("siri: short press on " + keyName + " -> invoking Siri");
        Thread t = new Thread(new Runnable() {
            public void run() {
                try {
                    resolveCarPlayService();
                    boolean dsiOk = false;
                    if (cpSvc != null && mSiriAction != null) {
                        mSiriAction.invoke(cpSvc, new Object[] { new Integer(actDown) });
                        Thread.sleep(120L);
                        mSiriAction.invoke(cpSvc, new Object[] { new Integer(actUp) });
                        dsiOk = true;
                        Log.log("siri: DSICarPlay tap sent (" + actDown + "->" + actUp + ")");
                    }
                    if (!dsiOk) {
                        FileWriter w = new FileWriter(CMD_FILE, false);
                        w.write("siri #" + (++cmdSeq) + "\n");
                        w.close();
                        Log.log("siri: fallback cmd written to " + CMD_FILE);
                    }
                } catch (Throwable ex) {
                    Log.log("siri trigger error: " + ex);
                }
            }
        }, "vwcn-siri-act");
        t.setDaemon(true);
        t.start();
    }

    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        String name = method.getName();
        if ("equals".equals(name)) return (args != null && args.length == 1 && proxy == args[0]) ? Boolean.TRUE : Boolean.FALSE;
        if ("hashCode".equals(name)) return new Integer(System.identityHashCode(proxy));
        if ("toString".equals(name)) return "vwcn.SiriShort";
        try {
            if ("updateKey2".equals(name) && args != null && args.length >= 5 && enabled()) {
                int kbd = ((Integer) args[0]).intValue();
                int key = ((Integer) args[1]).intValue();
                int st = ((Integer) args[2]).intValue();
                int valid = ((Integer) args[4]).intValue();
                if (valid == 1) {
                    String kn = (String) voiceKeys.get(new Integer(key));
                    if (kn != null && carplayOnline()) {
                        String stName = (String) kstNames.get(new Integer(st));
                        String id = kbd + ":" + key;
                        long now = System.currentTimeMillis();
                        if ("KST_PRESSED".equals(stName)) {
                            downAt.put(id, new Long(now));
                            longSeen.remove(id);
                        } else if (stName != null && (stName.indexOf("LONG") >= 0 || stName.indexOf("ULTRA") >= 0)) {
                            longSeen.put(id, Boolean.TRUE);
                        } else if ("KST_SHORT".equals(stName)) {
                            if (longSeen.get(id) == null) triggerSiri(kn);
                        } else if ("KST_RELEASED".equals(stName)) {
                            Long t0 = (Long) downAt.remove(id);
                            Object wasLong = longSeen.remove(id);
                            if (t0 != null && wasLong == null) {
                                long dt = now - t0.longValue();
                                if (dt >= 25L && dt <= SHORT_MAX_MS) {
                                    triggerSiri(kn + " dt=" + dt + "ms");
                                }
                            }
                        }
                    }
                }
            }
        } catch (Throwable ex) {
            Log.log("siri invoke error: " + ex);
        }
        Class rt = method.getReturnType();
        if (rt == Boolean.TYPE) return Boolean.FALSE;
        if (rt == Integer.TYPE) return new Integer(0);
        if (rt == Long.TYPE) return new Long(0L);
        return null;
    }
}
