/*
 * Decompiled with CFR 0.152.
 */
package vwcn;

public class Mapper {
    public static final int NO_SYMBOL = 0;
    public static final int NO_INFO = 1;
    public static final int DIRECTION_TO_DESTINATION = 2;
    public static final int ARRIVED = 3;
    public static final int NEAR_DESTINATION = 4;
    public static final int ARRIVED_DESTINATION_OFFMAP = 5;
    public static final int OFF_ROAD = 6;
    public static final int OFF_MAP = 7;
    public static final int NO_ROUTE = 8;
    public static final int CALC_ROUTE = 9;
    public static final int RECALC_ROUTE = 10;
    public static final int FOLLOW_STREET = 11;
    public static final int CHANGE_LANE = 12;
    public static final int TURN = 13;
    public static final int TURN_ON_MAINROAD = 14;
    public static final int EXIT_RIGHT = 15;
    public static final int EXIT_LEFT = 16;
    public static final int SERVICE_ROAD_RIGHT = 17;
    public static final int SERVICE_ROAD_LEFT = 18;
    public static final int FORK_2 = 19;
    public static final int FORK_3 = 20;
    public static final int ROUNDABOUT_TRS_RIGHT = 21;
    public static final int ROUNDABOUT_TRS_LEFT = 22;
    public static final int SQUARE_TRS_RIGHT = 23;
    public static final int SQUARE_TRS_LEFT = 24;
    public static final int UTURN = 25;
    public static final int EXIT_ROUNDABOUT_TRS_RIGHT = 26;
    public static final int EXIT_ROUNDABOUT_TRS_LEFT = 27;
    public static final int PREPARE_TURN = 28;
    public static final int PREPARE_ROUNDABOUT = 29;
    public static final int PREPARE_SQUARE = 30;
    public static final int PREPARE_U_TURN = 31;
    public static final int MICHIGAN_TURN = 34;
    public static final int DOUBLE_TURN = 35;
    public static final int DIRECTION_TO_WAYPOINT = 38;
    public static final int DIR_STRAIGHT = 0;
    public static final int DIR_SLIGHT_LEFT = 32;
    public static final int DIR_LEFT = 64;
    public static final int DIR_SHARP_LEFT = 96;
    public static final int DIR_UTURN = 128;
    public static final int DIR_SHARP_RIGHT = 160;
    public static final int DIR_RIGHT = 192;
    public static final int DIR_SLIGHT_RIGHT = 224;
    public static final int JUNCTION_SINGLE_INTERSECTION = 0;
    public static final int JUNCTION_ROUNDABOUT = 1;
    public static final int DRIVING_SIDE_RIGHT = 0;
    public static final int DRIVING_SIDE_LEFT = 1;
    public static final int MT_NOT_SET = -1;
    public static final int MT_NO_TURN = 0;
    public static final int MT_LEFT_TURN = 1;
    public static final int MT_RIGHT_TURN = 2;
    public static final int MT_STRAIGHT_AHEAD = 3;
    public static final int MT_U_TURN = 4;
    public static final int MT_FOLLOW_ROAD = 5;
    public static final int MT_ENTER_ROUNDABOUT = 6;
    public static final int MT_EXIT_ROUNDABOUT = 7;
    public static final int MT_OFF_RAMP = 8;
    public static final int MT_ON_RAMP = 9;
    public static final int MT_ARRIVE_END_OF_NAVIGATION = 10;
    public static final int MT_START_ROUTE = 11;
    public static final int MT_ARRIVE_AT_DESTINATION = 12;
    public static final int MT_KEEP_LEFT = 13;
    public static final int MT_KEEP_RIGHT = 14;
    public static final int MT_ENTER_FERRY = 15;
    public static final int MT_EXIT_FERRY = 16;
    public static final int MT_CHANGE_FERRY = 17;
    public static final int MT_START_ROUTE_WITH_U_TURN = 18;
    public static final int MT_U_TURN_AT_ROUNDABOUT = 19;
    public static final int MT_LEFT_TURN_AT_END = 20;
    public static final int MT_RIGHT_TURN_AT_END = 21;
    public static final int MT_HIGHWAY_OFF_RAMP_LEFT = 22;
    public static final int MT_HIGHWAY_OFF_RAMP_RIGHT = 23;
    public static final int MT_ARRIVE_DESTINATION_LEFT = 24;
    public static final int MT_ARRIVE_DESTINATION_RIGHT = 25;
    public static final int MT_U_TURN_WHEN_POSSIBLE = 26;
    public static final int MT_ARRIVE_END_OF_DIRECTIONS = 27;
    public static final int MT_ROUNDABOUT_EXIT_1 = 28;
    public static final int MT_ROUNDABOUT_EXIT_2 = 29;
    public static final int MT_ROUNDABOUT_EXIT_3 = 30;
    public static final int MT_ROUNDABOUT_EXIT_4 = 31;
    public static final int MT_ROUNDABOUT_EXIT_5 = 32;
    public static final int MT_ROUNDABOUT_EXIT_6 = 33;
    public static final int MT_ROUNDABOUT_EXIT_7 = 34;
    public static final int MT_ROUNDABOUT_EXIT_8 = 35;
    public static final int MT_ROUNDABOUT_EXIT_9 = 36;
    public static final int MT_ROUNDABOUT_EXIT_10 = 37;
    public static final int MT_ROUNDABOUT_EXIT_11 = 38;
    public static final int MT_ROUNDABOUT_EXIT_12 = 39;
    public static final int MT_ROUNDABOUT_EXIT_13 = 40;
    public static final int MT_ROUNDABOUT_EXIT_14 = 41;
    public static final int MT_ROUNDABOUT_EXIT_15 = 42;
    public static final int MT_ROUNDABOUT_EXIT_16 = 43;
    public static final int MT_ROUNDABOUT_EXIT_17 = 44;
    public static final int MT_ROUNDABOUT_EXIT_18 = 45;
    public static final int MT_ROUNDABOUT_EXIT_19 = 46;
    public static final int MT_SHARP_LEFT_TURN = 47;
    public static final int MT_SHARP_RIGHT_TURN = 48;
    public static final int MT_SLIGHT_LEFT_TURN = 49;
    public static final int MT_SLIGHT_RIGHT_TURN = 50;
    public static final int MT_CHANGE_HIGHWAY = 51;
    public static final int MT_CHANGE_HIGHWAY_LEFT = 52;
    public static final int MT_CHANGE_HIGHWAY_RIGHT = 53;

    public static int[] map(int n, int n2, int n3, int n4) {
        int n5;
        int n6;
        if (n == -1) {
            int n7 = 0;
            int n8 = 0;
            return new int[]{n7, n8};
        }
        if (n == 0 || n == 5) {
            int n9 = 11;
            int n10 = 0;
            return new int[]{n9, n10};
        }
        if (n == 10 || n == 12 || n == 27) {
            int n11 = 3;
            int n12 = 0;
            return new int[]{n11, n12};
        }
        if (n == 52) {
            int n13 = 12;
            int n14 = 64;
            return new int[]{n13, n14};
        }
        if (n == 53) {
            int n15 = 12;
            int n16 = 192;
            return new int[]{n15, n16};
        }
        if (n == 15 || n == 17) {
            int n17 = 11;
            int n18 = 0;
            return new int[]{n17, n18};
        }
        if (n == 11 || n == 16 || n == 51) {
            int n19 = 13;
            int n20 = Mapper.directionFromTurnAngle(n2);
            return new int[]{n19, n20};
        }
        if (n == 19) {
            int n21 = n4 == 1 ? 22 : 21;
            int n22 = 128;
            return new int[]{n21, n22};
        }
        if (n == 6) {
            int n23 = 13;
            int n24 = n4 == 1 ? 64 : 192;
            n24 = Mapper.applyDsiNavBapDirectionOverride(n, n24);
            return new int[]{n23, n24};
        }
        if (n3 != 0) {
            if (n3 != 1) {
                int n25 = 1;
                int n26 = 0;
                return new int[]{n25, n26};
            }
            if (n == 7) {
                int n27 = n4 == 1 ? 27 : 26;
                int n28 = n4 == 1 ? 64 : 192;
                return new int[]{n27, n28};
            }
            if (n >= 28 && n <= 46) {
                int n29 = n4 == 1 ? 22 : 21;
                int n30 = Mapper.directionFromAngle16(n2);
                return new int[]{n29, n30};
            }
            int n31 = 1;
            int n32 = 0;
            return new int[]{n31, n32};
        }
        switch (n) {
            case 3: {
                n6 = 13;
                n5 = 0;
                break;
            }
            case 1: {
                n6 = 13;
                n5 = 64;
                break;
            }
            case 2: {
                n6 = 13;
                n5 = 192;
                break;
            }
            case 13: 
            case 49: {
                n6 = 13;
                n5 = 32;
                break;
            }
            case 14: 
            case 50: {
                n6 = 13;
                n5 = 224;
                break;
            }
            case 47: {
                n6 = 13;
                n5 = 96;
                break;
            }
            case 48: {
                n6 = 13;
                n5 = 160;
                break;
            }
            case 4: 
            case 18: 
            case 26: {
                n6 = 25;
                if (n4 == 1) {
                    n5 = 192;
                    break;
                }
                n5 = 64;
                break;
            }
            case 8: {
                if (n4 == 1) {
                    n6 = 16;
                    n5 = 32;
                    break;
                }
                n6 = 15;
                n5 = 224;
                break;
            }
            case 9: {
                n6 = 13;
                if (n4 == 1 && ((long)n2 & 0xFFFFFFFFL) > 179L) {
                    n5 = 32;
                    break;
                }
                n5 = 224;
                break;
            }
            case 22: {
                n6 = 16;
                n5 = 32;
                break;
            }
            case 23: {
                n6 = 15;
                n5 = 224;
                break;
            }
            case 24: 
            case 25: {
                n6 = 3;
                n5 = n == 24 ? 64 : 192;
                break;
            }
            case 20: {
                n6 = 13;
                n5 = Mapper.dirFromEndOfRoadAngleLeft(n2);
                break;
            }
            case 21: {
                n6 = 13;
                n5 = Mapper.dirFromEndOfRoadAngleRight(n2);
                break;
            }
            default: {
                n6 = 1;
                n5 = 0;
            }
        }
        n5 = Mapper.applyDsiNavBapDirectionOverride(n, n5);
        return new int[]{n6, n5};
    }

    private static int applyDsiNavBapDirectionOverride(int n, int n2) {
        if (n == 6 || n == 7 || n == 19 || n == 4 || n == 18 || n == 26 || n >= 28 && n <= 46) {
            return n2;
        }
        if (n == 8 || n == 9 || n == 22 || n == 23 || n == 13 || n == 14 || n == 49 || n == 50 || n == 47 || n == 48) {
            return n2;
        }
        if (n2 == 32 || n2 == 64 || n2 == 96) {
            return 64;
        }
        if (n2 == 224 || n2 == 192 || n2 == 160) {
            return 192;
        }
        if (n2 == 128) {
            return 128;
        }
        return 0;
    }

    public static int directionFromAngle16(int n) {
        int n2;
        int n3 = n;
        if (n3 > 180) {
            n3 = 180;
        }
        if (n3 < -180) {
            n3 = -180;
        }
        if ((n2 = (int)Math.floor((double)(n3 + 180) / 22.5 + 0.5)) < 0) {
            n2 = 0;
        }
        if (n2 > 16) {
            n2 = 16;
        }
        int[] nArray = new int[]{128, 112, 96, 80, 64, 48, 32, 16, 0, 240, 224, 208, 192, 176, 160, 144, 128};
        return nArray[n2];
    }

    private static int directionFromTurnAngle(int n) {
        if (n == 1000 || n == -1000) {
            return 0;
        }
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            return 64;
        }
        return 192;
    }

    private static int dirFromEndOfRoadAngleLeft(int n) {
        if (n != 0 && n < 0 && n >= -180) {
            if (n >= -67) {
                return 32;
            }
            if (n < -112) {
                return 96;
            }
        }
        return 64;
    }

    private static int dirFromEndOfRoadAngleRight(int n) {
        if (n != 0 && n > 0 && n <= 180) {
            if (n <= 67) {
                return 224;
            }
            if (n > 112) {
                return 160;
            }
        }
        return 192;
    }

    public static boolean isHighwayManeuver(int n) {
        return n == 8 || n == 9 || n == 22 || n == 23 || n == 51 || n == 52 || n == 53;
    }

    public static boolean isValidType(int n) {
        return n >= 0 && n <= 53;
    }
}

