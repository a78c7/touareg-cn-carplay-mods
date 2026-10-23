package vwcn;

import de.vw.mib.asl.api.navbap.INavLaneGuidanceDataNavBap;

/** One lane for BAP NavSD LaneGuidance (FctID 24), fed via ASLNavBAPAPI.updateBapLaneGuidance. */
public final class Lane implements INavLaneGuidanceDataNavBap {
    final short direction;
    final byte guidance;
    final byte[] sideStreets;

    Lane(int direction, int guidance, byte[] sideStreets) {
        this.direction = (short) direction;
        this.guidance = (byte) guidance;
        this.sideStreets = sideStreets;
    }

    public short getLaneDirection() { return direction; }
    public byte getGuidanceInfo() { return guidance; }
    public byte getLaneDescription() { return 0; }   // lane available
    public byte getLaneMarkingLeft() { return 0; }
    public byte getLaneMarkingRight() { return 0; }
    public short getLaneType() { return 1; }         // normal lane
    public byte[] getLaneSideStreets() { return sideStreets; }

    /** iAP2 signed degrees (+90 right, -90 left) -> BAP 360/256 byte (64 = left, 192 = right). */
    static int angleToBap(int degrees) {
        int d = -degrees;
        while (d < 0) d += 360;
        while (d >= 360) d -= 360;
        return (d * 256 + 180) / 360 & 0xff;
    }

    /** Other legal directions of a lane (faded arrows), excluding the main one. */
    static byte[] sideStreets(String csv, int primary) {
        if (csv == null || csv.length() == 0) return new byte[0];
        int[] tmp = new int[8];
        int n = 0;
        java.util.StringTokenizer st = new java.util.StringTokenizer(csv, ",");
        while (st.hasMoreTokens() && n < tmp.length) {
            int a;
            try {
                a = Integer.parseInt(st.nextToken().trim());
            } catch (Throwable t) {
                continue;
            }
            if (a == 1000) continue;
            int code = angleToBap(a);
            if (code == primary) continue;
            boolean dup = false;
            for (int j = 0; j < n; j++) if (tmp[j] == code) dup = true;
            if (!dup) tmp[n++] = code;
        }
        for (int i = 1; i < n; i++) {
            int k = tmp[i], j = i - 1;
            while (j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; }
            tmp[j + 1] = k;
        }
        byte[] out = new byte[n];
        for (int i = 0; i < n; i++) out[i] = (byte) tmp[i];
        return out;
    }
}
