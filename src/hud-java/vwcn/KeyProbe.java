package vwcn;

import de.vw.mib.asl.framework.api.dsiproxy.DSIProxy;
import de.vw.mib.asl.framework.api.dsiproxy.DSIProxyFactory;
import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Hashtable;
import org.dsi.ifc.base.DSIListener;

/**
 * 方向盘与硬按键只读探针（订阅 DSIKeyPanel ATTR_KEY2 / ATTR_ENCODER2）。
 * 仅在 /tmp/vwcn_keyprobe 或 U 盘根目录 vwcn_keyprobe 存在时启用，平时完全休眠。
 * 纯 Java 1.4（class version 48），全反射 + Proxy 调用，不引入任何新编译期桩类。
 */
public final class KeyProbe implements Runnable, InvocationHandler {
    static volatile boolean stop;
    static volatile boolean active;

    private static final String TMP_FLAG = "/tmp/vwcn_keyprobe";
    private static final String[] USB_FLAGS = new String[] {
        "/fs/usb0_0/vwcn_keyprobe", "/fs/usb1_0/vwcn_keyprobe",
        "/fs/sda0/vwcn_keyprobe", "/fs/sdb0/vwcn_keyprobe"
    };

    private Class kpCls;
    private Object kp;
    private Method mSetNotif;
    private Method mClearNotif;
    private DSIListener listener;
    private int[] attrs;
    private boolean subscribed;

    private Hashtable kbdNames;
    private Hashtable keyNames;
    private Hashtable kstNames;
    private final Hashtable downAt = new Hashtable();

    public static void start() {
        Thread t = new Thread(new KeyProbe(), "vwcn-keyprobe");
        t.setDaemon(true);
        t.start();
    }

    private static boolean flagExists() {
        if (new File(TMP_FLAG).exists()) return true;
        for (int i = 0; i < USB_FLAGS.length; i++) {
            if (new File(USB_FLAGS[i]).exists()) return true;
        }
        return false;
    }

    public void run() {
        try {
            Thread.sleep(10000);
            while (!stop) {
                boolean want = flagExists();
                active = want;
                if (want && !subscribed) subscribe();
                else if (!want && subscribed) unsubscribe();
                Thread.sleep(1000);
            }
            active = false;
            if (subscribed) unsubscribe();
        } catch (Throwable t) {
            Log.log("keyprobe loop error: " + t);
        }
    }

    private void loadConstNames() {
        if (kbdNames != null) return;
        kbdNames = new Hashtable();
        keyNames = new Hashtable();
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
                    if (n.startsWith("KBD_") && !kbdNames.containsKey(key)) kbdNames.put(key, n);
                    else if (n.startsWith("KEY_") && !keyNames.containsKey(key)) keyNames.put(key, n);
                    else if (n.startsWith("KST_") && !kstNames.containsKey(key)) kstNames.put(key, n);
                }
            } catch (Throwable t) {
                Log.log("keyprobe consts " + clsNames[c] + ": " + t);
            }
        }
        Log.log("keyprobe: loaded names kbd=" + kbdNames.size()
            + " key=" + keyNames.size() + " kst=" + kstNames.size());
    }

    private static int readAttr(Class cls, String name) {
        try {
            return cls.getField(name).getInt(null);
        } catch (Throwable t) {
            return -1;
        }
    }

    private void subscribe() {
        try {
            loadConstNames();
            if (kpCls == null) kpCls = Class.forName("org.dsi.ifc.keypanel.DSIKeyPanel");
            Class kplCls = Class.forName("org.dsi.ifc.keypanel.DSIKeyPanelListener");
            if (attrs == null) {
                int aKey = readAttr(kpCls, "ATTR_KEY2");
                int aEnc = readAttr(kpCls, "ATTR_ENCODER2");
                if (aKey >= 0 && aEnc >= 0) attrs = new int[] { aKey, aEnc };
                else if (aKey >= 0) attrs = new int[] { aKey };
                else if (aEnc >= 0) attrs = new int[] { aEnc };
                else {
                    Log.log("keyprobe: ATTR_KEY2/ATTR_ENCODER2 not found");
                    return;
                }
                Log.log("keyprobe: attrs key2=" + aKey + " enc2=" + aEnc);
            }
            if (kp == null) {
                DSIProxy p = DSIProxyFactory.getDSIProxyAPI().getDSIProxy();
                kp = p.getService(null, kpCls);
            }
            if (kp == null) {
                Log.log("keyprobe: DSIKeyPanel service is null");
                return;
            }
            // 从公开接口 DSIKeyPanel（继承自 DSIBase）取方法，避免代理实现类非 public 触发 IllegalAccessException
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
            Log.log("keyprobe: subscribed (read-only) to DSIKeyPanel");
        } catch (Throwable t) {
            Log.log("keyprobe subscribe failed: " + t);
        }
    }

    private void unsubscribe() {
        try {
            if (kp != null && listener != null && attrs != null && mClearNotif != null) {
                mClearNotif.invoke(kp, new Object[] { attrs, listener });
            }
        } catch (Throwable t) {
            Log.log("keyprobe clear failed: " + t);
        }
        subscribed = false;
        downAt.clear();
        Log.log("keyprobe: unsubscribed");
    }

    private static String fmt(Hashtable map, int val) {
        if (map != null) {
            String n = (String) map.get(new Integer(val));
            if (n != null) return val + "(" + n + ")";
        }
        return String.valueOf(val);
    }

    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        String name = method.getName();
        if ("equals".equals(name)) {
            return (args != null && args.length == 1 && proxy == args[0]) ? Boolean.TRUE : Boolean.FALSE;
        }
        if ("hashCode".equals(name)) {
            return new Integer(System.identityHashCode(proxy));
        }
        if ("toString".equals(name)) {
            return "vwcn.KeyProbe";
        }
        try {
            if ("updateKey2".equals(name) && args != null && args.length >= 5) {
                int kbd = ((Integer) args[0]).intValue();
                int key = ((Integer) args[1]).intValue();
                int st = ((Integer) args[2]).intValue();
                int t = ((Integer) args[3]).intValue();
                int valid = ((Integer) args[4]).intValue();

                String stName = kstNames != null ? (String) kstNames.get(new Integer(st)) : null;
                String id = kbd + ":" + key;
                long now = System.currentTimeMillis();
                String dtStr = "";
                if ("KST_PRESSED".equals(stName)) {
                    downAt.put(id, new Long(now));
                } else {
                    Long t0 = (Long) downAt.get(id);
                    if (t0 != null) {
                        dtStr = " dt=" + (now - t0.longValue()) + "ms";
                        if ("KST_RELEASED".equals(stName)) downAt.remove(id);
                    }
                }
                Log.log("keyprobe: key2 kbd=" + fmt(kbdNames, kbd)
                    + " key=" + fmt(keyNames, key)
                    + " st=" + fmt(kstNames, st)
                    + " arg3=" + t + " valid=" + valid + dtStr
                    + " cpHold=" + (Rebind.hold != null ? 1 : 0));
            } else if ("updateEncoder2".equals(name) && args != null && args.length >= 5) {
                int kbd = ((Integer) args[0]).intValue();
                int enc = ((Integer) args[1]).intValue();
                int delta = ((Integer) args[2]).intValue();
                int t = ((Integer) args[3]).intValue();
                int valid = ((Integer) args[4]).intValue();
                Log.log("keyprobe: enc2 kbd=" + fmt(kbdNames, kbd)
                    + " enc=" + fmt(keyNames, enc)
                    + " delta=" + delta + " arg3=" + t + " valid=" + valid);
            } else {
                StringBuffer sb = new StringBuffer("keyprobe: ");
                sb.append(name).append('(');
                if (args != null) {
                    for (int i = 0; i < args.length; i++) {
                        if (i > 0) sb.append(',');
                        sb.append(args[i]);
                    }
                }
                sb.append(')');
                Log.log(sb.toString());
            }
        } catch (Throwable ex) {
            Log.log("keyprobe invoke error: " + ex);
        }
        Class rt = method.getReturnType();
        if (rt == Boolean.TYPE) return Boolean.FALSE;
        if (rt == Integer.TYPE) return new Integer(0);
        if (rt == Long.TYPE) return new Long(0L);
        return null;
    }
}
