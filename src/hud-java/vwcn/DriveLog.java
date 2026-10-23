package vwcn;

import java.io.File;
import java.io.FileWriter;
import java.util.Calendar;

/**
 * v18: drive log. While /tmp/vwcn_log_on exists (created by the VW_HUD_2 boot loop when a USB drive with a
 * "drive_logs" folder is plugged in), every vehicle-states high-frequency callback (8-10 per second) adds one
 * CSV line. Lines are buffered in memory and written by the Dash thread into 10-second chunk files
 * /tmp/drv_<session>_<seq>.csv; the boot loop (root) appends every finished chunk to
 * <usb>/drive_logs/drive_<session>.csv and deletes it. /tmp/drv_cur names the chunk being written, and is
 * updated before that chunk is created, so the boot loop never takes a chunk that is still growing.
 * The Java process does not need write access to the USB drive (the loop does the mount -uw and the copy).
 * At most MAX_PENDING chunks wait in /tmp (RAM); beyond that, data is dropped until the loop catches up.
 */
final class DriveLog {
    static final String CUR = "/tmp/drv_cur";
    static final long CHUNK_MS = 10000L;
    static final int MAX_PENDING = 60;
    static final String COLUMNS = "t_ms,spd_kmh,real_spd_kmh,rpm,power_kw,torque_nm,pedal_pct,brake_bar,steer_deg,yaw_dps,"
        + "ax_ms2,ay_g,lever,gear,gearbox_c,oil_c,coolant_c,cons,cons_unit,tank_pct,outside_c,range_km,"
        + "wheel1_kmh,wheel2_kmh,wheel3_kmh,wheel4_kmh";

    private static final StringBuffer buf = new StringBuffer(8192);
    private static String session, chunk;
    private static long chunkStart;
    private static int seq;
    private static boolean droppedLogged;

    private DriveLog() {
    }

    static void add(String line) {
        synchronized (buf) {
            if (buf.length() < 200000) buf.append(line).append('\n');
        }
    }

    /** called by the Dash thread about every 200 ms while logging */
    static void flush(long now) {
        String data;
        synchronized (buf) {
            if (buf.length() == 0) return;
            data = buf.toString();
            buf.setLength(0);
        }
        if (chunk == null || now - chunkStart >= CHUNK_MS) {
            if (pending() >= MAX_PENDING) {
                if (!droppedLogged) { droppedLogged = true; Log.log("drivelog: " + MAX_PENDING + " chunks waiting, dropping data"); }
                return;
            }
            droppedLogged = false;
            newChunk(now);
        }
        append(chunk, data);
    }

    /** logging switched off (drive removed): the next start opens a new session file */
    static void stop() {
        synchronized (buf) { buf.setLength(0); }
        if (session != null) Log.log("drivelog: stop " + session + " after " + seq + " chunks");
        session = null;
        chunk = null;
        seq = 0;
    }

    private static void newChunk(long now) {
        boolean first = session == null;
        if (first) {
            session = stamp();
            seq = 0;
            Log.log("drivelog: start " + session);
        }
        seq++;
        String s = String.valueOf(seq);
        while (s.length() < 5) s = "0" + s;
        chunk = "/tmp/drv_" + session + "_" + s + ".csv";
        chunkStart = now;
        write(CUR, chunk + "\n", false);   // before the chunk exists: the copier must not take it
        if (first) append(chunk, "# VWClusterNav v18 drive log, session " + session + " (head-unit clock), start t_ms=" + now
            + "; empty = not available; ay in g, ax in m/s2; cons_unit 0=L/100km 1=km/L 2=L/h\n" + COLUMNS + "\n");
    }

    private static int pending() {
        String[] l = new File("/tmp").list();
        int n = 0;
        if (l != null) for (int i = 0; i < l.length; i++) if (l[i].startsWith("drv_") && l[i].endsWith(".csv")) n++;
        return n;
    }

    private static String stamp() {
        Calendar c = Calendar.getInstance();
        return c.get(Calendar.YEAR) + two(c.get(Calendar.MONTH) + 1) + two(c.get(Calendar.DAY_OF_MONTH)) + "-"
            + two(c.get(Calendar.HOUR_OF_DAY)) + two(c.get(Calendar.MINUTE)) + two(c.get(Calendar.SECOND));
    }

    private static String two(int v) {
        return v < 10 ? "0" + v : String.valueOf(v);
    }

    private static void append(String f, String data) {
        write(f, data, true);
    }

    private static void write(String f, String data, boolean append) {
        FileWriter w = null;
        try {
            w = new FileWriter(f, append);
            w.write(data);
        } catch (Throwable t) {
        } finally {
            if (w != null) try { w.close(); } catch (Throwable t) {}
        }
    }
}
