package vwcn;

import de.vw.mib.asl.framework.api.dsiproxy.DSIProxy;
import de.vw.mib.asl.framework.api.dsiproxy.DSIProxyFactory;
import de.vw.mib.asl.internal.car.abstractclasses.dsiadapter.DSICarKombiAdapter;
import de.vw.mib.asl.internal.car.abstractclasses.dsiadapter.DSICarVehicleStatesAdapter;
import java.io.File;
import java.io.FileWriter;
import org.dsi.ifc.base.DSIListener;
import org.dsi.ifc.carkombi.BCTankLevel;
import org.dsi.ifc.carkombi.DSICarKombi;
import org.dsi.ifc.carvehiclestates.DSICarVehicleStates;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoHighFrequent;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoMidFrequent;
import org.dsi.ifc.global.CarBCConsumption;
import org.dsi.ifc.global.CarBCTemperature;

/**
 * v16: data for the cluster info box (drawn by cl_render on the CarPlay map).
 * Subscribes to vehicle states (power, torque, acceleration, gear, gearbox oil temperature) and the kombi
 * board computer (engine oil and coolant temperature, current consumption, tank level) only while the
 * CarPlay map is held on the cluster (Rebind.hold != null), and writes one line to /tmp/dash.state about
 * 5 times a second: "n=<counter> p=<kW> tq=<Nm> gx=<long g> gy=<lat g> gm=<lever> gc=<gear> gbt=<C> oil=<C> cool=<C> cv=<current> cu=<unit> ca=<since start avg> au=<unit> tank=<%>".
 * Keys whose value is not available are left out. Off switch: /tmp/vwcn_dash_off.
 */
public final class Dash implements Runnable {
    static volatile boolean stop;
    static final String STATE = "/tmp/dash.state";
    static final String TMP = "/tmp/dash.state.new";
    static final String OFF = "/tmp/vwcn_dash_off";
    static final int[] VS_ATTRS = new int[] {14, 15};
    // kombi: 4 view options, 6 current consumption 1, 11 short-term ("since start") average consumption 1,
    // 23 tank level 1, 63 coolant, 64 oil temperature value
    static final int[] KO_ATTRS = new int[] {4, 6, 11, 23, 63, 64};

    private DSICarVehicleStates vs;
    private DSICarKombi ko;
    private DSIListener vsl, kol;
    private boolean subscribed;
    private long n;
    private String lastLine = "";
    private long lastWrite;

    // latest values (written by DSI callbacks, read by the writer thread)
    volatile float power = Float.NaN, gxS = Float.NaN, gyS = Float.NaN, consumption = Float.NaN, tripAvg = Float.NaN;
    volatile int rpm = Integer.MIN_VALUE;
    volatile int torque = Integer.MIN_VALUE, lever = Integer.MIN_VALUE, gear = Integer.MIN_VALUE;
    volatile int gbt = Integer.MIN_VALUE, consUnit = Integer.MIN_VALUE, tank = Integer.MIN_VALUE, tripUnit = Integer.MIN_VALUE;
    volatile float oil = Float.NaN, coolant = Float.NaN;
    private int loggedConsUnit = Integer.MIN_VALUE, loggedOilUnit = Integer.MIN_VALUE, loggedTripUnit = Integer.MIN_VALUE;

    public static void start() {
        Thread t = new Thread(new Dash(), "vwcn-dash");
        t.setDaemon(true);
        t.start();
    }

    public void run() {
        try {
            Thread.sleep(8000);
            Log.log("dash: ready");
            while (!stop) {
                boolean want = Rebind.hold != null && !new File(OFF).exists();
                if (want && !subscribed) subscribe();
                else if (!want && subscribed) unsubscribe();
                if (subscribed) write();
                Thread.sleep(subscribed ? 200 : 1000);
            }
            if (subscribed) unsubscribe();
        } catch (Throwable t) {
            Log.log("dash error: " + t);
        }
    }

    private void subscribe() {
        try {
            DSIProxy p = DSIProxyFactory.getDSIProxyAPI().getDSIProxy();
            if (vs == null) vs = (DSICarVehicleStates) p.getService(null, DSICarVehicleStates.class);
            if (ko == null) ko = (DSICarKombi) p.getService(null, DSICarKombi.class);
            if (vsl == null) vsl = new VsListener();
            if (kol == null) kol = new KoListener();
            vs.setNotification(VS_ATTRS, vsl);
            ko.setNotification(KO_ATTRS, kol);
            subscribed = true;
            Log.log("dash: subscribed");
        } catch (Throwable t) {
            Log.log("dash subscribe failed: " + t);
        }
    }

    private void unsubscribe() {
        try { vs.clearNotification(VS_ATTRS, vsl); } catch (Throwable t) { Log.log("dash clear vs: " + t); }
        try { ko.clearNotification(KO_ATTRS, kol); } catch (Throwable t) { Log.log("dash clear kombi: " + t); }
        subscribed = false;
        new File(STATE).delete();
        Log.log("dash: unsubscribed");
    }

    private void write() {
        StringBuffer b = new StringBuffer(160);
        if (!Float.isNaN(power)) b.append(" p=").append(round1(power));
        if (torque != Integer.MIN_VALUE) b.append(" tq=").append(torque);
        if (rpm != Integer.MIN_VALUE) b.append(" rpm=").append(rpm);
        if (!Float.isNaN(gxS)) b.append(" gx=").append(round2(gxS));
        if (!Float.isNaN(gyS)) b.append(" gy=").append(round2(gyS));
        if (lever != Integer.MIN_VALUE) b.append(" gm=").append(lever).append(" gc=").append(gear);
        if (gbt != Integer.MIN_VALUE) b.append(" gbt=").append(gbt);
        if (!Float.isNaN(oil)) b.append(" oil=").append(Math.round(oil));
        if (!Float.isNaN(coolant)) b.append(" cool=").append(Math.round(coolant));
        if (!Float.isNaN(consumption)) b.append(" cv=").append(round1(consumption)).append(" cu=").append(consUnit);
        if (!Float.isNaN(tripAvg)) b.append(" ca=").append(round1(tripAvg)).append(" au=").append(tripUnit);
        if (tank != Integer.MIN_VALUE) b.append(" tank=").append(tank);
        String body = b.toString();
        long now = System.currentTimeMillis();
        if (body.equals(lastLine) && now - lastWrite < 1000) return;
        lastLine = body;
        lastWrite = now;
        FileWriter w = null;
        try {
            // v16i: /tmp on this unit has no rename/link -> write in place, one write() of a complete line
            w = new FileWriter(STATE, false);
            w.write("n=" + (++n) + body + "\n");
            w.close();
            w = null;
        } catch (Throwable t) {
        } finally {
            if (w != null) try { w.close(); } catch (Throwable t) {}
        }
    }

    private static String round1(float f) {
        long v = Math.round(f * 10f);
        StringBuffer s = new StringBuffer();
        if (v < 0) { s.append('-'); v = -v; }
        return s.append(v / 10).append('.').append(v % 10).toString();
    }

    private static String round2(float f) {
        long v = Math.round(f * 100f);
        StringBuffer s = new StringBuffer();
        if (v < 0) { s.append('-'); v = -v; }
        s.append(v / 100).append('.');
        long r = v % 100;
        if (r < 10) s.append('0');
        return s.append(r).toString();
    }

    final class VsListener extends DSICarVehicleStatesAdapter {
        public void updateDynamicVehicleInfoHighFrequent(DynamicVehicleInfoHighFrequent d, int s) {
            if (d == null || s != 1) return;
            power = d.currentOutputPower;
            torque = d.currentTorque;
            rpm = d.engineSpeed;
            // longitudinal in m/s^2, lateral already in g (same as the stock SportHmi G-meter)
            // signed: longitudinal + accelerating / - braking; lateral sign as the car reports it
            float gx = d.longitudinalAcceleration / 9.81f, gy = d.lateralAcceleration;
            float px = gxS, py = gyS;
            gxS = Float.isNaN(px) ? gx : px + 0.5f * (gx - px);
            gyS = Float.isNaN(py) ? gy : py + 0.5f * (gy - py);
        }
        public void updateDynamicVehicleInfoMidFrequent(DynamicVehicleInfoMidFrequent d, int s) {
            if (d == null || s != 1) return;
            gear = d.currentGear;
            lever = d.automaticGearShiftTransMode;
            gbt = d.gearboxOilTemperature;
        }
    }

    final class KoListener extends DSICarKombiAdapter {
        public void updateBCOilTemperatureValue(CarBCTemperature t, int s) {
            if (t == null || s != 1) { oil = Float.NaN; return; }
            oil = t.temperatureValue;
            if (t.temperatureUnit != loggedOilUnit) {
                loggedOilUnit = t.temperatureUnit;
                Log.log("dash: oil " + t.temperatureValue + " unit=" + t.temperatureUnit + " state=" + t.state);
            }
        }
        public void updateBCCoolantTemperature(CarBCTemperature t, int s) {
            if (t == null || s != 1) { coolant = Float.NaN; return; }
            coolant = t.temperatureValue;
        }
        public void updateBCCurrentConsumption1(CarBCConsumption c, int s) {
            if (c == null || s != 1) { consumption = Float.NaN; return; }
            consumption = c.consumptionValue;
            consUnit = c.consumptionUnit;
            if (c.consumptionUnit != loggedConsUnit) {
                loggedConsUnit = c.consumptionUnit;
                Log.log("dash: consumption " + c.consumptionValue + " unit=" + c.consumptionUnit + " state=" + c.state);
            }
        }
        public void updateBCShortTermAverageConsumption1(CarBCConsumption c, int s) {
            if (c == null || s != 1) { tripAvg = Float.NaN; return; }
            tripAvg = c.consumptionValue;
            tripUnit = c.consumptionUnit;
            if (c.consumptionUnit != loggedTripUnit) {
                loggedTripUnit = c.consumptionUnit;
                Log.log("dash: trip avg " + c.consumptionValue + " unit=" + c.consumptionUnit + " state=" + c.state);
            }
        }
        public void updateBCTankLevel1(BCTankLevel t, int s) {
            if (t == null || s != 1) { tank = Integer.MIN_VALUE; return; }
            tank = t.tankLevelValue;
        }
    }
}
