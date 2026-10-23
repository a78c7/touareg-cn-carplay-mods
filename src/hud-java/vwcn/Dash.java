package vwcn;

import de.vw.mib.asl.framework.api.dsiproxy.DSIProxy;
import de.vw.mib.asl.framework.api.dsiproxy.DSIProxyFactory;
import de.vw.mib.asl.internal.car.abstractclasses.dsiadapter.DSICarKombiAdapter;
import de.vw.mib.asl.internal.car.abstractclasses.dsiadapter.DSICarVehicleStatesAdapter;
import java.io.File;
import java.io.FileWriter;
import org.dsi.ifc.base.DSIListener;
import org.dsi.ifc.carkombi.BCCycleGeneralData;
import org.dsi.ifc.carkombi.BCLongTermGeneralData;
import org.dsi.ifc.carkombi.BCShortTermGeneralData;
import org.dsi.ifc.carkombi.BCTankLevel;
import org.dsi.ifc.carkombi.DSICarKombi;
import org.dsi.ifc.carvehiclestates.DSICarVehicleStates;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoHighFrequent;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoMidFrequent;
import org.dsi.ifc.global.CarBCConsumption;
import org.dsi.ifc.global.CarBCCurrentRange;
import org.dsi.ifc.global.CarBCDistance;
import org.dsi.ifc.global.CarBCSpeed;
import org.dsi.ifc.global.CarBCTemperature;
import org.dsi.ifc.global.CarBCTime;

/**
 * v16/v17: data for the cluster info box (drawn by cl_render on the CarPlay map).
 * Subscribes to vehicle states (power, torque, acceleration, gear, gearbox oil temperature) and the kombi
 * board computer (engine oil and coolant temperature, current consumption, tank level) only while the
 * CarPlay map is held on the cluster (Rebind.hold != null), and writes one line to /tmp/dash.state about
 * 5 times a second: "n=<counter> p=<kW> tq=<Nm> gx=<long g> gy=<lat g> gm=<lever> gc=<gear> gbt=<C> oil=<C> cool=<C>
 * cv=<current consumption> cu=<unit> tank=<%> ...". Board computer (VW names), per memory average + unit, distance,
 * average speed, driving time in minutes: 自启动起 ca au td ts tt, 自上次加油起 cr cru rd rs rt, 累计 cl clu ld ls lt.
 * Keys whose value is not available are left out. Off switch: /tmp/vwcn_dash_off.
 */
public final class Dash implements Runnable {
    static volatile boolean stop;
    static final String STATE = "/tmp/dash.state";
    static final String TMP = "/tmp/dash.state.new";
    static final String OFF = "/tmp/vwcn_dash_off";
    static final int[] VS_ATTRS = new int[] {14, 15};
    // kombi: 4 view options, 6 current consumption 1, 8 current range 1, 11 short-term ("since start") average
    // consumption 1, 13 short-term general (distance, average speed, time), 14/16 long-term ("累计") average
    // consumption 1 / general, 17/19 cycle ("since refuel") average consumption 1 / general, 23 tank level 1,
    // 63 coolant, 64 oil temperature value, 65 outside temperature
    static final int[] KO_ATTRS = new int[] {4, 6, 8, 11, 13, 14, 16, 17, 19, 23, 63, 64, 65};
    // board computer memories: 0 自启动起 (short term), 1 自上次加油起 (cycle), 2 累计 (long term)
    static final String[][] BK = new String[][] {
        {" ca=", " au=", " td=", " ts=", " tt="}, {" cr=", " cru=", " rd=", " rs=", " rt="}, {" cl=", " clu=", " ld=", " ls=", " lt="}};

    private DSICarVehicleStates vs;
    private DSICarKombi ko;
    private DSIListener vsl, kol;
    private boolean subscribed;
    private long n;
    private String lastLine = "";
    private long lastWrite;

    // latest values (written by DSI callbacks, read by the writer thread)
    volatile float power = Float.NaN, gxS = Float.NaN, gyS = Float.NaN, consumption = Float.NaN;
    volatile int rpm = Integer.MIN_VALUE, pedal = Integer.MIN_VALUE, steer = Integer.MIN_VALUE, range = Integer.MIN_VALUE;
    volatile float speed = Float.NaN, brake = Float.NaN, outside = Float.NaN;
    volatile int torque = Integer.MIN_VALUE, lever = Integer.MIN_VALUE, gear = Integer.MIN_VALUE;
    volatile int gbt = Integer.MIN_VALUE, consUnit = Integer.MIN_VALUE, tank = Integer.MIN_VALUE;
    volatile float oil = Float.NaN, coolant = Float.NaN;
    // board computer, index = memory (BK)
    final float[] bcAvg = new float[] {Float.NaN, Float.NaN, Float.NaN};
    final int[] bcUnit = new int[] {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE};
    final float[] bcDist = new float[] {Float.NaN, Float.NaN, Float.NaN};
    final float[] bcSpeed = new float[] {Float.NaN, Float.NaN, Float.NaN};
    final int[] bcMin = new int[] {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE};
    private final int[] loggedBcUnit = new int[] {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE};
    private final boolean[] loggedBcGeneral = new boolean[3];
    private int loggedConsUnit = Integer.MIN_VALUE, loggedOilUnit = Integer.MIN_VALUE;

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
        StringBuffer b = new StringBuffer(320);
        if (!Float.isNaN(power)) b.append(" p=").append(round1(power));
        if (torque != Integer.MIN_VALUE) b.append(" tq=").append(torque);
        if (rpm != Integer.MIN_VALUE) b.append(" rpm=").append(rpm);
        if (!Float.isNaN(speed)) b.append(" spd=").append(round1(speed));
        if (pedal != Integer.MIN_VALUE) b.append(" ped=").append(pedal);
        if (!Float.isNaN(brake)) b.append(" brk=").append(round1(brake));
        if (steer != Integer.MIN_VALUE) b.append(" str=").append(steer);
        if (!Float.isNaN(gxS)) b.append(" gx=").append(round2(gxS));
        if (!Float.isNaN(gyS)) b.append(" gy=").append(round2(gyS));
        if (lever != Integer.MIN_VALUE) b.append(" gm=").append(lever).append(" gc=").append(gear);
        if (gbt != Integer.MIN_VALUE) b.append(" gbt=").append(gbt);
        if (!Float.isNaN(oil)) b.append(" oil=").append(Math.round(oil));
        if (!Float.isNaN(coolant)) b.append(" cool=").append(Math.round(coolant));
        if (!Float.isNaN(consumption)) b.append(" cv=").append(round1(consumption)).append(" cu=").append(consUnit);
        if (tank != Integer.MIN_VALUE) b.append(" tank=").append(tank);
        if (!Float.isNaN(outside)) b.append(" out=").append(Math.round(outside));
        if (range != Integer.MIN_VALUE) b.append(" rng=").append(range);
        for (int m = 0; m < 3; m++) {
            if (!Float.isNaN(bcAvg[m])) b.append(BK[m][0]).append(round1(bcAvg[m])).append(BK[m][1]).append(bcUnit[m]);
            if (!Float.isNaN(bcDist[m])) b.append(BK[m][2]).append(round1(bcDist[m]));
            if (!Float.isNaN(bcSpeed[m])) b.append(BK[m][3]).append(round1(bcSpeed[m]));
            if (bcMin[m] != Integer.MIN_VALUE) b.append(BK[m][4]).append(bcMin[m]);
        }
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

    private void avg(int m, CarBCConsumption c, int s) {
        if (c == null || s != 1) { bcAvg[m] = Float.NaN; return; }
        bcAvg[m] = c.consumptionValue;
        bcUnit[m] = c.consumptionUnit;
        if (c.consumptionUnit != loggedBcUnit[m]) {
            loggedBcUnit[m] = c.consumptionUnit;
            Log.log("dash: bc" + m + " avg " + c.consumptionValue + " unit=" + c.consumptionUnit + " state=" + c.state);
        }
    }

    private void general(int m, CarBCDistance d, CarBCSpeed v, CarBCTime t, int s) {
        if (s != 1) { bcDist[m] = Float.NaN; bcSpeed[m] = Float.NaN; bcMin[m] = Integer.MIN_VALUE; return; }
        bcDist[m] = d == null ? Float.NaN : (float) d.distanceValue;
        bcSpeed[m] = v == null ? Float.NaN : v.speedValue;
        bcMin[m] = t == null ? Integer.MIN_VALUE : t.timeValue;
        if (!loggedBcGeneral[m]) {   // first value per memory: log the units/states once (unverified on the car)
            loggedBcGeneral[m] = true;
            Log.log("dash: bc" + m + " general dist=" + (d == null ? "null" : d.distanceValue + " u=" + d.distanceUnit + " st=" + d.distanceValueState)
                + " speed=" + (v == null ? "null" : v.speedValue + " u=" + v.speedUnit + " st=" + v.speedValueState)
                + " time=" + (t == null ? "null" : t.timeValue + " st=" + t.state));
        }
    }

    final class VsListener extends DSICarVehicleStatesAdapter {
        public void updateDynamicVehicleInfoHighFrequent(DynamicVehicleInfoHighFrequent d, int s) {
            if (d == null || s != 1) return;
            power = d.currentOutputPower;
            torque = d.currentTorque;
            rpm = d.engineSpeed;
            speed = d.vehicleSpeed == null ? Float.NaN : d.vehicleSpeed.speedValue;
            pedal = d.acceleratorPosition;
            brake = d.brakePressure;
            steer = d.wheelAngle;
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
        public void updateBCShortTermAverageConsumption1(CarBCConsumption c, int s) { avg(0, c, s); }
        public void updateBCCycleAverageConsumption1(CarBCConsumption c, int s) { avg(1, c, s); }
        public void updateBCLongTermAverageConsumption1(CarBCConsumption c, int s) { avg(2, c, s); }
        public void updateBCShortTermGeneral(BCShortTermGeneralData g, int s) {
            general(0, g == null ? null : g.distance, g == null ? null : g.speed, g == null ? null : g.timeValue, s);
        }
        public void updateBCCycleGeneral(BCCycleGeneralData g, int s) {
            general(1, g == null ? null : g.distance, g == null ? null : g.speed, g == null ? null : g.timeValue, s);
        }
        public void updateBCLongTermGeneral(BCLongTermGeneralData g, int s) {
            general(2, g == null ? null : g.distance, g == null ? null : g.speed, g == null ? null : g.timeValue, s);
        }
        public void updateBCOutsideTemperature(CarBCTemperature t, int s) {
            if (t == null || s != 1) { outside = Float.NaN; return; }
            outside = t.temperatureValue;
        }
        public void updateBCCurrentRange1(CarBCCurrentRange r, int s) {
            if (r == null || s != 1) { range = Integer.MIN_VALUE; return; }
            range = r.currentRangeValue;
        }
        public void updateBCTankLevel1(BCTankLevel t, int s) {
            if (t == null || s != 1) { tank = Integer.MIN_VALUE; return; }
            tank = t.tankLevelValue;
        }
    }
}
