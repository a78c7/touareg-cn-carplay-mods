package vwcn;

import de.vw.mib.bap.mqbab2.common.api.carplay.CarPlayService;
import de.vw.mib.bap.mqbab2.common.api.carplay.CarPlayServiceListener;
import de.vw.mib.bap.mqbab2.common.api.carplay.datatypes.CarPlayDeviceState;
import de.vw.mib.bap.mqbab2.common.api.carplay.datatypes.iterator.elements.CarPlayCall;
import java.util.Iterator;
import java.io.File;

/** Wraps the BAP CarPlayService; hides "CarPlay route guidance active" while /tmp/vwcn_noRG exists,
 *  so NavSD InfoStates does not send state 6 ("navigation active on mobile device") to the cluster. */
public final class NoRGCarPlay implements CarPlayService {
    static final File FLAG = new File("/tmp/vwcn_noRG");
    final CarPlayService d;
    NoRGCarPlay(CarPlayService d) { this.d = d; }
    public void addCarPlayServiceListener(CarPlayServiceListener var1, int[] var2) {
        d.addCarPlayServiceListener(var1, var2);
    }
    public void removeCarPlayServiceListener(CarPlayServiceListener var1, int[] var2) {
        d.removeCarPlayServiceListener(var1, var2);
    }
    public boolean isCarPlayActive() {
        return d.isCarPlayActive();
    }
    public CarPlayDeviceState getCarPlayDeviceState() {
        return d.getCarPlayDeviceState();
    }
    public boolean isCarPlayRouteGuidanceActive() {
        if (FLAG.exists()) return false;
        return d.isCarPlayRouteGuidanceActive();
    }
    public Iterator getCarPlayCalls() {
        return d.getCarPlayCalls();
    }
    public void hangUpCall() {
        d.hangUpCall();
    }
    public void hangUpCall(CarPlayCall var1) {
        d.hangUpCall(var1);
    }
    public void acceptCall(int var1) {
        d.acceptCall(var1);
    }
    public int getCarPlayTelephonyStateRegistrationStatus() {
        return d.getCarPlayTelephonyStateRegistrationStatus();
    }
    public String getDeviceName() {
        return d.getDeviceName();
    }
    public boolean isAirplanModeActive() {
        return d.isAirplanModeActive();
    }
    public boolean isCarPlayTelephonyStateCellularSupported() {
        return d.isCarPlayTelephonyStateCellularSupported();
    }
    public boolean isCarPlayTelephonyStateAvailable() {
        return d.isCarPlayTelephonyStateAvailable();
    }
    public int getCarPlayTelephonyStateBatteryLevel() {
        return d.getCarPlayTelephonyStateBatteryLevel();
    }
    public boolean isMuteStatus() {
        return d.isMuteStatus();
    }
    public boolean isHoldAvailable() {
        return d.isHoldAvailable();
    }
    public boolean isEndAndAcceptAvailable() {
        return d.isEndAndAcceptAvailable();
    }
    public boolean isSwapAvailable() {
        return d.isSwapAvailable();
    }
    public boolean isHoldAndAcceptAvailable() {
        return d.isHoldAndAcceptAvailable();
    }
    public boolean isMergeAvailable() {
        return d.isMergeAvailable();
    }
    public void requestEndActiveAndAcceptIncomingCall() {
        d.requestEndActiveAndAcceptIncomingCall();
    }
    public void requestHoldCall() {
        d.requestHoldCall();
    }
    public void requestSwapCalls() {
        d.requestSwapCalls();
    }
    public void requestResumeCall() {
        d.requestResumeCall();
    }
    public void requestMergeCalls() {
        d.requestMergeCalls();
    }
    public boolean isServiceStateConnectedForCPWL() {
        return d.isServiceStateConnectedForCPWL();
    }
    public boolean isServiceStateConnectedForCPWired() {
        return d.isServiceStateConnectedForCPWired();
    }
}
