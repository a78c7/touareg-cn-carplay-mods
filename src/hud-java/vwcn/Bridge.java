package vwcn;

import de.vw.mib.asl.api.navbap.ASLNavBAPAPI;
import de.vw.mib.asl.api.navbap.ASLNavBAPFactory;
import de.vw.mib.asl.framework.internal.framework.GenericCollector;
import de.vw.mib.asl.framework.internal.framework.ServiceManager;
import generated.de.vw.mib.asl.internal.ListManager;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.util.StringTokenizer;

public final class Bridge implements Runnable {
    private static final String STATE = "/tmp/cluster_cp.state";
    private static final String OFF_FLAG = "/tmp/vwcn_off";
    private static final String[] USB_OFF = new String[] {
        "/fs/usb0_0/vwcn_off", "/fs/usb1_0/vwcn_off", "/fs/sda0/vwcn_off", "/fs/sdb0/vwcn_off" };
    private static final int DP_RG_STATE = 732;
    /** BAP NavSD CurrentPositionInfo (FctID 19) reads ASL string datapool 738 (mqbpq CurrentPositionInfoStage). */
    private static final int DP_CUR_POSITION = 738;
    private static final String TRIP_OFF = "/tmp/vwcn_trip_off";
    private static final String TRIP_STOPOVER = "/tmp/vwcn_trip_stopover";
    private static final String POS_OFF = "/tmp/vwcn_pos_off";
    private static final int LIST_MANEUVER = 751;
    private static final int LIST_MANEUVER2 = 838;
    private static final long START_DELAY_MS = 30000L;
    private static final long POLL_MS = 300L;
    private static final long HEARTBEAT_MS = 2000L;

    private static Thread thread = null;
    private static volatile boolean stop = false;

    private boolean active = false;
    private int lastMain = -999;
    private int lastDir = -999;
    private int lastDist = -999;
    private int lastBarOn = -1;
    private int lastBar = -1;
    private String lastStreet = null;
    private String lastSign = null;
    private int lastIapIdx = -1;
    private long lastHeartbeat = 0L;
    private long lastErrLog = 0L;
    private String lastStateLine = "";
    private int notLive = 0;
    private String lastLaneKey = "";
    private long lastTripDist = -2;
    private int lastTripMin = -2;
    private String lastPos = null;
    private static final int MAX_LANES = 8;

    static synchronized void startThread() {
        if (thread != null && thread.isAlive()) {
            return;
        }
        stop = false;
        thread = new Thread(new Bridge(), "VWClusterNav");
        thread.setDaemon(true);
        try {
            thread.setPriority(4);
        } catch (Throwable t) {
        }
        thread.start();
    }

    static synchronized void stopThread() {
        stop = true;
        if (thread != null) {
            thread.interrupt();
        }
        thread = null;
    }

    public void run() {
        Log.log("thread running, waiting 30s for HMI");
        try {
            Thread.sleep(START_DELAY_MS);
        } catch (InterruptedException e) {
            return;
        }
        // Link check: touch every HMI class once and report, so the log shows linkage problems.
        try {
            Log.log("link check: aslPropertyManager=" + (ServiceManager.aslPropertyManager != null)
                + " navbap=" + (ASLNavBAPFactory.getNavBAPApi() != null)
                + " list751=" + (ListManager.getGenericASLList(LIST_MANEUVER) != null)
                + " list838=" + (ListManager.getGenericASLList(LIST_MANEUVER2) != null));
        } catch (Throwable t) {
            Log.log("link check error: " + t);
        }
        Log.log("polling " + STATE);
        while (!stop) {
            try {
                loopOnce();
            } catch (Throwable t) {
                long now = System.currentTimeMillis();
                if (now - lastErrLog > 10000L) {
                    lastErrLog = now;
                    Log.log("loop error: " + t);
                }
            }
            try {
                Thread.sleep(POLL_MS);
            } catch (InterruptedException e) {
                break;
            }
        }
        try {
            if (active) {
                deactivate("thread stop");
            }
        } catch (Throwable t) {
        }
        Log.log("thread stopped");
    }

    private boolean disabled() {
        if (new File(OFF_FLAG).exists()) {
            return true;
        }
        for (int i = 0; i < USB_OFF.length; i++) {
            if (new File(USB_OFF[i]).exists()) {
                return true;
            }
        }
        return false;
    }

    private void loopOnce() throws Exception {
        if (disabled()) {
            if (active) {
                deactivate("disabled by vwcn_off flag");
            }
            return;
        }
        String content = readFile(STATE);
        if (content == null || content.length() == 0 || content.indexOf("current_slot:") < 0) {
            if (active && ++notLive >= 10) {
                deactivate("state file empty/missing");
            }
            return;
        }

        boolean session = false;
        int routeState = -1;
        int srcRg = -1;
        int dist = -1;
        int slotCount = 0;
        int curSlot = -1;
        int type = -1;
        int drivingSide = 0;
        int junction = 0;
        int exitAngle = 0;
        int iapIdx = -1;
        String road = "";
        String desc = "";
        String exitInfo = "";
        String curRoad = "";
        long distRem = -1;
        long timeRem = -1;
        boolean laneActive = false;
        int laneCount = 0;
        int[] lanePos = new int[MAX_LANES];
        int[] laneDir = new int[MAX_LANES];
        int[] laneStatus = new int[MAX_LANES];
        String[] laneAngles = new String[MAX_LANES];

        StringTokenizer st = new StringTokenizer(content, "\n");
        while (st.hasMoreTokens()) {
            String line = st.nextToken();
            if (line.length() == 0 || line.charAt(0) == '@') {
                continue;
            }
            // format: key:type:value
            int c1 = line.indexOf(':');
            if (c1 < 0) {
                continue;
            }
            int c2 = line.indexOf(':', c1 + 1);
            if (c2 < 0) {
                continue;
            }
            String key = line.substring(0, c1);
            String val = line.substring(c2 + 1);
            if (key.equals("session_active")) {
                session = val.startsWith("true") || val.equals("1");
            } else if (key.equals("route_state")) {
                routeState = toInt(val, -1);
            } else if (key.equals("source_supports_rg")) {
                srcRg = toInt(val, -1);
            } else if (key.equals("dist_to_maneuver")) {
                dist = toInt(val, -1);
            } else if (key.equals("slot_count")) {
                slotCount = toInt(val, 0);
            } else if (key.equals("current_slot")) {
                curSlot = toInt(val, -1);
            } else if (key.equals("current_road")) {
                curRoad = val;
            } else if (key.equals("distance_remaining")) {
                distRem = toLong(val, -1);
            } else if (key.equals("time_remaining")) {
                timeRem = toLong(val, -1);
            } else if (key.equals("slot_0_type")) {
                type = toInt(val, -1);
            } else if (key.equals("slot_0_iap_idx")) {
                iapIdx = toInt(val, -1);
            } else if (key.equals("slot_0_driving_side")) {
                drivingSide = toInt(val, 0);
            } else if (key.equals("slot_0_junction_type")) {
                junction = toInt(val, 0);
            } else if (key.equals("slot_0_exit_angle")) {
                exitAngle = toInt(val, 0);
            } else if (key.equals("slot_0_road")) {
                road = val;
            } else if (key.equals("slot_0_description")) {
                desc = val;
            } else if (key.equals("slot_0_exit_info")) {
                exitInfo = val;
            } else if (key.equals("lane_active")) {
                laneActive = val.startsWith("true") || val.equals("1");
            } else if (key.equals("lane_count")) {
                laneCount = toInt(val, 0);
            } else if (key.startsWith("lane_") && key.length() > 7) {
                int us = key.indexOf('_', 5);
                if (us > 5) {
                    int li = toInt(key.substring(5, us), -1);
                    String f = key.substring(us + 1);
                    if (li >= 0 && li < MAX_LANES) {
                        if (f.equals("pos")) {
                            lanePos[li] = toInt(val, li);
                        } else if (f.equals("dir")) {
                            laneDir[li] = toInt(val, 0);
                        } else if (f.equals("status")) {
                            laneStatus[li] = toInt(val, 0);
                        } else if (f.equals("angles")) {
                            laneAngles[li] = val;
                        }
                    }
                }
            }
        }

        boolean live = session && routeState >= 1 && routeState != 2 && srcRg != 0;
        String stateLine = "state session=" + session + " route=" + routeState + " srcRg=" + srcRg
            + " slots=" + slotCount + " cur=" + curSlot + " type=" + type + " live=" + live;
        if (!stateLine.equals(lastStateLine)) {
            lastStateLine = stateLine;
            Log.log(stateLine);
        }
        if (!live) {
            if (active && ++notLive >= 5) {
                deactivate("route ended");
            }
            return;
        }
        notLive = 0;

        int main = Mapper.FOLLOW_STREET;
        int dir = 0;
        if (slotCount > 0 && curSlot >= 0 && type >= 0) {
            int[] m = Mapper.map(type, exitAngle, junction, drivingSide);
            main = m[0];
            dir = m[1];
        }
        if (iapIdx != lastIapIdx) {
            lastIapIdx = iapIdx;
        }

        String street = road;
        if (street == null || street.length() == 0) {
            street = desc;
        }
        if (street == null || street.length() == 0) {
            street = curRoad;
        }
        if (street == null) {
            street = "";
        }
        street = shortName(street);
        String sign = exitInfo == null ? "" : exitInfo;

        long now = System.currentTimeMillis();
        boolean beat = now - lastHeartbeat >= HEARTBEAT_MS;
        if (!active) {
            activate();
            beat = true;
        }
        if (beat || main != lastMain || dir != lastDir) {
            pushManeuver(main, dir);
        }
        if (beat || !street.equals(lastStreet) || !sign.equals(lastSign)) {
            pushTurnTo(street, sign);
        }
        if (dist >= 0) {
            int barOn = 0;
            int bar = 0;
            if (dist > 0 && dist <= 300) {
                barOn = 1;
                bar = (300 - dist) * 100 / 300;
                if (bar < 0) {
                    bar = 0;
                }
                if (bar > 100) {
                    bar = 100;
                }
            }
            if (beat || dist != lastDist || barOn != lastBarOn || bar != lastBar) {
                pushDistance(dist, barOn, bar);
            }
        }
        try {
            pushLanes(beat, laneActive, laneCount, lanePos, laneDir, laneStatus, laneAngles);
        } catch (Throwable t) {
            long now2 = System.currentTimeMillis();
            if (now2 - lastErrLog > 10000L) {
                lastErrLog = now2;
                Log.log("lanes error: " + t);
            }
        }
        try {
            pushTrip(beat, distRem, timeRem);
        } catch (Throwable t) {
            long now2 = System.currentTimeMillis();
            if (now2 - lastErrLog > 10000L) {
                lastErrLog = now2;
                Log.log("trip error: " + t);
            }
        }
        try {
            pushPosition(beat, curRoad);
        } catch (Throwable t) {
            long now2 = System.currentTimeMillis();
            if (now2 - lastErrLog > 10000L) {
                lastErrLog = now2;
                Log.log("position error: " + t);
            }
        }
        if (beat) {
            ServiceManager.aslPropertyManager.valueChangedInteger(DP_RG_STATE, 1);
            lastHeartbeat = now;
        }
    }

    /** v15: remaining distance (m, iAP2 RG TLV 0x07) and remaining time (s, TLV 0x06) to the cluster, the same
     *  calls the stock navigation makes (HsmTargetBap.updateDistanceTime): unit 255 = let the HMI pick km/m,
     *  updateBapTimeToDestination2(h, min, timeFormat) fills both remaining time and arrival time. */
    private void pushTrip(boolean beat, long distRem, long timeRem) {
        if (new File(TRIP_OFF).exists()) {
            return;
        }
        int min = (timeRem >= 0 && timeRem < 360000L) ? (int) ((timeRem + 59) / 60) : -1;
        long d = (distRem >= 0 && distRem < 20000000L) ? distRem : -1;
        if (!beat && d == lastTripDist && min == lastTripMin) {
            return;
        }
        ASLNavBAPAPI api = ASLNavBAPFactory.getNavBAPApi();
        if (min >= 0) {
            api.updateBapTimeToDestination2(min / 60, min % 60, 0);
        } else {
            api.updateBapTimeToDestination2(-1, -1, 0);
        }
        if (d >= 0) {
            api.updateBapDistanceToDestination((int) d, 255, new File(TRIP_STOPOVER).exists());
        }
        if (d != lastTripDist && (lastTripDist < 0 || Math.abs(d - lastTripDist) >= 500) || min != lastTripMin) {
            Log.log("trip dist=" + d + "m time=" + min + "min");
        }
        if (d != lastTripDist || min != lastTripMin) {
            lastTripDist = d;
            lastTripMin = min;
        }
    }

    /** v15: current road -> BAP CurrentPositionInfo (the cluster shows it when navigation status 733 == 0). */
    private void pushPosition(boolean beat, String curRoad) {
        if (new File(POS_OFF).exists()) {
            return;
        }
        String p = curRoad == null ? "" : curRoad.trim();
        if (!beat && p.equals(lastPos)) {
            return;
        }
        ServiceManager.aslPropertyManager.valueChangedString(DP_CUR_POSITION, p);
        if (!p.equals(lastPos)) {
            Log.log("position \"" + p + "\"");
        }
        lastPos = p;
    }

    private void activate() {
        Log.log("ACTIVATE: routeGuidanceState=1");
        ServiceManager.aslPropertyManager.valueChangedInteger(DP_RG_STATE, 1);
        active = true;
        lastMain = -999;
        lastDir = -999;
        lastDist = -999;
        lastBarOn = -1;
        lastBar = -1;
        lastStreet = null;
        lastSign = null;
    }

    private void deactivate(String why) {
        Log.log("DEACTIVATE (" + why + ")");
        active = false;
        try {
            ListManager.getGenericASLList(LIST_MANEUVER).updateList(new Object[0]);
            ListManager.getGenericASLList(LIST_MANEUVER2).updateList(new Object[0]);
        } catch (Throwable t) {
            Log.log("clear maneuver: " + t);
        }
        try {
            ASLNavBAPAPI api = ASLNavBAPFactory.getNavBAPApi();
            api.updateBapDistanceToNextManeuver(0, 0, false, 0);
            api.updateBapTurnToInfo("", "");
            api.updateBapLaneGuidance(new Lane[0], false);
            lastLaneKey = "";
            if (!new File(TRIP_OFF).exists()) {
                api.updateBapTimeToDestination2(-1, -1, 0);
            }
            lastTripDist = -2;
            lastTripMin = -2;
            if (!new File(POS_OFF).exists() && lastPos != null) {
                ServiceManager.aslPropertyManager.valueChangedString(DP_CUR_POSITION, "");
            }
            lastPos = null;
        } catch (Throwable t) {
            Log.log("clear navbap: " + t);
        }
        try {
            ServiceManager.aslPropertyManager.valueChangedInteger(DP_RG_STATE, 0);
        } catch (Throwable t) {
            Log.log("clear 732: " + t);
        }
        lastMain = -999;
        lastDir = -999;
        lastDist = -999;
        lastStreet = null;
        lastSign = null;
        lastIapIdx = -1;
        notLive = 0;
    }

    private void pushManeuver(int main, int dir) {
        GenericCollector gc = new GenericCollector();
        gc.setIntItem(0, main);
        gc.setIntItem(1, dir);
        gc.setIntItem(2, 0);
        gc.setStringItem(3, "");
        Object[] items = new GenericCollector[] { gc };
        ListManager.getGenericASLList(LIST_MANEUVER).updateList(items);
        ListManager.getGenericASLList(LIST_MANEUVER2).updateList(items);
        if (main != lastMain || dir != lastDir) {
            Log.log("maneuver main=" + main + " dir=" + dir);
        }
        lastMain = main;
        lastDir = dir;
    }

    private void pushLanes(boolean beat, boolean active, int count, int[] pos, int[] dir,
                           int[] status, String[] angles) {
        int n = active ? Math.min(count, MAX_LANES) : 0;
        // order lanes left to right by position
        int[] idx = new int[n];
        for (int i = 0; i < n; i++) idx[i] = i;
        for (int i = 1; i < n; i++) {
            int k = idx[i], j = i - 1;
            while (j >= 0 && pos[idx[j]] > pos[k]) { idx[j + 1] = idx[j]; j--; }
            idx[j + 1] = k;
        }
        Lane[] lanes = new Lane[n];
        StringBuffer key = new StringBuffer();
        key.append(n);
        for (int i = 0; i < n; i++) {
            int li = idx[i];
            int d = Lane.angleToBap(dir[li]);
            int g = status[li];
            if (g < 0) g = 0;
            if (g > 2) g = 2;
            lanes[i] = new Lane(d, g, Lane.sideStreets(angles[li], d));
            key.append('|').append(d).append(',').append(g).append(',').append(angles[li]);
        }
        String k = key.toString();
        if (!beat && k.equals(lastLaneKey)) {
            return;
        }
        ASLNavBAPFactory.getNavBAPApi().updateBapLaneGuidance(lanes, n > 0);
        if (!k.equals(lastLaneKey)) {
            Log.log("lanes " + k);
        }
        lastLaneKey = k;
    }

    private void pushTurnTo(String street, String sign) {
        ASLNavBAPFactory.getNavBAPApi().updateBapTurnToInfo(street, sign);
        if (!street.equals(lastStreet)) {
            Log.log("turnTo street=\"" + street + "\" sign=\"" + sign + "\"");
        }
        lastStreet = street;
        lastSign = sign;
    }

    private void pushDistance(int dist, int barOn, int bar) {
        ASLNavBAPFactory.getNavBAPApi().updateBapDistanceToNextManeuver(dist, 0, barOn == 1, bar);
        lastDist = dist;
        lastBarOn = barOn;
        lastBar = bar;
    }

    // v19.2: action words are searched anywhere in the text (Baidu: "左转进入XX路", "向右前方行驶进入XX"), the road name
    // is what follows the last one. Single-character 沿 / 经 are no longer stripped (沿江大道, 经三路 are road names);
    // only the full pattern 沿XX行驶/直行 is.
    private static final String[] ACTIONS = new String[] {
        "\u8fdb\u5165", "\u9a76\u5165", "\u524d\u5f80", "\u6cbf\u7740", "\u7ecf\u7531" };   // 进入 驶入 前往 沿着 经由
    private static final String[] TAILS = new String[] {
        "\u7ee7\u7eed\u884c\u9a76", "\u884c\u9a76", "\u76f4\u884c", "\u65b9\u5411" };    // 继续行驶 行驶 直行 方向
    private static final String YAN = "\u6cbf";                                                    // 沿
    private static final String[] SUFFIXES = new String[] { "\u9ad8\u901f\u516c\u8def", "\u516c\u8def" };
    private static final int MAX_CHARS = 5;
    private static final String UNNAMED = "\u65e0\u540d\u9053\u8def";

    /** HUD shows ~5 chars: keep the road name after the action words, drop spaces and long suffixes; unnamed roads. */
    static String shortName(String s) {
        if (s == null) {
            return "";
        }
        StringBuffer b = new StringBuffer();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c != ' ' && c != '\u3000' && c != '\t') {
                b.append(c);
            }
        }
        String t = b.toString();
        int cut = -1;
        for (int i = 0; i < ACTIONS.length; i++) {
            int k = t.lastIndexOf(ACTIONS[i]);
            if (k >= 0 && k + ACTIONS[i].length() > cut && k + ACTIONS[i].length() < t.length()) {
                cut = k + ACTIONS[i].length();
            }
        }
        if (cut > 0) {
            t = t.substring(cut);
        }
        boolean tail = false;
        for (int round = 0; round < 2; round++) {
            for (int i = 0; i < TAILS.length; i++) {
                if (t.endsWith(TAILS[i]) && t.length() > TAILS[i].length()) {
                    t = t.substring(0, t.length() - TAILS[i].length());
                    tail = true;
                    break;
                }
            }
        }
        if (tail && t.startsWith(YAN) && t.length() > 1) {
            t = t.substring(1);   // 沿XX路行驶 -> XX路
        }
        if (t.length() == 0 || t.equals("\u65e0\u540d\u9053\u8def") || t.equals("\u65e0\u540d\u8def")
            || t.equals("\u672a\u547d\u540d\u9053\u8def") || t.equals("\u672a\u547d\u540d\u8def")) {
            return UNNAMED;
        }
        if (t.length() > MAX_CHARS) {
            for (int i = 0; i < SUFFIXES.length; i++) {
                if (t.endsWith(SUFFIXES[i]) && t.length() > SUFFIXES[i].length()) {
                    t = t.substring(0, t.length() - SUFFIXES[i].length());
                    break;
                }
            }
        }
        if (t.length() > MAX_CHARS) {
            t = t.substring(0, MAX_CHARS);
        }
        return t;
    }

    private static long toLong(String s, long def) {
        try {
            String t = s.trim();
            return t.length() == 0 ? def : Long.parseLong(t);
        } catch (Throwable e) {
            return def;
        }
    }

    private static int toInt(String s, int def) {
        try {
            String t = s.trim();
            if (t.length() == 0) {
                return def;
            }
            long v = Long.parseLong(t);
            if (v > Integer.MAX_VALUE) {
                return Integer.MAX_VALUE;
            }
            if (v < Integer.MIN_VALUE) {
                return Integer.MIN_VALUE;
            }
            return (int) v;
        } catch (Throwable e) {
            return def;
        }
    }

    private static String readFile(String path) {
        FileInputStream in = null;
        try {
            File f = new File(path);
            if (!f.exists()) {
                return null;
            }
            in = new FileInputStream(f);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return new String(out.toByteArray(), "UTF-8");
        } catch (Throwable t) {
            return null;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Throwable t) {
                }
            }
        }
    }
}
