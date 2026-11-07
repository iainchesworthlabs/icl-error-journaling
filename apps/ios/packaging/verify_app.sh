#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Check that an iOS application (a built .app, or an exported .ipa) is complete.
#
#   apps/ios/packaging/verify_app.sh <name.app | package.ipa> --bundle-id <id> --executable <name>
#       [--version <x.y.z>] [--signed [--store]]
#
# What it looks for:
#
#   * Info.plist is valid, names the identifier, the executable, the version, the
#     icon set and a launch screen, says it is an iPhone/iPad program, and holds
#     no usage text for a permission (the application asks for none);
#   * the compiled asset catalogue (the icon) is in the bundle;
#   * the executable is for arm64 and links nothing outside the system: a link to
#     a framework or to a path on the build machine means the build picked up
#     the wrong libraries;
#   * Info.plist declares that the application uses no encryption beyond the system's, and a
#     build number, as TestFlight asks;
#   * with --signed: the code signature verifies, the profile is embedded, it is an
#     ad-hoc one (it names devices and does not allow a debugger to attach) and it
#     belongs to this bundle identifier. With --store it must be an App Store one
#     instead: no devices, and no debugger.
#
# macOS only (plutil, otool, codesign, security).
set -euo pipefail

target=""
bundle_id=""
executable=""
version=""
signed=false
store=false
while [ $# -gt 0 ]; do
  case "$1" in
    --bundle-id) bundle_id="${2:?}"; shift 2 ;;
    --executable) executable="${2:?}"; shift 2 ;;
    --version) version="${2:?}"; shift 2 ;;
    --signed) signed=true; shift ;;
    --store) store=true; shift ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "verify_app.sh: unknown argument '$1'" >&2; exit 2 ;;
    *) target="$1"; shift ;;
  esac
done
[ -e "$target" ] || { echo "usage: verify_app.sh <app|ipa> --bundle-id <id> --executable <name> [--version v] [--signed [--store]]" >&2; exit 2; }
[ -n "$bundle_id" ] && [ -n "$executable" ] || { echo "verify_app.sh: --bundle-id and --executable are required" >&2; exit 2; }
fail() { echo "FAILED: $*" >&2; exit 1; }


work=""
cleanup() { if [ -n "$work" ]; then rm -rf "$work"; fi; }
trap cleanup EXIT

app="$target"
case "$target" in
  *.ipa)
    work="$(mktemp -d)"
    unzip -q "$target" -d "$work"
    app="$(find "$work/Payload" -maxdepth 1 -name '*.app' -type d | head -n 1)"
    [ -n "$app" ] || fail "no Payload/*.app in $target"
    ;;
esac
echo "==> $app"

plist="$app/Info.plist"
[ -f "$plist" ] || fail "no Info.plist (an iOS bundle is flat: it is beside the executable)"
plutil -lint "$plist" >/dev/null || fail "Info.plist is not a valid property list"
key() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }

[ "$(key CFBundleIdentifier)" = "$bundle_id" ] || fail "CFBundleIdentifier is '$(key CFBundleIdentifier)'"
[ "$(key CFBundleExecutable)" = "$executable" ] || fail "CFBundleExecutable is '$(key CFBundleExecutable)'"
for usage in NSCameraUsageDescription NSMicrophoneUsageDescription NSLocationWhenInUseUsageDescription NSPhotoLibraryUsageDescription; do
  [ -z "$(key $usage)" ] || fail "Info.plist has $usage: the application asks for no permission"
done
[ "$(key CFBundleIconName)" = "AppIcon" ] || fail "CFBundleIconName is '$(key CFBundleIconName)'"
[ -n "$(key UILaunchScreen)" ] || fail "Info.plist has no UILaunchScreen (iOS would run the program in a legacy window)"
[ "$(key DTPlatformName)" = "iphoneos" ] || fail "DTPlatformName is '$(key DTPlatformName)'; this is not a device build"
[ -n "$(key MinimumOSVersion)" ] || fail "Info.plist has no MinimumOSVersion"
if [ -n "$version" ]; then
  [ "$(key CFBundleShortVersionString)" = "$version" ] || fail "CFBundleShortVersionString is '$(key CFBundleShortVersionString)', expected $version"
fi
[ "$(key ITSAppUsesNonExemptEncryption)" = "false" ] || fail "ITSAppUsesNonExemptEncryption is '$(key ITSAppUsesNonExemptEncryption)': TestFlight would ask about encryption for every build"
[ -n "$(key CFBundleVersion)" ] || fail "Info.plist has no CFBundleVersion (the build number)"
echo "    Info.plist: identifier, executable, version $(key CFBundleShortVersionString) build $(key CFBundleVersion), icon, launch screen, no permission text, iOS $(key MinimumOSVersion) or later"

for required in \
    "$app/$executable" \
    "$app/Assets.car"; do
  [ -e "$required" ] || fail "missing ${required#"$app"/}"
done
echo "    executable and asset catalogue"

archs="$(lipo -archs "$app/$executable" 2>/dev/null || true)"
[ "$archs" = "arm64" ] || fail "the executable is for '$archs', expected arm64"
bad="$(otool -L "$app/$executable" | tail -n +2 | awk '{print $1}' | grep -Ev '^(/usr/lib/|/System/Library/)' || true)"
[ -z "$bad" ] || fail "the executable links outside the system: $bad"
echo "    executable: arm64, links only the system"

if $signed; then
  codesign --verify --strict --verbose=2 "$app" || fail "the code signature does not verify"
  profile="$app/embedded.mobileprovision"
  [ -f "$profile" ] || fail "no embedded.mobileprovision: the build was not signed with a profile"
  decoded="$(mktemp)"
  security cms -D -i "$profile" > "$decoded" 2>/dev/null || fail "cannot decode embedded.mobileprovision"
  pkey() { /usr/libexec/PlistBuddy -c "Print :$1" "$decoded" 2>/dev/null || true; }
  case "$(pkey Entitlements:application-identifier)" in
    *."$bundle_id") ;;
    *) fail "the profile is for '$(pkey Entitlements:application-identifier)', not $bundle_id" ;;
  esac
  devices="$(/usr/libexec/PlistBuddy -c "Print :ProvisionedDevices" "$decoded" 2>/dev/null | grep -c '^    ' || true)"
  if $store; then
    [ "$devices" -eq 0 ] || fail "the profile names $devices device(s): it is not an App Store profile"
  else
    [ "$devices" -gt 0 ] || fail "the profile names no devices: it is not an ad-hoc profile"
  fi
  [ "$(pkey Entitlements:get-task-allow)" = "false" ] || fail "the profile allows a debugger to attach (get-task-allow): it is a development profile"
  kind="ad-hoc"; $store && kind="App Store"
  echo "    signed: the signature verifies; $kind profile '$(pkey Name)' ($devices device(s)), expires $(pkey ExpirationDate)"
  rm -f "$decoded"
fi
echo "OK: $target"
