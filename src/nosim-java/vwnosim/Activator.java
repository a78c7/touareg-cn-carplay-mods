package vwnosim;

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
 * VW MH2p HMI bundle (ID 98): 屏蔽连接 CarPlay / Android Auto 时反复弹出的
 * 「SIM 卡仅用于数据连接。」(ASLPhoneSystemEventConstants.PHONE_SIM_DATAONLY) 弹窗。
 *
 * 原理：
 * 1. 启动时从车机当前 lsd.jxe 动态解析 PHONE_SIM_DATAONLY 事件编号（在 P2878 上为 1410020），
 *    不硬编码魔数，跨 VW MH2p 固件版本安全；若当前车机无该常量则自动退出不做任何改动。
 * 2. 通过反射包装 de.vw.mib.event.internal.ServiceManager.systemEventSender
 *    （单方法公开接口 de.vw.mib.coreapi.asl.SystemEventSender.fireSystemEvent(int)），
 *    仅丢弃 PHONE_SIM_DATAONLY 事件，其余全部 HMI 系统事件原样透传。
 * 3. 不改动 SIM 卡数据/语音模式、蓝牙、CarPlay、eCall/SOS 或任何 5F 编码。
 * 4. 纯 Java 1.4 / J2ME Foundation Profile 1.1 (class version 48)，零原厂编译期依赖。
 *
 * 临时停用（调试用）：在车机创建 /tmp/vw_nosim_off；日志位于 /tmp/vw_nosim.log。
 */
public final class Activator implements BundleActivator, Runnable, InvocationHandler {
    private static final String LOG_PATH = "/tmp/vw_nosim.log";
    private static final long LOG_MAX = 65536L;
    private static final String BYPASS_FLAG = "/tmp/vw_nosim_off";

    private static final String CONST_CLASS = "generated.de.vw.mib.asl.internal.phone.ASLPhoneSystemEventConstants";
    private static final String CONST_FIELD = "PHONE_SIM_DATAONLY";
    private static final String SM_CLASS = "de.vw.mib.event.internal.ServiceManager";
    private static final String SM_FIELD = "systemEventSender";
    private static final String SENDER_IFACE = "de.vw.mib.coreapi.asl.SystemEventSender";

    private volatile boolean stop;
    private volatile boolean bypassed;
    private volatile int targetEventId = -1;
    private volatile Field senderField;
    private volatile Class senderIface;
    private volatile Object origSender;
    private volatile Object proxySender;
    private int droppedCount;

    public void start(BundleContext ctx) throws Exception {
        try {
            log("VWNoSimPopup v1.0 Activator.start");
            Thread t = new Thread(this, "vw-nosim-popup");
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
            int evId = resolveEventId();
            if (evId < 0) {
                log("PHONE_SIM_DATAONLY not found in this HMI; exiting without hooking");
                return;
            }
            targetEventId = evId;
            log("resolved PHONE_SIM_DATAONLY = " + evId);

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
            senderIface = iface;

            int waitCount = 0;
            while (!stop) {
                bypassed = new File(BYPASS_FLAG).exists();
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

    private int resolveEventId() {
        try {
            Class c = Class.forName(CONST_CLASS);
            Field f = c.getField(CONST_FIELD);
            return f.getInt(null);
        } catch (Throwable t) {
            log("resolveEventId error: " + t);
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
            return "VWNoSimPopup[" + origSender + "]";
        }
        if ("fireSystemEvent".equals(name) && args != null && args.length == 1 && args[0] instanceof Integer) {
            int id = ((Integer) args[0]).intValue();
            if (!bypassed && id == targetEventId) {
                onDropped(id);
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

    private synchronized void onDropped(int id) {
        droppedCount++;
        if (droppedCount <= 20 || (droppedCount % 50) == 0) {
            log("dropped PHONE_SIM_DATAONLY (" + id + ") #" + droppedCount);
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
