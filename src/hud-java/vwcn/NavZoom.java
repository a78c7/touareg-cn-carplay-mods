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
    final de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService d;
    private int pending;
    private long lastSent;
    private int seq;
    private boolean logged;

    NavZoom(de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService d) {
        this.d = d;
        Thread t = new Thread(this, "vwcn-zoom");
        t.setDaemon(true);
        t.start();
    }

    private synchronized void press(int step) {
        pending += step;
        if (!logged) { logged = true; Log.log("zoom: first cluster zoom key while CarPlay is on the cluster, step " + step); }
    }

    public void run() {
        while (true) {
            try {
                Thread.sleep(50);
                int p;
                long now = System.currentTimeMillis();
                synchronized (this) {
                    if (pending == 0 || now - lastSent < 150) continue;
                    p = pending;
                    pending = 0;
                }
                if (Rebind.hold == null) continue;
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
        if (Rebind.hold != null) return;
        d.activateAutozoom();
    }
    public void addHMIEventListener(de.vw.mib.asl.api.bap.events.HMIEventListener a0, int[] a1) {
        d.addHMIEventListener(a0, a1);
    }
    public void addNavigationServiceListener(de.vw.mib.bap.mqbab2.common.api.navigation.NavigationServiceListener a0, int[] a1) {
        d.addNavigationServiceListener(a0, a1);
    }
    public void deactivateAutozoom() {
        if (Rebind.hold != null) return;
        d.deactivateAutozoom();
    }
    public void decrementZoomIndex() {
        if (Rebind.hold != null) { press(-1); return; }
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
        if (Rebind.hold != null) { press(+1); return; }
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
        d.repeatLastNavAnnouncement();
    }
    public void setActiveMapOrientation(int a0) {
        d.setActiveMapOrientation(a0);
    }
    public void setActiveMapType(int a0) {
        d.setActiveMapType(a0);
    }
    public void setActiveMapView(int a0) {
        d.setActiveMapView(a0);
    }
    public void setAlternativeRouteCalcSetting(boolean a0) {
        d.setAlternativeRouteCalcSetting(a0);
    }
    public void setMapScaleSteps(int a0) {
        d.setMapScaleSteps(a0);
    }
    public void setMapSwitchState(int a0) {
        d.setMapSwitchState(a0);
    }
    public void setVoiceGuidanceStatus(int a0) {
        d.setVoiceGuidanceStatus(a0);
    }
    public void setxUrgentTMCMessageToRead() {
        d.setxUrgentTMCMessageToRead();
    }
    public void startRouteGuidance(int a0, long a1) {
        d.startRouteGuidance(a0, a1);
    }
    public void stopCurrentNavAnnouncement() {
        d.stopCurrentNavAnnouncement();
    }
    public void stopRouteGuidance() {
        d.stopRouteGuidance();
    }
}
