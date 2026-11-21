package vwcn;

import java.io.File;
import java.io.FileWriter;

/**
 * v19.2: steering-wheel zoom for the CarPlay map on the cluster.
 * The cluster sends the zoom keys of its map view as BAP NavSD MapScale requests; the stock HMI turns them into
 * NavigationService.increment/decrementZoomIndex() for the stock cluster map. This wraps the BAP NavigationService
 * (APIFactory.navigationService, installed by RGGate like NoRGCarPlay): while the CarPlay map is on the cluster
 * (Rebind.hold != null) the zoom steps are not applied to the hidden stock map but counted, and a writer thread
 * sends them to the iPhone through the cl_probe command file (/tmp/cl_alt_cmd "zoomin N #seq" / "zoomout N #seq";
 * the hook polls it every 100 ms by content). Presses within 150 ms are merged (N <= 3).
 * Everything else is passed through unchanged.
 * Which key is "in" is not known yet: increment = zoom out by default; an empty file
 * /mnt/app/eso/bin/apps/cluster/zoom_flip swaps the directions.
 */
public final class NavZoom implements de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService, Runnable {
    static final String CMD = "/tmp/cl_alt_cmd";
    static final File FLIP = new File("/mnt/app/eso/bin/apps/cluster/zoom_flip");
    static final File SWITCH_OFF = new File("/mnt/app/eso/bin/apps/cluster/switch_off");
    static final long DEACT_DELAY_MS = 100L;      // 缩放键附带的 deactivateAutozoom 会在 1~20ms 内紧跟 zoomIndex，100ms 可彻底过滤
    static final long DOUBLE_TAP_MAX_MS = 2000L;  // 第 1 击与第 2 击最大间隔（连按 2 下切换 CarPlay ↔ 原车地图）
    static final long MAP_COOLDOWN_MS = 2500L;    // 切地图后 2.5s 冷却（过滤仪表在 167ms/1345ms 自动补发的 Autozoom 回弹）
    final de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService d;
    private int pending;
    private long lastSent;
    private int seq;
    private boolean logged;
    // 方向盘右侧 OK 键双击（切换 CarPlay 地图 ↔ 原车地图）状态
    private long pendingDeactAt;      // 待确认的独立 deactivateAutozoom 时间戳（0=无）
    private long lastOkTapAt;         // 上次已确认的独立 OK 按键时间
    private int  lastOkAct;           // 1=activateAutozoom, -1=deactivateAutozoom
    private int  okTapCount;          // 当前连击次数 (0, 1)
    private long cooldownUntil;       // 防连跳冷却截止时间戳

    NavZoom(de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService d) {
        this.d = d;
        Thread t = new Thread(this, "vwcn-zoom");
        t.setDaemon(true);
        t.start();
    }

    private synchronized void press(int step) {
        pendingDeactAt = 0; // 收到缩放键，取消刚伴随到来的 deactivateAutozoom
        pending += step;
        if (!logged) { logged = true; Log.log("zoom: first cluster zoom key while CarPlay is on the cluster, step " + step); }
    }

    private synchronized void onZoomKeyNative() {
        pendingDeactAt = 0; // 原车地图下按缩放键同样取消附带的 deactivateAutozoom
    }

    /** 记录一次已确认的独立 OK 键按下（act: 1=activate, -1=deactivate）。
     *  交互规则：2 秒内连按 2 下 = 切换 CarPlay 地图 ↔ 原车地图。 */
    private void recordOkTap(long ts, int act) {
        if (SWITCH_OFF.exists()) return;
        if (Rebind.streamHold == null) {
            lastOkTapAt = 0;
            okTapCount = 0;
            return;
        }
        if (ts < cooldownUntil) {
            Log.log("switch: ok tap ignored (cooldown) act=" + act);
            return;
        }
        long dt = ts - lastOkTapAt;
        // 过滤同方向 540~660ms 的 BAP 重试帧
        if (lastOkTapAt > 0 && act == lastOkAct && dt >= 540L && dt <= 660L) {
            Log.log("switch: ignored possible 600ms BAP retry dt=" + dt + "ms act=" + act);
            return;
        }
        if (okTapCount == 1 && lastOkTapAt > 0 && dt >= 120L && dt <= DOUBLE_TAP_MAX_MS) {
            okTapCount = 0;
            lastOkTapAt = 0;
            lastOkAct = 0;
            cooldownUntil = ts + MAP_COOLDOWN_MS;
            Log.log("switch: OK double-tap detected dt=" + dt + "ms (" + act + ") -> toggle map");
            Rebind.requestToggle("ok-double-tap");
        } else {
            okTapCount = 1;
            lastOkTapAt = ts;
            lastOkAct = act;
            Log.log("switch: OK tap #1 act=" + act + " cpHold=" + (Rebind.hold != null ? 1 : 0));
        }
    }

    public void run() {
        while (true) {
            try {
                Thread.sleep(50);
                int p = 0;
                long now = System.currentTimeMillis();
                long fireDeact = 0;
                synchronized (this) {
                    if (pendingDeactAt > 0 && (now - pendingDeactAt) >= DEACT_DELAY_MS) {
                        fireDeact = pendingDeactAt;
                        pendingDeactAt = 0;
                    }
                    if (pending != 0 && now - lastSent >= 150) {
                        p = pending;
                        pending = 0;
                    }
                }
                if (fireDeact > 0) {
                    synchronized (this) { recordOkTap(fireDeact, -1); }
                }
                if (p == 0 || Rebind.hold == null) continue;
                boolean out = p > 0;
                if (FLIP.exists()) out = !out;
                int n = Math.min(Math.abs(p), 3);
                FileWriter w = new FileWriter(CMD, false);
                w.write((out ? "zoomout " : "zoomin ") + n + " #" + (++seq) + "\n");
                w.close();
                lastSent = now;
                Log.log("zoom: " + (out ? "out " : "in ") + n);
            } catch (Throwable t) {
                Log.log("zoom error: " + t);
                try { Thread.sleep(2000); } catch (Throwable u) {}
            }
        }
    }

    public void activateAutozoom() {
        if (KeyProbe.active) Log.log("keyprobe: navbap activateAutozoom cpHold=" + (Rebind.hold != null ? 1 : 0));
        // 放行给原车服务以返回 BAP Status ACK，消除仪表 600ms×5 超时重传；随后计一次独立 OK 按键
        try { d.activateAutozoom(); } catch (Throwable t) { Log.log("activateAutozoom delegate err: " + t); }
        synchronized (this) {
            pendingDeactAt = 0;
            recordOkTap(System.currentTimeMillis(), 1);
        }
    }
    public void addHMIEventListener(de.vw.mib.asl.api.bap.events.HMIEventListener a0, int[] a1) {
        d.addHMIEventListener(a0, a1);
    }
    public void addNavigationServiceListener(de.vw.mib.bap.mqbab2.common.api.navigation.NavigationServiceListener a0, int[] a1) {
        d.addNavigationServiceListener(a0, a1);
    }
    public void deactivateAutozoom() {
        if (KeyProbe.active) Log.log("keyprobe: navbap deactivateAutozoom cpHold=" + (Rebind.hold != null ? 1 : 0));
        // 放行以返回 BAP ACK，并用 100ms 延迟区分是缩放键附带还是独立按 OK 键
        try { d.deactivateAutozoom(); } catch (Throwable t) { Log.log("deactivateAutozoom delegate err: " + t); }
        synchronized (this) {
            pendingDeactAt = System.currentTimeMillis();
        }
    }
    public void decrementZoomIndex() {
        if (KeyProbe.active) Log.log("keyprobe: navbap decrementZoomIndex cpHold=" + (Rebind.hold != null ? 1 : 0));
        if (Rebind.hold != null) { press(-1); return; }
        onZoomKeyNative();
        d.decrementZoomIndex();
    }
    public int getActiveMapOrientation() {
        return d.getActiveMapOrientation();
    }
    public int getActiveMapType() {
        return d.getActiveMapType();
    }
    public int getActiveMapView() {
        return d.getActiveMapView();
    }
    public int getCompassInfoAngle() {
        return d.getCompassInfoAngle();
    }
    public java.lang.String getCurrentPositionInfo() {
        return d.getCurrentPositionInfo();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationDistanceToDestination getDistanceToDestination() {
        return d.getDistanceToDestination();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationDistanceToNextManeuver getDistanceToNextManeuver() {
        return d.getDistanceToNextManeuver();
    }
    public java.util.Iterator getFavoriteDestinations() {
        return d.getFavoriteDestinations();
    }
    public int getFavoriteDestinationsNumberOfEntries() {
        return d.getFavoriteDestinationsNumberOfEntries();
    }
    public int getGuidanceAnnouncementActive() {
        return d.getGuidanceAnnouncementActive();
    }
    public int getKombiMapStatus() {
        return d.getKombiMapStatus();
    }
    public int getKombiMapVisibility() {
        return d.getKombiMapVisibility();
    }
    public java.util.Iterator getLaneGuidance() {
        return d.getLaneGuidance();
    }
    public java.util.Iterator getLastDestinations() {
        return d.getLastDestinations();
    }
    public java.util.Iterator getManeuverDescriptor() {
        return d.getManeuverDescriptor();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationMapScale getMapScale() {
        return d.getMapScale();
    }
    public int getMapSwitchState() {
        return d.getMapSwitchState();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationCurrentAltitude getNavigationCurrentAltitude() {
        return d.getNavigationCurrentAltitude();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationDestinationInfo getNavigationDestinationInfo() {
        return d.getNavigationDestinationInfo();
    }
    public int getNavigationOperationState() {
        return d.getNavigationOperationState();
    }
    public int getNavigationServiceState() {
        return d.getNavigationServiceState();
    }
    public int getNavigationStatus() {
        return d.getNavigationStatus();
    }
    public int getNumberOfElementsInFavoriteDestinations() {
        return d.getNumberOfElementsInFavoriteDestinations();
    }
    public int getNumberOfElementsInLastDestinations() {
        return d.getNumberOfElementsInLastDestinations();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationOnlineNavigationState getOnlineNavigationState() {
        return d.getOnlineNavigationState();
    }
    public int getOnlineNavigationSystem() {
        return d.getOnlineNavigationSystem();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationRemainingTravelTime getRemainingTravelTime() {
        return d.getRemainingTravelTime();
    }
    public int getRouteGuidanceState() {
        return d.getRouteGuidanceState();
    }
    public int getSatelliteMapLogo() {
        return d.getSatelliteMapLogo();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationStopoverInfo getStopoverInfo() {
        return d.getStopoverInfo();
    }
    public boolean getSuggestAlternativeRouteCalculation() {
        return d.getSuggestAlternativeRouteCalculation();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationSupportedMapOrientations getSupportedMapOrientations() {
        return d.getSupportedMapOrientations();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationSupportedMapTypes getSupportedMapTypes() {
        return d.getSupportedMapTypes();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationSupportedMapViews getSupportedMapViews() {
        return d.getSupportedMapViews();
    }
    public java.util.Iterator getTMCInfos() {
        return d.getTMCInfos();
    }
    public int getTMCSymbol() {
        return d.getTMCSymbol();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationTimeToDestination getTimeToDestination() {
        return d.getTimeToDestination();
    }
    public int getTimeToDestinationTimeInfo() {
        return d.getTimeToDestinationTimeInfo();
    }
    public de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationTurnToInfo getTurnToInfo() {
        return d.getTurnToInfo();
    }
    public int getVisibleIntersectionJunctionView() {
        return d.getVisibleIntersectionJunctionView();
    }
    public int getVoiceGuidanceStatus() {
        return d.getVoiceGuidanceStatus();
    }
    public java.util.Iterator getWindowedCachedFavoriteDestinations(int[] a0) {
        return d.getWindowedCachedFavoriteDestinations(a0);
    }
    public java.util.Iterator getWindowedCachedLastDestinations(int[] a0) {
        return d.getWindowedCachedLastDestinations(a0);
    }
    public java.util.Iterator getWindowedFavoriteDestinations(int[] a0) {
        return d.getWindowedFavoriteDestinations(a0);
    }
    public java.util.Iterator getWindowedLastDestinations(int[] a0) {
        return d.getWindowedLastDestinations(a0);
    }
    public int getZoomLevel() {
        return d.getZoomLevel();
    }
    public void incrementZoomIndex() {
        if (KeyProbe.active) Log.log("keyprobe: navbap incrementZoomIndex cpHold=" + (Rebind.hold != null ? 1 : 0));
        if (Rebind.hold != null) { press(+1); return; }
        onZoomKeyNative();
        d.incrementZoomIndex();
    }
    public boolean isAutozoomEnabled() {
        return d.isAutozoomEnabled();
    }
    public boolean isGALRouteGuidanceActive() {
        return d.isGALRouteGuidanceActive();
    }
    public boolean isGPSSignalAvailable() {
        return d.isGPSSignalAvailable();
    }
    public boolean isMaximumZoomLevelReached() {
        if (Rebind.hold != null) return false;
        return d.isMaximumZoomLevelReached();
    }
    public boolean isMinimumZoomLevelReached() {
        if (Rebind.hold != null) return false;
        return d.isMinimumZoomLevelReached();
    }
    public boolean isNavigationDemoModeActive() {
        return d.isNavigationDemoModeActive();
    }
    public boolean isSetupLaneGuidanceActive() {
        return d.isSetupLaneGuidanceActive();
    }
    public boolean isTMCSignalAvailable() {
        return d.isTMCSignalAvailable();
    }
    public boolean isVISCSignalAvailable() {
        return d.isVISCSignalAvailable();
    }
    public void removeHMIEventListener(de.vw.mib.asl.api.bap.events.HMIEventListener a0, int[] a1) {
        d.removeHMIEventListener(a0, a1);
    }
    public void removeNavigationServiceListener(de.vw.mib.bap.mqbab2.common.api.navigation.NavigationServiceListener a0, int[] a1) {
        d.removeNavigationServiceListener(a0, a1);
    }
    public void repeatLastNavAnnouncement() {
        if (KeyProbe.active) Log.log("keyprobe: navbap repeatLastNavAnnouncement");
        d.repeatLastNavAnnouncement();
    }
    public void setActiveMapOrientation(int a0) {
        if (KeyProbe.active) Log.log("keyprobe: navbap setActiveMapOrientation=" + a0);
        d.setActiveMapOrientation(a0);
    }
    public void setActiveMapType(int a0) {
        if (KeyProbe.active) Log.log("keyprobe: navbap setActiveMapType=" + a0);
        d.setActiveMapType(a0);
    }
    public void setActiveMapView(int a0) {
        if (KeyProbe.active) Log.log("keyprobe: navbap setActiveMapView=" + a0);
        d.setActiveMapView(a0);
    }
    public void setAlternativeRouteCalcSetting(boolean a0) {
        d.setAlternativeRouteCalcSetting(a0);
    }
    public void setMapScaleSteps(int a0) {
        if (KeyProbe.active) Log.log("keyprobe: navbap setMapScaleSteps=" + a0);
        d.setMapScaleSteps(a0);
    }
    public void setMapSwitchState(int a0) {
        if (KeyProbe.active) Log.log("keyprobe: navbap setMapSwitchState=" + a0);
        d.setMapSwitchState(a0);
    }
    public void setVoiceGuidanceStatus(int a0) {
        d.setVoiceGuidanceStatus(a0);
    }
    public void setxUrgentTMCMessageToRead() {
        d.setxUrgentTMCMessageToRead();
    }
    public void startRouteGuidance(int a0, long a1) {
        if (KeyProbe.active) Log.log("keyprobe: navbap startRouteGuidance=" + a0 + "," + a1);
        d.startRouteGuidance(a0, a1);
    }
    public void stopCurrentNavAnnouncement() {
        if (KeyProbe.active) Log.log("keyprobe: navbap stopCurrentNavAnnouncement");
        d.stopCurrentNavAnnouncement();
    }
    public void stopRouteGuidance() {
        if (KeyProbe.active) Log.log("keyprobe: navbap stopRouteGuidance");
        d.stopRouteGuidance();
    }
}
