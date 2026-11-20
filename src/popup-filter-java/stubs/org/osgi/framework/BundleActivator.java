package org.osgi.framework;

public interface BundleActivator {
    void start(BundleContext context) throws Exception;
    void stop(BundleContext context) throws Exception;
}
