echo If this fails you probably need to download the latest
echo libtool,aclocal,autoconf and automake
libtoolize --copy --force
aclocal  --force
autoconf -f
automake -acf
