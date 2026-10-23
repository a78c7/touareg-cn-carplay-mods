# Make the byte patch for vhpatch from a stock and a modified viewhandler.zip (both stay on your own disk,
# only the patch - offsets and byte values - goes into the repository).
# usage: python3 mkpatch.py stock.zip modified.zip "description" > fullscreen.patch
import sys, zipfile, zlib
a, b, desc = zipfile.ZipFile(sys.argv[1]), zipfile.ZipFile(sys.argv[2]), sys.argv[3]
print('# vhpatch 1: ' + desc)
print('# entry <name> <size> <stock crc32> <patched crc32>, then "<offset> <stock byte> <patched byte>" (hex)')
for n in a.namelist():
    da, db = a.read(n), b.read(n)
    if da == db:
        continue
    if len(da) != len(db):
        sys.exit('size differs in ' + n)
    print('entry %s %d %08x %08x' % (n, len(da), zlib.crc32(da), zlib.crc32(db)))
    for i in range(len(da)):
        if da[i] != db[i]:
            print('%d %02x %02x' % (i, da[i], db[i]))
