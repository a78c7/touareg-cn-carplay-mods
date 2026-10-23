package vwcn;

import de.vw.mib.asl.framework.api.dsiproxy.DSIProxy;
import de.vw.mib.asl.framework.api.dsiproxy.DSIProxyFactory;
import java.io.File;
import java.io.FileWriter;
import org.dsi.ifc.base.DSIListener;
import org.dsi.ifc.carvehiclestates.DSICarVehicleStates;
import org.dsi.ifc.carvehiclestates.DSICarVehicleStatesListener;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoHighFrequent;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoHighFrequentViewOptions;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoMidFrequent;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoMidFrequentViewOptions;
import org.dsi.ifc.carvehiclestates.DynamicVehicleInfoSCR;
import org.dsi.ifc.carvehiclestates.KeyData;
import org.dsi.ifc.carvehiclestates.OilLevelData;
import org.dsi.ifc.carvehiclestates.SemiStaticDataViewOptions;
import org.dsi.ifc.carvehiclestates.SemiStaticVehicleData;
import org.dsi.ifc.carvehiclestates.VehicleInfoViewOptions;
import org.dsi.ifc.global.CarViewOption;

/**
 * Read-only probe: while /tmp/perf_on exists, subscribes to DSICarVehicleStates
 * (view options 12/13/16, data 14/15/17) and logs to /tmp/perf.log.
 * Latest high-frequency sample also goes to /tmp/perf.state (overwritten).
 */
public final class Perf implements Runnable, DSICarVehicleStatesListener {
    static volatile boolean stop;
    static final String ON = "/tmp/perf_on";
    static final String LOG = "/tmp/perf.log";
    static final String STATE = "/tmp/perf.state";
    static final long LOG_MAX = 3000000L;
    static final int[] ATTRS = new int[] {12, 13, 16, 14, 15, 17};

    private DSICarVehicleStates dsi;
    private boolean subscribed;
    private int hfCount, hfFull, mfCount;
    private long lastHfLine, lastState, lastMf, rateStart;
    private int rateCount;

    public static void start() {
        Thread t = new Thread(new Perf(), "vwcn-perf");
        t.setDaemon(true);
        t.start();
    }

    public void run() {
        try {
            Thread.sleep(30000);
            Log.log("perf: thread ready, waiting for " + ON);
            while (!stop) {
                boolean on = new File(ON).exists();
                if (on && !subscribed) subscribe();
                else if (!on && subscribed) unsubscribe();
                Thread.sleep(2000);
            }
            if (subscribed) unsubscribe();
        } catch (Throwable t) {
            Log.log("perf error: " + t);
        }
    }

    private void subscribe() {
        try {
            if (dsi == null) {
                DSIProxy p = DSIProxyFactory.getDSIProxyAPI().getDSIProxy();
                dsi = (DSICarVehicleStates) p.getService(null, DSICarVehicleStates.class);
            }
            hfCount = 0; hfFull = 0; mfCount = 0; rateStart = System.currentTimeMillis(); rateCount = 0;
            dsi.setNotification(ATTRS, (DSIListener) this);
            subscribed = true;
            plog("subscribed attrs 12,13,16,14,15,17 dsi=" + dsi);
        } catch (Throwable t) {
            plog("subscribe failed: " + t);
            Log.log("perf subscribe failed: " + t);
        }
    }

    private void unsubscribe() {
        try {
            dsi.clearNotification(ATTRS, (DSIListener) this);
        } catch (Throwable t) {
            plog("unsubscribe failed: " + t);
        }
        subscribed = false;
        plog("unsubscribed, hf updates=" + hfCount + " mf updates=" + mfCount);
    }

    // ---- listener ----
    public void updateDynamicVehicleInfoHighFrequentViewOptions(DynamicVehicleInfoHighFrequentViewOptions v, int s) {
        StringBuffer b = new StringBuffer("VO12 valid=").append(s);
        if (v != null) {
            b.append(" accLong=").append(st(v.longitudinalAcceleration))
             .append(" accLat=").append(st(v.lateralAcceleration))
             .append(" rpm=").append(st(v.engineSpeed))
             .append(" powermeter=").append(st(v.powermeter))
             .append(" powerKW=").append(st(v.currentOutputPower))
             .append(" torqueNm=").append(st(v.currentTorque))
             .append(" boostAbs=").append(st(v.absChargingAirPressure))
             .append(" boostRel=").append(st(v.relChargingAirPressure))
             .append(" pedal=").append(st(v.acceleratorPosition))
             .append(" brake=").append(st(v.brakePressure))
             .append(" wheelAngle=").append(st(v.wheelAngle))
             .append(" yaw=").append(st(v.yawRate))
             .append(" speed=").append(st(v.vehicleSpeed))
             .append(" realSpeed=").append(st(v.realVehicleSpeed))
             .append(" awdTq=").append(st(v.allWheelDriveTorque))
             .append(" relAwdTq=").append(st(v.relAllWheelDriveTorque))
             .append(" tqFL/FR/RL/RR=").append(st(v.torqueDistributionFrontLeft)).append('/')
             .append(st(v.torqueDistributionFrontRight)).append('/')
             .append(st(v.torqueDistributionRearLeft)).append('/')
             .append(st(v.torqueDistributionRearRight))
             .append(" espOver/Under=").append(st(v.espOversteering)).append('/').append(st(v.espUndersteering))
             .append(" wheelVel=").append(st(v.espWheelVelocityFrontLeft))
             .append(" shiftLight=").append(st(v.shiftLight));
        }
        plog(b.toString());
        plog("VO12 raw " + v);
    }

    public void updateDynamicVehicleInfoHighFrequent(DynamicVehicleInfoHighFrequent d, int s) {
        hfCount++;
        rateCount++;
        long now = System.currentTimeMillis();
        if (now - rateStart >= 10000) {
            plog("HF rate " + (rateCount * 1000L / (now - rateStart)) + "/s");
            rateStart = now; rateCount = 0;
        }
        if (d == null) return;
        if (hfFull < 3) { hfFull++; plog("HF raw valid=" + s + " " + d); }
        if (now - lastHfLine < 200 && now - lastState < 200) return;
        StringBuffer b = new StringBuffer();
        b.append("spd=").append(d.vehicleSpeed == null ? -1f : d.vehicleSpeed.speedValue)
         .append(" rpm=").append(d.engineSpeed)
         .append(" P=").append(d.currentOutputPower)
         .append(" Tq=").append(d.currentTorque)
         .append(" ax=").append(d.longitudinalAcceleration)
         .append(" ay=").append(d.lateralAcceleration)
         .append(" boostAbs=").append(d.absChargingAirPressure)
         .append(" boostRel=").append(d.relChargingAirPressure)
         .append(" pedal=").append(d.acceleratorPosition)
         .append(" brake=").append(d.brakePressure)
         .append(" wa=").append(d.wheelAngle)
         .append(" yaw=").append(d.yawRate)
         .append(" pm=").append(d.powermeter)
         .append(" awd=").append(d.allWheelDriveTorque)
         .append(" rawd=").append(d.relAllWheelDriveTorque)
         .append(" td=").append(d.torqueDistributionFrontLeft).append(',').append(d.torqueDistributionFrontRight)
         .append(',').append(d.torqueDistributionRearLeft).append(',').append(d.torqueDistributionRearRight)
         .append(" over=").append(d.espOversteering).append(" under=").append(d.espUndersteering);
        String line = b.toString();
        if (now - lastState >= 200) { lastState = now; writeState(now + " " + line); }
        if (now - lastHfLine >= 200) { lastHfLine = now; plog("HF " + line); }
    }

    public void updateDynamicVehicleInfoMidFrequentViewOptions(DynamicVehicleInfoMidFrequentViewOptions v, int s) {
        plog("VO13 valid=" + s + " " + v);
    }

    public void updateDynamicVehicleInfoMidFrequent(DynamicVehicleInfoMidFrequent d, int s) {
        mfCount++;
        long now = System.currentTimeMillis();
        if (mfCount <= 2 || now - lastMf >= 5000) { lastMf = now; plog("MF valid=" + s + " " + d); }
    }

    public void updateSemiStaticVehicleDataViewOptions(SemiStaticDataViewOptions v, int s) {
        plog("VO16 valid=" + s + " " + v);
    }

    public void updateSemiStaticVehicleData(SemiStaticVehicleData d, int s) {
        plog("SEMI valid=" + s + " " + d);
    }

    public void asyncException(int a, String b, int c) { plog("asyncException " + a + " " + b + " " + c); }
    public void updateOilLevelViewOption(CarViewOption v, int s) {}
    public void updateOilLevelData(OilLevelData d, int s) {}
    public void updateVINViewOption(CarViewOption v, int s) {}
    public void updateVINData(String d, int s) {}
    public void updateKeyViewOption(CarViewOption v, int s) {}
    public void updateKeyData(KeyData d, int s) {}
    public void updateDrvSchoolSystem(boolean d, int s) {}
    public void updateVehicleInfoViewOptions(VehicleInfoViewOptions v, int s) {}
    public void updateDynamicVehicleInfoSCR(DynamicVehicleInfoSCR d, int s) {}

    // ---- helpers ----
    private static String st(CarViewOption o) {
        if (o == null) return "null";
        return String.valueOf(o.getState());
    }

    private static synchronized void plog(String msg) {
        FileWriter w = null;
        try {
            File f = new File(LOG);
            if (f.exists() && f.length() > LOG_MAX) return;
            w = new FileWriter(LOG, true);
            w.write(System.currentTimeMillis() + " " + msg + "\n");
        } catch (Throwable t) {
        } finally {
            if (w != null) try { w.close(); } catch (Throwable t) {}
        }
    }

    private static void writeState(String line) {
        FileWriter w = null;
        try {
            w = new FileWriter(STATE, false);
            w.write(line + "\n");
        } catch (Throwable t) {
        } finally {
            if (w != null) try { w.close(); } catch (Throwable t) {}
        }
    }
}
