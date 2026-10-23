#!/bin/sh
# Build VWClusterNav_vN.jar (Java 1.4 class files, version 48 - the head unit's J9 rejects newer).
# Needs: a JDK 8 (javac -source 1.4 -target 1.4), lsd.jar converted from the unit's /ifs/lsd.jxe
# with luka-dev/jxe2jar, and a J9 foundation class library (jcl.jar) as boot class path.
# usage: JDK8=/path/to/jdk8 LSD_JAR=lsd.jar JCL=jcl.jar ./build.sh v6
set -e
V=${1:-v6}
rm -rf out && mkdir out
"$JDK8/bin/javac" -nowarn -source 1.4 -target 1.4 -bootclasspath "$JCL" -cp "$LSD_JAR" -d out vwcn/*.java
printf 'Manifest-Version: 1.0\r\nImplementation-Title: VWClusterNav\r\nImplementation-Version: %s\r\n\r\n' "$V" > out/MANIFEST.MF
(cd out && "$JDK8/bin/jar" cfm ../VWClusterNav_$V.jar MANIFEST.MF vwcn/*.class)
echo "built VWClusterNav_$V.jar"
