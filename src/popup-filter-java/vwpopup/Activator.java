package vwpopup;

import java.io.File;
import java.io.FileWriter;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.osgi.framework.BundleActivator;
import org.osgi.framework.BundleContext;

/**
 * VW MH2p HMI 弹窗过滤插件（Bundle ID 98），由可选模块按开关文件独立启用：
 * - /mnt/app/eso/hmi/lsd/config/popup_filter/no_sim_dataonly
 *     屏蔽连接 CarPlay / Android Auto 时的「SIM 卡仅用于数据连接。」弹窗
 *     (ASLPhoneSystemEventConstants.PHONE_SIM_DATAONLY，P2878 上为 1410020)
 * - /mnt/app/eso/hmi/lsd/config/popup_filter/no_bt_auth
 *     屏蔽蓝牙 OBEX 认证触发的「输入用户名」弹窗 (Busrlo / BLUETOOTH_UID_SPELLER_REQ__LatinOnly)
 *     (ASLPhoneBluetoothSystemEventConstants.BLUETOOTH_CONNECT_AUTH_REQUIRED，P2878 上为 132)
 *
 * 原理：
 * 1. 启动时从当前车机 lsd.jxe 动态解析上述事件编号（不硬编码固件版本偏移）。
 * 2. 通过单一 Proxy 包装 de.vw.mib.event.internal.ServiceManager.systemEventSender
 *    （单方法公开接口 de.vw.mib.coreapi.asl.SystemEventSender.fireSystemEvent(int)），
 *    仅丢弃已启用的目标弹窗事件，其余所有 HMI 系统事件原样透传。
 * 3. 纯 Java 1.4 / J2ME Foundation Profile 1.1 (class version 48)，零原厂编译依赖。
 *
 * 调试：日志位于 /tmp/vw_popup_filter.log；创建 /tmp/vw_popup_filter_off 可临时全部放行。
 */
public final class Activator implements BundleActivator, Runnable, InvocationHandler {
    private static final String LOG_PATH = "/tmp/vw_popup_filter.log";
    private static final long LOG_MAX = 65536L;
    private static final String BYPASS_FLAG = "/tmp/vw_popup_filter_off";
    private static final String SAFE_MODE_APP = "/mnt/app/eso/bin/apps/cluster/safe_mode";
    private static final String SAFE_MODE_TMP = "/tmp/vw_hmi_safe_mode";
    private static final String FLAG_DIR = "/mnt/app/eso/hmi/lsd/config/popup_filter";
    private static final String ALT_FLAG_DIR = "/eso/hmi/lsd/config/popup_filter";
    private static final String FLAG_SIM = FLAG_DIR + "/no_sim_dataonly";
    private static final String ALT_FLAG_SIM = ALT_FLAG_DIR + "/no_sim_dataonly";
    private static final String FLAG_BT_AUTH = FLAG_DIR + "/no_bt_auth";
    private static final String ALT_FLAG_BT_AUTH = ALT_FLAG_DIR + "/no_bt_auth";

    private static final String SIM_CONST_CLASS = "generated.de.vw.mib.asl.internal.phone.ASLPhoneSystemEventConstants";
    private static final String SIM_CONST_FIELD = "PHONE_SIM_DATAONLY";
    private static final String BT_CONST_CLASS = "generated.de.vw.mib.asl.internal.phone.bluetooth.ASLPhoneBluetoothSystemEventConstants";
    private static final String BT_CONST_FIELD = "BLUETOOTH_CONNECT_AUTH_REQUIRED";

    private static final String SM_CLASS = "de.vw.mib.event.internal.ServiceManager";
    private static final String SM_FIELD = "systemEventSender";
    private static final String SENDER_IFACE = "de.vw.mib.coreapi.asl.SystemEventSender";

    private volatile boolean stop;
    private volatile boolean bypassed;
    private volatile boolean blockSim;
    private volatile boolean blockBtAuth;
    private volatile int evSim = -1;
    private volatile int evBtAuth = -1;
    private volatile Field senderField;
    private volatile Object origSender;
    private volatile Object proxySender;
    private int droppedSim;
    private int droppedBtAuth;

    public void start(BundleContext ctx) throws Exception {
        try {
            if (new File(SAFE_MODE_APP).exists() || new File(SAFE_MODE_TMP).exists()) {
                log("VWPopupFilter v1.1: safe_mode active, bypassing hook");
                return;
            }
            log("VWPopupFilter v1.1 Activator.start");
            Thread t = new Thread(this, "vw-popup-filter");
            t.setDaemon(true);
            t.start();
        } catch (Throwable t) {
            log("start error: " + t);
        }
    }

    public void stop(BundleContext ctx) throws Exception {
        stop = true;
        try {
            Field f = senderField;
            Object orig = origSender;
            Object p = proxySender;
            if (f != null && orig != null && p != null && f.get(null) == p) {
                f.set(null, orig);
                log("stop: restored original systemEventSender");
            }
        } catch (Throwable t) {
            log("stop error: " + t);
        }
    }

    public void run() {
        try {
            evSim = resolveConst(SIM_CONST_CLASS, SIM_CONST_FIELD);
            evBtAuth = resolveConst(BT_CONST_CLASS, BT_CONST_FIELD);
            refreshFlags();
            log("resolved events: PHONE_SIM_DATAONLY=" + evSim
                + " BLUETOOTH_CONNECT_AUTH_REQUIRED=" + evBtAuth
                + " | flags: no_sim_dataonly=" + blockSim
                + " no_bt_auth=" + blockBtAuth);

            if (evSim < 0 && evBtAuth < 0) {
                log("no target event constants found in this HMI; exiting without hooking");
                return;
            }

            Class smCls = Class.forName(SM_CLASS);
            Field f;
            try {
                f = smCls.getField(SM_FIELD);
            } catch (NoSuchFieldException e) {
                f = smCls.getDeclaredField(SM_FIELD);
                f.setAccessible(true);
            }
            Class iface = Class.forName(SENDER_IFACE);
            senderField = f;

            int waitCount = 0;
            while (!stop) {
                refreshFlags();
                boolean attached = ensureHooked(f, iface);
                if (attached) {
                    Thread.sleep(5000L);
                } else {
                    waitCount++;
                    if (waitCount == 1 || waitCount == 20 || waitCount == 120) {
                        log("waiting for systemEventSender (attempt " + waitCount + ")");
                    }
                    Thread.sleep(waitCount <= 120 ? 500L : 5000L);
                }
            }
        } catch (Throwable t) {
            log("monitor loop error: " + t);
        }
    }

    private void refreshFlags() {
        bypassed = new File(BYPASS_FLAG).exists()
            || new File(SAFE_MODE_APP).exists()
            || new File(SAFE_MODE_TMP).exists();
        blockSim = new File(FLAG_SIM).exists() || new File(ALT_FLAG_SIM).exists();
        blockBtAuth = new File(FLAG_BT_AUTH).exists() || new File(ALT_FLAG_BT_AUTH).exists();
    }

    private static int resolveConst(String clsName, String fieldName) {
        try {
            Class c = Class.forName(clsName);
            Field f = c.getField(fieldName);
            return f.getInt(null);
        } catch (Throwable t) {
            log("resolve " + clsName + "." + fieldName + " failed: " + t);
            return -1;
        }
    }

    private boolean ensureHooked(Field f, Class iface) {
        try {
            Object cur = f.get(null);
            if (cur == null) {
                return false;
            }
            if (cur == proxySender) {
                return true;
            }
            if (!iface.isInstance(cur)) {
                log("unexpected systemEventSender type: " + cur.getClass().getName());
                return false;
            }
            if (Proxy.isProxyClass(cur.getClass())) {
                InvocationHandler h = Proxy.getInvocationHandler(cur);
                if (h instanceof Activator) {
                    proxySender = cur;
                    return true;
                }
            }
            ClassLoader cl = iface.getClassLoader();
            if (cl == null) cl = Thread.currentThread().getContextClassLoader();
            if (cl == null) cl = ClassLoader.getSystemClassLoader();

            origSender = cur;
            Object p = Proxy.newProxyInstance(cl, new Class[] { iface }, this);
            proxySender = p;
            f.set(null, p);
            log("hook installed around " + cur.getClass().getName());
            return true;
        } catch (Throwable t) {
            log("ensureHooked error: " + t);
            return false;
        }
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
            return "VWPopupFilter[" + origSender + "]";
        }
        if (!bypassed && "fireSystemEvent".equals(name) && args != null && args.length == 1 && args[0] instanceof Integer) {
            int id = ((Integer) args[0]).intValue();
            if (blockSim && evSim >= 0 && id == evSim) {
                onDroppedSim(id);
                return null;
            }
            if (blockBtAuth && evBtAuth >= 0 && id == evBtAuth) {
                onDroppedBtAuth(id);
                return null;
            }
        }
        Object target = origSender;
        if (target == null) {
            return null;
        }
        try {
            return method.invoke(target, args);
        } catch (InvocationTargetException ite) {
            throw ite.getTargetException();
        }
    }

    private synchronized void onDroppedSim(int id) {
        droppedSim++;
        if (droppedSim <= 20 || (droppedSim % 50) == 0) {
            log("dropped PHONE_SIM_DATAONLY (" + id + ") #" + droppedSim);
        }
    }

    private synchronized void onDroppedBtAuth(int id) {
        droppedBtAuth++;
        if (droppedBtAuth <= 20 || (droppedBtAuth % 50) == 0) {
            log("dropped BLUETOOTH_CONNECT_AUTH_REQUIRED (" + id + ") #" + droppedBtAuth);
        }
    }

    private static synchronized void log(String msg) {
        FileWriter w = null;
        try {
            File f = new File(LOG_PATH);
            boolean append = !f.exists() || f.length() <= LOG_MAX;
            w = new FileWriter(LOG_PATH, append);
            StringBuffer sb = new StringBuffer();
            sb.append(System.currentTimeMillis()).append(' ').append(msg).append('\n');
            w.write(sb.toString());
        } catch (Throwable t) {
            // ignore
        } finally {
            if (w != null) {
                try {
                    w.close();
                } catch (Throwable t) {
                }
            }
        }
    }
}
