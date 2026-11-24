package vwcn;

import de.vw.mib.asl.framework.api.dsiproxy.DSIProxy;
import de.vw.mib.asl.framework.api.dsiproxy.DSIProxyFactory;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.dsi.ifc.base.DSIListener;

/**
 * v21: 途锐原厂真实传感器扩展数据只读订阅（Java 1.4 class version 48）：
 * 1) de.vw.mib.dsi.carcomfort.DSICarComfort（回退 org.dsi.ifc.carcomfort.DSICarComfort）：
 *    直读 65 模块（J502）四轮真实胎压与低压告警位
 * 2) DSICarDrivingCharacteristics：直读越野俯仰角（pitch）与侧倾角（roll）
 *    （需实车配备空气悬架底盘；无空悬车型如中规 2021–2022 eHybrid 无上报）
 * 3) DSINavigation：直读 GNSS 海拔高度（alt）
 */
final class ExtraDSI {
    // 四轮胎压（统一换算为 bar）与告警位掩码（bit0=FL, bit1=FR, bit2=RL, bit3=RR）
    static volatile float tpFL = Float.NaN, tpFR = Float.NaN, tpRL = Float.NaN, tpRR = Float.NaN;
    static volatile int tpWarn = 0;

    // 车身俯仰角与侧倾角（度）
    static volatile float rollDeg = Float.NaN;
    static volatile float pitchDeg = Float.NaN;

    // 海拔（米）
    static volatile int altM = Integer.MIN_VALUE;

    // 高频 ViewOptions 功率/扭矩可用性探测（-1=未知, 0=网关明确不下发, 1=可用）
    static volatile int pwrOpt = -1;
    static volatile int tqOpt = -1;

    private static Slot sDrv, sComf, sNav, sVsOpt;
    private static boolean rdkLogged, drvLogged, navLogged, vsOptLogged;

    private ExtraDSI() {}

    static synchronized void subscribeAll() {
        if (sDrv == null) {
            sDrv = new Slot(
                "org.dsi.ifc.cardrivingcharacteristics.DSICarDrivingCharacteristics",
                "org.dsi.ifc.cardrivingcharacteristics.DSICarDrivingCharacteristicsListener",
                new String[] { "ATTR_TADCURRENTROLLANGLE", "ATTR_TADCURRENTPITCHANGLE" },
                new int[] { 22, 23 },
                new DrvHandler()
            );
        }
        if (sComf == null) {
            sComf = new Slot(
                new String[] {
                    "de.vw.mib.dsi.carcomfort.DSICarComfort",
                    "org.dsi.ifc.carcomfort.DSICarComfort"
                },
                new String[] {
                    "de.vw.mib.dsi.carcomfort.DSICarComfortListener",
                    "org.dsi.ifc.carcomfort.DSICarComfortListener"
                },
                new String[] { "ATTR_RDKTIREDISPLAY" },
                new int[] { 37 },
                new ComfHandler()
            );
        }
        if (sNav == null) {
            sNav = new Slot(
                "org.dsi.ifc.navigation.DSINavigation",
                "org.dsi.ifc.navigation.DSINavigationListener",
                new String[] { "ATTR_SOPOSPOSITION" },
                new int[] { 28 },
                new NavHandler()
            );
        }
        if (sVsOpt == null) {
            sVsOpt = new Slot(
                "org.dsi.ifc.carvehiclestates.DSICarVehicleStates",
                "org.dsi.ifc.carvehiclestates.DSICarVehicleStatesListener",
                new String[] { "ATTR_DYNAMICVEHICLEINFOHIGHFREQUENTVIEWOPTIONS" },
                new int[] { 12 },
                new VsOptHandler()
            );
        }
        sDrv.subscribe();
        sComf.subscribe();
        sNav.subscribe();
        sVsOpt.subscribe();
    }

    static synchronized void unsubscribeAll() {
        if (sDrv != null) sDrv.unsubscribe();
        if (sComf != null) sComf.unsubscribe();
        if (sNav != null) sNav.unsubscribe();
        if (sVsOpt != null) sVsOpt.unsubscribe();
    }

    static int getIntField(Object obj, String name, int defVal) {
        if (obj == null) return defVal;
        try {
            Field f = obj.getClass().getField(name);
            Object v = f.get(obj);
            if (v instanceof Number) return ((Number) v).intValue();
        } catch (Throwable t) {
        }
        return defVal;
    }

    static Object getObjField(Object obj, String name) {
        if (obj == null) return null;
        try {
            return obj.getClass().getField(name).get(obj);
        } catch (Throwable t) {
            return null;
        }
    }

    /** 将大众 RDK 原始整数压力值按 DSI CarConstants 单位换算为 bar（过滤哨兵无效值 0/254/255/65535）。
     *  实车反汇编 HsmTargetCarService.dsiCarComfortUpdateRDKTireDisplay 确认 DSI pressureUnit 枚举（与 ASL 顺序不同）：
     *    0 = PRESSUREUNIT_BAR (0.1 bar/LSB, 例 24 -> 2.4 bar)
     *    1 = PRESSUREUNIT_PSI (0.5 psi/LSB, 例 70 -> 35.0 psi -> 2.41 bar)
     *    2 = PRESSUREUNIT_KPA (10 kPa/LSB = 0.1 bar/LSB, 例 24 -> 240 kPa -> 2.4 bar) */
    static float toBar(int raw, int unit) {
        if (raw <= 0 || raw == 254 || raw == 255 || raw >= 6000) return Float.NaN;
        float bar;
        if (unit == 1) {
            float psi = raw * 0.5f;
            bar = psi * 0.0689476f;
        } else if (unit == 2) {
            if (raw > 800) bar = raw / 1000.0f;
            else if (raw >= 80) bar = raw / 100.0f;
            else bar = raw / 10.0f;
        } else {
            bar = raw >= 80 ? (raw / 100.0f) : (raw / 10.0f);
        }
        if (Float.isNaN(bar) || bar < 0.5f || bar > 5.5f) return Float.NaN;
        return bar;
    }

    private static final class Slot {
        final String[] svcNames, lsnNames;
        final String[] attrNames;
        final int[] fallbackAttrs;
        final InvocationHandler handler;
        String activeSvcName;
        Class svcCls;
        Object svc;
        Method mSet, mClear;
        DSIListener proxy;
        int[] attrs;
        boolean on;

        Slot(String svcName, String lsnName, String[] attrNames, int[] fallbackAttrs, InvocationHandler handler) {
            this(new String[] { svcName }, new String[] { lsnName }, attrNames, fallbackAttrs, handler);
        }

        Slot(String[] svcNames, String[] lsnNames, String[] attrNames, int[] fallbackAttrs, InvocationHandler handler) {
            this.svcNames = svcNames;
            this.lsnNames = lsnNames;
            this.attrNames = attrNames;
            this.fallbackAttrs = fallbackAttrs;
            this.handler = handler;
        }

        private static Method findNotifMethod(Class cls, String name) {
            try {
                return cls.getMethod(name, new Class[] { int[].class, DSIListener.class });
            } catch (Throwable t) {
            }
            Method[] ms = cls.getMethods();
            for (int i = 0; i < ms.length; i++) {
                if (name.equals(ms[i].getName())) {
                    Class[] pt = ms[i].getParameterTypes();
                    if (pt.length == 2 && pt[0] == int[].class && DSIListener.class.isAssignableFrom(pt[1])) {
                        return ms[i];
                    }
                }
            }
            return null;
        }

        void subscribe() {
            if (on) return;
            Throwable lastErr = null;
            DSIProxy p = DSIProxyFactory.getDSIProxyAPI().getDSIProxy();
            for (int k = 0; k < svcNames.length; k++) {
                String sName = svcNames[k];
                String lName = lsnNames[k];
                try {
                    Class sc = (svc != null && svcCls != null) ? svcCls : Class.forName(sName);
                    Class lc = Class.forName(lName);
                    Object s = svc;
                    if (s == null) s = p.getService(null, sc);
                    if (s == null) continue;
                    int[] a = attrs;
                    if (a == null) {
                        int[] tmp = new int[attrNames.length];
                        int n = 0;
                        for (int i = 0; i < attrNames.length; i++) {
                            int id = -1;
                            try {
                                id = sc.getField(attrNames[i]).getInt(null);
                            } catch (Throwable t1) {
                                for (int j = 0; j < svcNames.length && id <= 0; j++) {
                                    if (j == k) continue;
                                    try {
                                        id = Class.forName(svcNames[j]).getField(attrNames[i]).getInt(null);
                                    } catch (Throwable t2) {
                                    }
                                }
                                if (id <= 0 && i < fallbackAttrs.length) id = fallbackAttrs[i];
                            }
                            if (id > 0) tmp[n++] = id;
                        }
                        if (n == 0) {
                            Log.log("extradsi: no attrs for " + sName);
                            return;
                        }
                        a = new int[n];
                        System.arraycopy(tmp, 0, a, 0, n);
                    }
                    Method ms = mSet != null ? mSet : findNotifMethod(sc, "setNotification");
                    Method mc = mClear != null ? mClear : findNotifMethod(sc, "clearNotification");
                    if (ms == null) throw new NoSuchMethodException("setNotification on " + sName);
                    DSIListener prx = proxy;
                    if (prx == null) {
                        ClassLoader cl = lc.getClassLoader();
                        if (cl == null) cl = Thread.currentThread().getContextClassLoader();
                        if (cl == null) cl = ClassLoader.getSystemClassLoader();
                        prx = (DSIListener) Proxy.newProxyInstance(cl, new Class[] { lc }, handler);
                    }
                    ms.invoke(s, new Object[] { a, prx });
                    activeSvcName = sName;
                    svcCls = sc;
                    svc = s;
                    attrs = a;
                    mSet = ms;
                    mClear = mc;
                    proxy = prx;
                    on = true;
                    StringBuffer ab = new StringBuffer();
                    for (int i = 0; i < a.length; i++) {
                        if (i > 0) ab.append(',');
                        ab.append(a[i]);
                    }
                    Log.log("extradsi: subscribed " + sName + " (" + a.length + " attrs: " + ab.toString() + ")");
                    return;
                } catch (Throwable t) {
                    lastErr = t;
                }
            }
            if (lastErr != null) {
                Log.log("extradsi: sub " + svcNames[0] + " failed: " + lastErr);
            }
        }

        void unsubscribe() {
            if (!on) return;
            try {
                if (svc != null && proxy != null && attrs != null && mClear != null) {
                    mClear.invoke(svc, new Object[] { attrs, proxy });
                }
            } catch (Throwable t) {
                Log.log("extradsi: unsub " + (activeSvcName != null ? activeSvcName : svcNames[0]) + ": " + t);
            }
            on = false;
        }
    }

    private static Object baseObjMethod(Object proxy, String name, Object[] args, String tag) {
        if ("equals".equals(name)) {
            return (args != null && args.length == 1 && proxy == args[0]) ? Boolean.TRUE : Boolean.FALSE;
        }
        if ("hashCode".equals(name)) {
            return new Integer(System.identityHashCode(proxy));
        }
        if ("toString".equals(name)) {
            return tag;
        }
        return null;
    }

    private static final class DrvHandler implements InvocationHandler {
        public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
            String name = m.getName();
            Object b = baseObjMethod(proxy, name, args, "vwcn.ExtraDSI.Drv");
            if (b != null || "equals".equals(name)) return b;
            try {
                if (args == null || args.length < 2) return null;
                int valid = (args[args.length - 1] instanceof Number) ? ((Number) args[args.length - 1]).intValue() : 0;
                if (valid != 1) return null;
                if ("updateTADCurrentRollAngle".equals(name) && args[0] instanceof Number) {
                    float v = ((Number) args[0]).floatValue();
                    if (!Float.isNaN(v) && v >= -60f && v <= 60f) rollDeg = v;
                    if (!drvLogged) { drvLogged = true; Log.log("extradsi: roll=" + v); }
                } else if ("updateTADCurrentPitchAngle".equals(name) && args[0] instanceof Number) {
                    float v = ((Number) args[0]).floatValue();
                    if (!Float.isNaN(v) && v >= -60f && v <= 60f) pitchDeg = v;
                }
            } catch (Throwable t) {
            }
            return null;
        }
    }

    private static final class ComfHandler implements InvocationHandler {
        public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
            String name = m.getName();
            Object b = baseObjMethod(proxy, name, args, "vwcn.ExtraDSI.Comf");
            if (b != null || "equals".equals(name)) return b;
            try {
                if ("updateRDKTireDisplay".equals(name) && args != null && args.length >= 2) {
                    int valid = (args[1] instanceof Number) ? ((Number) args[1]).intValue() : 0;
                    Object d = args[0];
                    if (valid != 1 || d == null) return null;

                    int pu = -1, pfl = -1, pfr = -1, prl = -1, prr = -1;
                    Object wp = getObjField(d, "wheelPressures");
                    if (wp != null) {
                        pu  = getIntField(wp, "pressureUnit", 0);
                        pfl = getIntField(wp, "frontLeft", -1);
                        pfr = getIntField(wp, "frontRight", -1);
                        prl = getIntField(wp, "rearLeft", -1);
                        prr = getIntField(wp, "rearRight", -1);
                        tpFL = toBar(pfl, pu);
                        tpFR = toBar(pfr, pu);
                        tpRL = toBar(prl, pu);
                        tpRR = toBar(prr, pu);
                    }

                    Object ws = getObjField(d, "wheelStates");
                    if (ws != null) {
                        int mask = 0;
                        int s0 = getIntField(ws, "frontLeft", 0);
                        int s1 = getIntField(ws, "frontRight", 0);
                        int s2 = getIntField(ws, "rearLeft", 0);
                        int s3 = getIntField(ws, "rearRight", 0);
                        if (s0 >= 2 && s0 <= 4) mask |= 1;
                        if (s1 >= 2 && s1 <= 4) mask |= 2;
                        if (s2 >= 2 && s2 <= 4) mask |= 4;
                        if (s3 >= 2 && s3 <= 4) mask |= 8;
                        tpWarn = mask;
                    }

                    if (!rdkLogged) {
                        rdkLogged = true;
                        Log.log("extradsi: RDK P(u=" + pu + ":" + pfl + "," + pfr + "," + prl + "," + prr + ")");
                    }
                }
            } catch (Throwable t) {
            }
            return null;
        }
    }

    private static final class NavHandler implements InvocationHandler {
        public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
            String name = m.getName();
            Object b = baseObjMethod(proxy, name, args, "vwcn.ExtraDSI.Nav");
            if (b != null || "equals".equals(name)) return b;
            try {
                if ("updateSoPosPosition".equals(name) && args != null && args.length >= 2) {
                    int valid = (args[1] instanceof Number) ? ((Number) args[1]).intValue() : 0;
                    Object pos = args[0];
                    if (valid != 1 || pos == null) return null;
                    int h = getIntField(pos, "height", Integer.MIN_VALUE);
                    if (h >= -500 && h <= 9000) altM = h;
                    if (!navLogged && h != Integer.MIN_VALUE) {
                        navLogged = true;
                        Log.log("extradsi: sopos height=" + h);
                    }
                }
            } catch (Throwable t) {
            }
            return null;
        }
    }

    private static int readViewOptState(Object vo, String fieldName) {
        Object opt = getObjField(vo, fieldName);
        if (opt == null) return -1;
        return getIntField(opt, "state", -1);
    }

    private static final class VsOptHandler implements InvocationHandler {
        public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
            String name = m.getName();
            Object b = baseObjMethod(proxy, name, args, "vwcn.ExtraDSI.VsOpt");
            if (b != null || "equals".equals(name)) return b;
            try {
                if ("updateDynamicVehicleInfoHighFrequentViewOptions".equals(name) && args != null && args.length >= 2) {
                    int valid = (args[1] instanceof Number) ? ((Number) args[1]).intValue() : 0;
                    Object vo = args[0];
                    if (valid != 1 || vo == null) return null;
                    int ps = readViewOptState(vo, "currentOutputPower");
                    int ts = readViewOptState(vo, "currentTorque");
                    if (ps == 0) pwrOpt = 0;
                    else if (ps == 2) pwrOpt = 1;
                    if (ts == 0) tqOpt = 0;
                    else if (ts == 2) tqOpt = 1;
                    if (!vsOptLogged) {
                        vsOptLogged = true;
                        Log.log("extradsi: hf viewOptions power=" + ps + " torque=" + ts);
                    }
                }
            } catch (Throwable t) {
            }
            return null;
        }
    }
}
