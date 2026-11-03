#!/bin/sh
# 在 iPhone 上執行（需要 apt 的 clang、ldid、dpkg-deb）。把這個資料夾 scp 到手機 /var/root/privatemac 後：
# sh build.sh → 先跑位址解析的自我檢查，再產出 com.neo.privatemac_<版本>_iphoneos-arm.deb
set -e
cd "$(dirname "$0")"
D=pkg/Library/MobileSubstrate/DynamicLibraries
rm -rf pkg sdk && mkdir -p $D pkg/DEBIAN sdk/usr/lib
cp libSystem.tbd sdk/usr/lib/
clang -target arm64-apple-ios14.0 -isysroot sdk -O2 -fblocks -DPM_TEST -o pmtest PrivateMAC.c && ldid -S pmtest && ./pmtest && rm pmtest
# 系統處理程序（wifid、SpringBoard、設定）是 arm64e：要做成 arm64＋arm64e 雙架構才會被 Substitute 注入
# （clang 10 只編得出舊 ABI 的 arm64e，unc0ver 的 Substitute 照樣載入）
for A in arm64 arm64e; do
  clang -target $A-apple-ios12.0 -isysroot sdk -O2 -fblocks -dynamiclib     -Wl,-install_name,/Library/MobileSubstrate/DynamicLibraries/PrivateMAC.dylib -o PrivateMAC-$A.dylib PrivateMAC.c
done
lipo -create PrivateMAC-arm64.dylib PrivateMAC-arm64e.dylib -output $D/PrivateMAC.dylib && rm PrivateMAC-arm64*.dylib
ldid -S $D/PrivateMAC.dylib
echo '{ Filter = { Bundles = ( "com.apple.springboard", "com.apple.Preferences" ); Executables = ( "wifid" ); }; }' > $D/PrivateMAC.plist
cp control pkg/DEBIAN/
dpkg-deb -Zgzip -b pkg com.neo.privatemac_$(sed -n 's/^Version: //p' control)_iphoneos-arm.deb
