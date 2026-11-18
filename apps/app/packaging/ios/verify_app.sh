#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Check that an iOS application (a built .app, or an exported .ipa) is complete.
#
#   apps/app/packaging/ios/verify_app.sh <name.app | package.ipa> --bundle-id <id> --executable <name>
#       [--version <x.y.z>] [--signed [--store]]
#
# What it looks for:
#
#   * Info.plist is valid, names the identifier, the executable, the version, the
#     icon set and a launch screen, says it is an iPhone/iPad program, allows a
#     ProMotion phone to run at 120 Hz (CADisableMinimumFrameDurationOnPhone is
#     true), and holds no usage text for a permission (the application asks for none);
#   * the compiled asset catalogue (the icon) is in the bundle, and holds the launch image and
#     the launch colour that Info.plist names, each in a light and a dark variant;
#   * the executable is for arm64 and links nothing outside the system: a link to
#     a framework or to a path on the build machine means the build picked up
#     the wrong libraries; and the bundle holds no dynamic library or framework, so
#     what the notices call statically linked is;
#   * it carries no Tracy profiler, and it reports the network code Qt links (Qt QML
#     brings Qt Network) without refusing it (docs/plan/stage-11.md, 11.2);
#   * PrivacyInfo.xcprivacy is at the top of the bundle, declares no tracking and no
#     collected data, and declares each required-reason API category the executable
#     calls (found from its imports and its Objective-C names);
#   * Info.plist declares that the application uses no encryption beyond the system's, and a
#     build number, as TestFlight asks;
#   * with --signed: the code signature verifies, the profile is embedded, it is an
#     ad-hoc one (it names devices and does not allow a debugger to attach) and it
#     belongs to this bundle identifier. With --store it must be an App Store one
#     instead: no devices, and no debugger. The profile and the signature carry the
#     default data protection entitlement with its exact value, and no iCloud, app
#     group or associated domain entitlement.
#
# macOS only (plutil, otool, nm, codesign, security).
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
    -h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
for usage in NSCameraUsageDescription NSLocationWhenInUseUsageDescription NSPhotoLibraryUsageDescription; do
  [ -z "$(key $usage)" ] || fail "Info.plist has $usage: the application asks for no permission but the two of dictation"
done
# Dictation (PRD NOTE-07) asks for the microphone and speech recognition at the first tap, and needs the texts for both.
for usage in NSMicrophoneUsageDescription NSSpeechRecognitionUsageDescription; do
  [ -n "$(key $usage)" ] || fail "Info.plist has no $usage (dictation asks for it)"
done
[ "$(key CFBundleIconName)" = "AppIcon" ] || fail "CFBundleIconName is '$(key CFBundleIconName)'"
# The setup file type: exported, named by a document type, opened by copy and not in place, and no file sharing.
[ "$(key UTExportedTypeDeclarations:0:UTTypeIdentifier)" = "$bundle_id.config" ] || fail "UTExportedTypeDeclarations does not export $bundle_id.config"
[ "$(key UTExportedTypeDeclarations:0:UTTypeTagSpecification:public.filename-extension:0)" = "ejconfig" ] || fail "the exported type does not have the extension ejconfig"
# Not public.json: the system previews JSON, and an AirDropped setup file would open as text and not in the application.
[ "$(key UTExportedTypeDeclarations:0:UTTypeConformsTo:0)" = "public.data" ] || fail "the exported type does not conform to public.data"
for index in 0 1 2 3; do
  [ "$(key "UTExportedTypeDeclarations:0:UTTypeConformsTo:$index")" != "public.json" ] || fail "the exported type conforms to public.json: AirDrop would show a preview of the setup file"
done
[ "$(key CFBundleDocumentTypes:0:LSItemContentTypes:0)" = "$bundle_id.config" ] || fail "CFBundleDocumentTypes does not name $bundle_id.config"
[ "$(key LSSupportsOpeningDocumentsInPlace)" = "false" ] || fail "LSSupportsOpeningDocumentsInPlace is '$(key LSSupportsOpeningDocumentsInPlace)', expected false"
[ "$(key CADisableMinimumFrameDurationOnPhone)" = "true" ] || fail "CADisableMinimumFrameDurationOnPhone is '$(key CADisableMinimumFrameDurationOnPhone)', expected true: a ProMotion phone would run the application at 60 Hz"
[ "$(key UIFileSharingEnabled)" != "true" ] || fail "UIFileSharingEnabled is true: the Documents folder would be listed in Files"
[ -n "$(key UILaunchScreen)" ] || fail "Info.plist has no UILaunchScreen (iOS would run the program in a legacy window)"
# Orientations (docs/plan/stage-8.md): the phone is portrait only, the iPad has all four and is not full screen only, and the
# application has one window.
[ "$(key UISupportedInterfaceOrientations:0)" = "UIInterfaceOrientationPortrait" ] && [ -z "$(key UISupportedInterfaceOrientations:1)" ] || fail "UISupportedInterfaceOrientations is not portrait only"
ipad_orientations=""
for index in 0 1 2 3; do ipad_orientations="$ipad_orientations $(key "UISupportedInterfaceOrientations~ipad:$index")"; done
[ -z "$(key "UISupportedInterfaceOrientations~ipad:4")" ] || fail "UISupportedInterfaceOrientations~ipad lists more than four orientations"
for orientation in UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight; do
  case "$ipad_orientations " in *" $orientation "*) ;; *) fail "UISupportedInterfaceOrientations~ipad does not list $orientation (App Store Connect asks for all four)" ;; esac
done
[ -z "$(key UIRequiresFullScreen)" ] || fail "Info.plist has UIRequiresFullScreen: the iPad application runs in Split View and Stage Manager windows"
[ "$(key UIApplicationSupportsMultipleScenes)" != "true" ] || fail "UIApplicationSupportsMultipleScenes is true: the application has one window"
[ "$(key DTPlatformName)" = "iphoneos" ] || fail "DTPlatformName is '$(key DTPlatformName)'; this is not a device build"
[ -n "$(key MinimumOSVersion)" ] || fail "Info.plist has no MinimumOSVersion"
if [ -n "$version" ]; then
  [ "$(key CFBundleShortVersionString)" = "$version" ] || fail "CFBundleShortVersionString is '$(key CFBundleShortVersionString)', expected $version"
fi
[ "$(key ITSAppUsesNonExemptEncryption)" = "false" ] || fail "ITSAppUsesNonExemptEncryption is '$(key ITSAppUsesNonExemptEncryption)': TestFlight would ask about encryption for every build"
[ -n "$(key CFBundleVersion)" ] || fail "Info.plist has no CFBundleVersion (the build number)"
echo "    Info.plist: identifier, executable, version $(key CFBundleShortVersionString) build $(key CFBundleVersion), icon, launch screen, 120 Hz allowed, no permission text, iOS $(key MinimumOSVersion) or later"

for required in \
    "$app/$executable" \
    "$app/Assets.car"; do
  [ -e "$required" ] || fail "missing ${required#"$app"/}"
done
echo "    executable and asset catalogue"

# The launch screen: the image and the colour that Info.plist names are in the compiled catalogue, each in a light and a dark
# variant (assetutil lists the variant for the dark appearance as UIAppearanceDark). Without them iOS shows a blank launch screen.
[ "$(key UILaunchScreen:UIImageRespectsSafeAreaInsets)" = "false" ] || fail "UIImageRespectsSafeAreaInsets is '$(key UILaunchScreen:UIImageRespectsSafeAreaInsets)', expected false"
assets_json="$(mktemp)"
assetutil --info "$app/Assets.car" > "$assets_json" 2>/dev/null || fail "assetutil cannot read Assets.car"
for launch in "UIImageName:Image" "UIColorName:Color"; do
  launch_key="${launch%%:*}"
  launch_kind="${launch##*:}"
  launch_name="$(key "UILaunchScreen:$launch_key")"
  [ -n "$launch_name" ] || fail "UILaunchScreen has no $launch_key"
  variants="$(python3 - "$assets_json" "$launch_name" "$launch_kind" <<'PY'
import json
import sys

path, name, kind = sys.argv[1:4]
with open(path, encoding="utf-8") as handle:
    entries = json.load(handle)
found = [e for e in entries if e.get("Name") == name and e.get("AssetType") == kind]
dark = [e for e in found if e.get("Appearance") == "UIAppearanceDark"]
print(len(found) - len(dark), len(dark))
PY
)" || fail "cannot read the asset list of Assets.car"
  read -r light_variants dark_variants <<< "$variants"
  { [ "$light_variants" -ge 1 ] && [ "$dark_variants" -ge 1 ]; } || fail "Assets.car has $light_variants light and $dark_variants dark variants of the launch $launch_kind '$launch_name' (it needs both)"
done
rm -f "$assets_json"
echo "    launch screen: image $(key UILaunchScreen:UIImageName) and colour $(key UILaunchScreen:UIColorName) are in Assets.car, light and dark"

archs="$(lipo -archs "$app/$executable" 2>/dev/null || true)"
[ "$archs" = "arm64" ] || fail "the executable is for '$archs', expected arm64"
bad="$(otool -L "$app/$executable" | tail -n +2 | awk '{print $1}' | grep -Ev '^(/usr/lib/|/System/Library/)' || true)"
[ -z "$bad" ] || fail "the executable links outside the system: $bad"
echo "    executable: arm64, links only the system"
# Qt and everything it bundles are static libraries inside the executable. The About page and the notices say so
# (tools/licences/notices.py reads it off the link line), so a library file in the bundle would make them wrong.
shipped_libraries="$(find "$app" \( -name '*.dylib' -o -name '*.framework' -o -name 'Frameworks' \) | head -n 5)"
[ -z "$shipped_libraries" ] || fail "the bundle holds a dynamic library, but the licence notices say everything is linked statically: $shipped_libraries"
echo "    no dynamic library or framework in the bundle: every third-party library is linked statically"

# Network code and the profiler (docs/plan/stage-11.md, 11.2, T11-5). Qt QML links Qt Network (the kit is built with
# qml_network, so QML could load a remote URL), and Qt Network links CFNetwork and Network. So the executable carries
# network code that the application never calls: tools/ci/check_no_network.py keeps the application's own code free of
# network use and of remote URLs. Here the network code is reported, not refused, so that a change in what is linked shows
# in the log; Tracy, the profiler, opens a socket of its own and must never be in a device build.
imports="$(mktemp)"
nm -u "$app/$executable" 2>/dev/null | awk '{print $NF}' | sort -u > "$imports" || fail "cannot read the imported symbols of the executable"
[ -s "$imports" ] || fail "the executable imports no symbols: nm could not read it"
profiler="$(grep -a -o -E 'TracyProfiler|___tracy_[a-z_]+' "$app/$executable" | sort -u | tr '\n' ' ' || true)"
[ -z "$profiler" ] || fail "the executable carries the Tracy profiler: $profiler"
network_frameworks="$(otool -L "$app/$executable" | tail -n +2 | awk '{print $1}' | grep -E '/(CFNetwork|Network)\.framework/' | xargs -n1 basename 2>/dev/null | tr '\n' ' ' || true)"
network_calls="$(grep -E '^_(getaddrinfo|gethostbyname|connect|nw_connection_.*|nw_listener_.*|CFSocketCreate.*|CFStreamCreatePairWithSocket.*|SSLHandshake|sec_protocol_options_.*)$' "$imports" | tr '\n' ' ' || true)"
network_classes="$(grep -a -o -E 'QNetworkAccessManager|QTcpSocket|QUdpSocket|QSslSocket|QWebSocket|QLocalSocket' "$app/$executable" | sort -u | tr '\n' ' ' || true)"
echo "    no profiler; network code linked by Qt and not called by the application: frameworks ${network_frameworks:-none}; imports ${network_calls:-none}; classes ${network_classes:-none}"

# The privacy manifest (11.2, T11-6): no tracking, nothing collected, and a declared category for each required-reason
# API that the executable calls. A category is found by the system functions it imports, or for Objective-C APIs by the
# class or selector name in the binary. The manifest may declare a category the scan does not find (Apple's scan sees
# what this one does not), and that is shown, not refused.
manifest="$app/PrivacyInfo.xcprivacy"
[ -f "$manifest" ] || fail "no PrivacyInfo.xcprivacy at the top of the bundle"
plutil -lint "$manifest" > /dev/null || fail "PrivacyInfo.xcprivacy is not a valid property list"
[ "$(plutil -extract NSPrivacyTracking raw -o - "$manifest" 2>/dev/null || true)" = "false" ] || fail "PrivacyInfo.xcprivacy: NSPrivacyTracking is not false"
for empty_key in NSPrivacyTrackingDomains NSPrivacyCollectedDataTypes; do
  [ "$(plutil -extract "$empty_key" raw -o - "$manifest" 2>/dev/null || true)" = "0" ] || fail "PrivacyInfo.xcprivacy: $empty_key is not an empty array"
done
declared="$(plutil -convert json -o - "$manifest" | python3 -c 'import json,sys; print(" ".join(e["NSPrivacyAccessedAPIType"] for e in json.load(sys.stdin).get("NSPrivacyAccessedAPITypes", [])))')" \
  || fail "cannot read the accessed API types of PrivacyInfo.xcprivacy"
uses_import() { grep -Eq "$1" "$imports"; }
uses_name() { grep -a -q -E "$1" "$app/$executable"; }
found=""
if uses_import '^_(stat|fstat|lstat|fstatat|getattrlist|fgetattrlist|getattrlistat|getattrlistbulk)$'; then found="$found NSPrivacyAccessedAPICategoryFileTimestamp"; fi
if uses_import '^_mach_absolute_time$' || uses_name 'systemUptime'; then found="$found NSPrivacyAccessedAPICategorySystemBootTime"; fi
if uses_import '^_(statfs|statvfs|fstatfs|fstatvfs)$' || uses_name 'NSFileSystemFreeSize|volumeAvailableCapacity'; then found="$found NSPrivacyAccessedAPICategoryDiskSpace"; fi
if uses_import '^_OBJC_CLASS_\$_NSUserDefaults$' || uses_import '^_CFPreferences'; then found="$found NSPrivacyAccessedAPICategoryUserDefaults"; fi
if uses_name 'activeInputModes'; then found="$found NSPrivacyAccessedAPICategoryActiveKeyboards"; fi
undeclared=""
for category in $found; do
  case " $declared " in
    *" $category "*) ;;
    *) undeclared="$undeclared $category" ;;
  esac
done
[ -z "$undeclared" ] || fail "the executable calls required-reason APIs that PrivacyInfo.xcprivacy does not declare:$undeclared"

for category in $declared; do
  case " $found " in
    *" $category "*) ;;
    *) echo "    note: PrivacyInfo.xcprivacy declares $category, which this scan did not find in the executable" ;;
  esac
done
rm -f "$imports"
echo "    privacy manifest: no tracking, nothing collected; required-reason APIs found:${found:- none}; all declared"

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
  # The entitlements in the signature: the default data protection class, and nothing for iCloud, app groups or
  # associated domains. The profile must carry the same class, or the signed build is refused.
  protection_key="com.apple.developer.default-data-protection"
  protection_class="NSFileProtectionCompleteUntilFirstUserAuthentication"
  [ "$(pkey "Entitlements:$protection_key")" = "$protection_class" ] || fail "the profile does not carry $protection_key = $protection_class"
  signed_entitlements="$(mktemp)"
  codesign -d --entitlements :- "$app" > "$signed_entitlements" 2>/dev/null || fail "cannot read the entitlements of the signature"
  [ "$(/usr/libexec/PlistBuddy -c "Print :$protection_key" "$signed_entitlements" 2>/dev/null || true)" = "$protection_class" ]     || fail "the signature does not carry $protection_key = $protection_class"
  if grep -Eq 'com\.apple\.developer\.icloud|com\.apple\.security\.application-groups|com\.apple\.developer\.associated-domains' "$signed_entitlements"; then
    fail "the signature carries an iCloud, app group or associated domain entitlement: the application uses none"
  fi
  rm -f "$signed_entitlements"
  kind="ad-hoc"; $store && kind="App Store"
  echo "    signed: the signature verifies; $kind profile '$(pkey Name)' ($devices device(s)), default data protection $protection_class, expires $(pkey ExpirationDate)"
  rm -f "$decoded"
fi
echo "OK: $target"
