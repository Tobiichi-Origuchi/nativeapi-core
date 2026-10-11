#!/bin/bash
# C++ NativeActivity regression; only disposable emulators are accepted.
set -euo pipefail
sdk=${1:?Android SDK required}
ndk=${2:?Android NDK required}
serial=${3:?Disposable emulator serial required}
out=${4:?Scratch build/output directory required}
[[ "$serial" == emulator-* ]] || { echo 'Use a disposable emulator'; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
cmake -S "$here/.." -B "$out" -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
cmake --build "$out" --target clipboard_android_test clipboard_android_writer -j 4
tools="$sdk/build-tools/36.0.0"
for kind in test writer; do
  case "$kind" in
    test) package=dev.nativeapi.clipboard.cpptest ;;
    writer) package=dev.nativeapi.clipboard.cppwriter ;;
  esac
  library=clipboard_android_$kind
  apk="$out/$kind.apk"
  mkdir -p "$out/$kind/lib/arm64-v8a"
  cp "$out/tests/lib$library.so" "$out/$kind/lib/arm64-v8a/"
  cat > "$out/$kind/AndroidManifest.xml" <<XML
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="$package">
  <uses-sdk android:minSdkVersion="24" android:targetSdkVersion="36" />
  <application android:label="nativeapi C++ clipboard test" android:hasCode="false">
    <activity android:name="android.app.NativeActivity" android:exported="true">
      <meta-data android:name="android.app.lib_name" android:value="$library" />
    </activity>
  </application>
</manifest>
XML
  "$tools/aapt2" link -I "$sdk/platforms/android-36/android.jar" --manifest "$out/$kind/AndroidManifest.xml" -o "$out/$kind-unsigned.apk"
  (cd "$out/$kind" && zip -q "$out/$kind-unsigned.apk" "lib/arm64-v8a/lib$library.so")
  "$tools/zipalign" -f 4 "$out/$kind-unsigned.apk" "$apk"
  if [ ! -f "$out/test.keystore" ]; then
    keytool -genkeypair -keystore "$out/test.keystore" -storepass android -keypass android -alias clipboard-test -dname CN=nativeapi-test -keyalg RSA -keysize 2048 -validity 2 >/dev/null 2>&1
  fi
  "$tools/apksigner" sign --ks "$out/test.keystore" --ks-pass pass:android --key-pass pass:android "$apk"
  "$sdk/platform-tools/adb" -s "$serial" install -r "$apk"
done
adb=("$sdk/platform-tools/adb" -s "$serial")
cleanup() {
  "${adb[@]}" shell am force-stop dev.nativeapi.clipboard.cpptest >/dev/null 2>&1 || true
  "${adb[@]}" shell am force-stop dev.nativeapi.clipboard.cppwriter >/dev/null 2>&1 || true
  "${adb[@]}" uninstall dev.nativeapi.clipboard.cpptest >/dev/null 2>&1 || true
  "${adb[@]}" uninstall dev.nativeapi.clipboard.cppwriter >/dev/null 2>&1 || true
}
trap cleanup EXIT
"${adb[@]}" logcat -c
"${adb[@]}" shell am start -n dev.nativeapi.clipboard.cpptest/android.app.NativeActivity
for ((attempt=0; attempt<60; attempt++)); do
  "${adb[@]}" logcat -d > "$out/runtime.log"
  if rg -q 'NATIVEAPI_CPP_CLIPBOARD_FAIL|FATAL EXCEPTION|Fatal signal' "$out/runtime.log"; then
    cat "$out/runtime.log"; exit 1
  fi
  if rg -q 'NATIVEAPI_CPP_CLIPBOARD_PASS' "$out/runtime.log"; then
    rg 'nativeapi-test' "$out/runtime.log"; exit 0
  fi
  sleep 1
done
tail -100 "$out/runtime.log"
exit 1
