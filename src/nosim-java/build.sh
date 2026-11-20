#!/bin/sh
# Build VWNoSimPopup.jar (Java 1.4 class files, major version 48 - MH2p J9 Foundation 1.1).
# Standalone build: only needs JDK 8 (javac -source 1.4 -target 1.4), no lsd.jar required.
# usage: JDK8=/path/to/jdk8 ./build.sh
set -e
cd "$(dirname "$0")"
: ${JDK8:?set JDK8 to a JDK 8 directory (e.g. /home/admin/qnx-build/jdk8)}

rm -rf out && mkdir -p out/stubs out/classes
"$JDK8/bin/javac" -nowarn -source 1.4 -target 1.4 -d out/stubs \
    stubs/org/osgi/framework/BundleContext.java \
    stubs/org/osgi/framework/BundleActivator.java
"$JDK8/bin/javac" -nowarn -source 1.4 -target 1.4 -cp out/stubs -d out/classes \
    vwnosim/Activator.java

printf 'Manifest-Version: 1.0\r\nImplementation-Title: VWNoSimPopup\r\nImplementation-Version: 1.0\r\n\r\n' > out/MANIFEST.MF
(cd out/classes && "$JDK8/bin/jar" cfm ../../VWNoSimPopup.jar ../MANIFEST.MF vwnosim/*.class)
echo "built VWNoSimPopup.jar"
