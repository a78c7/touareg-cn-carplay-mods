package vwcn;

import java.io.File;
import java.io.FileWriter;

/** Tiny logger with no HMI dependencies, so it works even if HMI classes fail to link. */
public final class Log {
    static final String LOG = "/tmp/vwcn.log";
    private static final long LOG_MAX = 300000L;

    private Log() {
    }

    public static synchronized void log(String msg) {
        FileWriter w = null;
        try {
            File f = new File(LOG);
            boolean append = !f.exists() || f.length() <= LOG_MAX;
            w = new FileWriter(LOG, append);
            StringBuffer sb = new StringBuffer();
            sb.append(System.currentTimeMillis()).append(' ').append(msg).append('\n');
            w.write(sb.toString());
        } catch (Throwable t) {
            // nothing we can do
        } finally {
            if (w != null) {
                try {
                    w.close();
                } catch (Throwable t) {
                }
            }
        }
    }
}
